// =============================================================================
// Datalogger Rev B — RP2040 da LilyGO T-PicoC3.
//
// Lê todos os sensores a cada 1 s, calcula potência/energia e estado de carga,
// grava o CSV no microSD e envia um frame de telemetria (payload schema 3)
// para a Heltec (LoRa) e para o ESP32-C3 (BLE do tablet).
//
// Compilar e gravar:  cd Datalogger && make compile && make upload PORT=COMx
//   [AJUSTE] PORT no Makefile (ou na linha de comando).
//   Bibliotecas: ADS1X15 (Rob Tillaart), OneWire, DallasTemperature; SD,
//   SerialPIO e SoftwareSPI vêm com o core rp2040 (arduino-pico).
// Pinos:        config/pins_rev_b.h
// Calibrações:  config/rev_b_config.h  (procure [CALIBRAÇÃO])
// Contratos:    docs/specs/ (procure [CONTRATO] antes de mudar payload/CSV)
// =============================================================================
#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <SD.h>

#include "Ads1115Bank.h"
#include "Acs712Channel.h"
#include "BatteryIna226.h"
#include "BatterySoc.h"
#include "CsvLoggerRevB.h"
#include "FlowMeter.h"
#include "GpsNeo6m.h"
#include "PinMapRevB.h"
#include "PowerChannel.h"
#include "RadioLink.h"
#include "TelemetryFrame.h"
#include "TelemetryPayloadRevB.h"
#include "config/rev_b_config.h"

using namespace RevB;

// Arquivo onde o SoC é salvo para sobreviver a um reset.
static constexpr const char* SOC_STATE_PATH = "/soc_state.txt";

// -----------------------------------------------------------------------------
// Objetos dos sensores e enlaces.
// [NOVO SENSOR] Declare aqui a instância do novo sensor (ex.: outro
// Acs712Channel + PowerChannel), inicialize em setup() e leia em loop().
// -----------------------------------------------------------------------------
static FlowMeter flowA;                       // fluxômetro A (GP23)
static FlowMeter flowB;                       // fluxômetro B (GP24)
static Ads1115Bank ads;                       // 4x ADS1115 no I2C
// Correntes dos 3 MPPTs (ACS712-30A), calibração em rev_b_config.h.
static Acs712Channel acsA(ACS712_MPPT_A.sensitivityVPerAAt5V, ACS712_MPPT_A.frontEndGain, ACS712_MPPT_A.zeroOffsetA);
static Acs712Channel acsB(ACS712_MPPT_B.sensitivityVPerAAt5V, ACS712_MPPT_B.frontEndGain, ACS712_MPPT_B.zeroOffsetA);
static Acs712Channel acsC(ACS712_MPPT_C.sensitivityVPerAAt5V, ACS712_MPPT_C.frontEndGain, ACS712_MPPT_C.zeroOffsetA);
// Corrente da bateria: INA226 low-side no shunt externo.
static BatteryIna226 battery(INA226_SHUNT_OHM, INA226_CURRENT_SIGN, INA226_CONFIG_REGISTER);
static BatterySoc soc;                        // estado de carga (contagem de Ah)
// Um PowerChannel por nó elétrico (P = V x I do mesmo nó + energia).
static PowerChannel powerA;
static PowerChannel powerB;
static PowerChannel powerC;
static PowerChannel powerBattery;
static GpsNeo6m gps;                          // parser NMEA do NEO-6M
static CsvLoggerRevB csv;                     // CSV schema 2
static RadioLink radioLink;                   // RP2040 -> Heltec (LoRa)
static RadioLink c3Link;                      // RP2040 -> ESP32-C3 (DashboardAntenna / BLE)
// Temperaturas DS18B20 no barramento OneWire.
static OneWire oneWire(Pins::PIN_ONEWIRE);
static DallasTemperature temperatures(&oneWire);
static DeviceAddress temperatureRoms[MAX_TEMPERATURE_SENSORS];  // ROMs descobertas no boot
static float temperatureValues[MAX_TEMPERATURE_SENSORS] = {};   // última leitura de cada sensor
static uint8_t temperatureCount = 0;
static bool temperatureConversionPending = false;
static uint32_t temperatureRequestMs = 0;
// Estado do microSD e temporizadores.
static File csvFile;
static bool sdReady = false;
static uint16_t sequence = 0;                 // seq16 do frame (e da linha CSV)
static uint32_t lastSampleMs = 0;
static uint32_t lastFlushMs = 0;
static uint32_t lastSocPersistMs = 0;
static uint32_t lastAdsErrors = 0;            // para detectar erro NOVO de ADS no ciclo

// Tudo o que foi medido em um ciclo de aquisição de 1 s.
struct Sample {
  PowerReading mppt[3];   // MPPT A, B, C
  PowerReading battery;
  FlowReading flowA;
  FlowReading flowB;
  float supply5V = NAN;   // 5 V medido dos ACS712
  SocState soc;
  uint32_t errorFlags = 0;
};

// Rotinas de interrupção dos fluxômetros (curtas: só contam pulsos).
static void flowAInterrupt() { flowA.onPulse(); }
static void flowBInterrupt() { flowB.onPulse(); }

// Procura os DS18B20 no barramento (família 0x28) e guarda as ROMs.
// A ordem segue a busca do OneWire; registre ROM x posição física na montagem.
static void scanTemperatures() {
  oneWire.reset_search();
  while (temperatureCount < MAX_TEMPERATURE_SENSORS && oneWire.search(temperatureRoms[temperatureCount])) {
    if (temperatureRoms[temperatureCount][0] == 0x28 &&
        OneWire::crc8(temperatureRoms[temperatureCount], 7) == temperatureRoms[temperatureCount][7]) {
      temperatures.setResolution(temperatureRoms[temperatureCount], 12);  // 12 bits = 0,0625 °C
      temperatureValues[temperatureCount] = NAN;
      ++temperatureCount;
    }
  }
  oneWire.reset_search();
}

// Leitura não bloqueante: pede a conversão e só lê depois de 800 ms.
static void serviceTemperatures(uint32_t nowMs) {
  if (!temperatureConversionPending) {
    temperatures.requestTemperatures();  // inicia a conversão em todos os sensores
    temperatureRequestMs = nowMs;
    temperatureConversionPending = true;
    return;
  }
  if (nowMs - temperatureRequestMs < TEMPERATURE_CONVERSION_MS) return;
  for (uint8_t i = 0; i < temperatureCount; ++i) {
    const float value = temperatures.getTempC(temperatureRoms[i]);
    // Sensor desconectado/erro vira nan (nunca 0 °C ou -127 °C).
    temperatureValues[i] = (value == DEVICE_DISCONNECTED_C || isnan(value) || isinf(value)) ? NAN : value;
  }
  temperatureConversionPending = false;
}

// Lê um canal do ADS e aplica a relação do divisor (V_real = V_ADC x ratio).
static bool readScaledVoltage(AdcChannelRef channel, float ratio, float& value) {
  float adcVoltage = NAN;
  if (!ads.readVoltage(channel.device, channel.channel, adcVoltage)) {
    value = NAN;
    return false;
  }
  value = adcVoltage * ratio;
  return !isnan(value) && !isinf(value);
}

// As saídas dos ACS712 têm ruído: soma várias conversões e tira a média (ADR-005).
static bool readAveragedAdc(AdcChannelRef channel, float& adcVoltage) {
  float sum = 0.0f;
  for (uint8_t i = 0; i < ACS712_AVERAGE_SAMPLES; ++i) {
    float value = NAN;
    if (!ads.readVoltage(channel.device, channel.channel, value)) {
      adcVoltage = NAN;
      return false;
    }
    sum += value;
  }
  adcVoltage = sum / ACS712_AVERAGE_SAMPLES;
  return true;
}

// Mede corrente (ACS712) e tensão (divisor) de UM MPPT e calcula potência/energia.
// [NOVO SENSOR] Um quarto MPPT reutiliza esta função com novos canais ADS.
static PowerReading readMppt(const Acs712Channel& sensor, AdcChannelRef currentAdc, AdcChannelRef voltageAdc,
                             float dividerRatio, float supply5V, bool supplyValid, PowerChannel& power,
                             uint32_t elapsedMs) {
  float adcVoltage = NAN, voltage = NAN;
  const bool adcValid = readAveragedAdc(currentAdc, adcVoltage);
  const bool voltageValid = readScaledVoltage(voltageAdc, dividerRatio, voltage);
  // Sem um 5 V de referência confiável a corrente ratiométrica é desconhecida.
  const Acs712Reading current = supplyValid ? sensor.convert(adcVoltage, supply5V) : Acs712Reading{};
  return power.update(voltage, voltageValid, current.currentA, adcValid && current.valid, elapsedMs);
}

// Copia uma leitura de potência (e o fluxo, se houver) para um canal do payload.
static void fillTelemetryChannel(TelemetryPayloadRevB::Channel& channel, const PowerReading& power,
                                 const FlowReading* flow) {
  channel.voltageV = power.voltageV;
  channel.currentA = power.currentA;
  channel.powerW = power.powerW;
  channel.energyWh = power.energyWh;
  channel.flowLpm = flow ? flow->litersPerMinute : NAN;
  channel.validFlags = power.validFlags;
  if (flow && flow->valid) channel.validFlags |= (1UL << 8);  // bit 8 = fluxo válido
}

// Monta o payload schema 3 e envia o mesmo frame para a Heltec e para o C3.
// [CONTRATO] Os campos seguem TelemetryPayloadRevB.h / TELEMETRY_FRAME.md.
static void sendTelemetryFrame(uint32_t nowMs, const Sample& sample) {
  TelemetryPayloadRevB payload = {};
  payload.schemaVersion = TelemetryPayloadRevB::SCHEMA_VERSION;
  payload.channelCount = TelemetryPayloadRevB::CHANNEL_COUNT;
  payload.temperatureCount = temperatureCount;
  payload.timestampMs = nowMs;
  // Energia: fluxo A acompanha o canal MPPT_A e fluxo B o MPPT_B.
  fillTelemetryChannel(payload.channels[TelemetryPayloadRevB::MPPT_A], sample.mppt[0], &sample.flowA);
  fillTelemetryChannel(payload.channels[TelemetryPayloadRevB::MPPT_B], sample.mppt[1], &sample.flowB);
  fillTelemetryChannel(payload.channels[TelemetryPayloadRevB::MPPT_C], sample.mppt[2], nullptr);
  fillTelemetryChannel(payload.channels[TelemetryPayloadRevB::BATTERY], sample.battery, nullptr);
  // Temperaturas: posições sem sensor ficam nan.
  for (uint8_t i = 0; i < TelemetryPayloadRevB::TEMPERATURE_COUNT; ++i)
    payload.temperatures[i] = i < temperatureCount ? temperatureValues[i] : NAN;

  // GPS.
  const GpsReading& gpsReading = gps.reading();
  payload.gpsValid = gpsReading.valid ? 1 : 0;
  payload.gpsFixQuality = gpsReading.fixQuality;
  payload.gpsSatellites = gpsReading.satellites;
  payload.latitude = gpsReading.latitude;
  payload.longitude = gpsReading.longitude;
  payload.hdop = gpsReading.hdop;
  payload.speedKn = gpsReading.speedKnots;
  payload.courseDeg = gpsReading.courseDeg;
  // IMU: o driver Rev B ainda não está ativo; campos ficam inválidos.
  // [NOVO SENSOR] Ao implementar o LSM6DS3, preencha imuValid/accel/pitch/roll aqui.
  payload.imuValid = 0;
  payload.accelXG = NAN;
  payload.accelYG = NAN;
  payload.accelZG = NAN;
  payload.pitchDeg = NAN;
  payload.rollDeg = NAN;

  // Estado de carga da bateria.
  payload.socPct = sample.soc.socPct;
  payload.ahFromFull = sample.soc.ahFromFull;
  payload.socSource = static_cast<uint8_t>(sample.soc.source);

  // Bloco do motor fica inválido até existir o gateway VESC (ADR-004, M9).
  // [NOVO SENSOR] No M9: copiar aqui o último frame 0x08 recebido do C3.
  payload.motorValid = 0;
  payload.vescFault = 0;
  payload.motorAgeMs = 0xFFFF;
  payload.motorRpm = NAN;
  payload.motorTempC = NAN;
  payload.fetTempC = NAN;
  payload.motorInputCurrentA = NAN;
  payload.motorInputVoltageV = NAN;
  payload.motorDutyPct = NAN;

  payload.supply5V = sample.supply5V;
  payload.errorFlags = sample.errorFlags;

  // Empacota no frame Rev B e envia aos dois destinos (sem esperar ACK).
  TelemetryFrame frame;
  frame.type = FrameType::TELEMETRY;
  frame.sequence = sequence++;
  frame.length = sizeof(payload);
  memcpy(frame.payload, &payload, sizeof(payload));

  radioLink.send(frame);
  c3Link.send(frame);
}

// microSD falhou em operação: marca SD_OFFLINE e continua o CSV na USB.
static void enterSdDegradedMode() {
  sdReady = false;
  csvFile.close();
  csv.begin(Serial);
}

// Um arquivo novo por sessão: usa o primeiro /telemetry_NNNN.csv livre
// (millis() no boot é quase constante, então não serve para o nome).
static bool openSessionFile() {
  char path[24];
  for (uint16_t index = 0; index < 10000; ++index) {
    snprintf(path, sizeof(path), "/telemetry_%04u.csv", index);
    if (SD.exists(path)) continue;
    csvFile = SD.open(path, FILE_WRITE);
    return static_cast<bool>(csvFile);
  }
  return false;
}

// Persistência do SoC no microSD (ADR-005). Sem RTC, a idade do valor é
// desconhecida; o valor restaurado fica SOC_UNCALIBRATED até ressincronizar.
static void restoreSoc() {
  if (!sdReady || !SD.exists(SOC_STATE_PATH)) return;
  File file = SD.open(SOC_STATE_PATH, FILE_READ);
  if (!file) return;
  const float ahFromFull = file.parseFloat();
  file.close();
  soc.restore(ahFromFull);
}

// Regrava o /soc_state.txt a cada BATTERY_SOC_PERSIST_MS (60 s).
static void persistSoc(uint32_t nowMs) {
  if (!sdReady || !soc.hasValue() || nowMs - lastSocPersistMs < BATTERY_SOC_PERSIST_MS) return;
  lastSocPersistMs = nowMs;
  SD.remove(SOC_STATE_PATH);
  File file = SD.open(SOC_STATE_PATH, FILE_WRITE);
  if (!file) return;
  file.println(soc.state().ahFromFull, 4);
  file.close();
}

// Escreve uma linha CSV de um nó. Campos não informados ficam nan.
static void writeCsvRow(const char* node, uint32_t nowMs, const PowerReading* power, const FlowReading* flow,
                        uint32_t errorFlags, float socPct = NAN, float rpm = NAN, float speedKn = NAN,
                        float courseDeg = NAN, float temperatureC = NAN) {
  CsvRow row;
  row.node = node;
  row.timestampMs = nowMs;
  row.sequence = sequence;  // mesmo seq do frame enviado logo depois
  if (power) {
    row.voltageV = power->voltageV;
    row.currentA = power->currentA;
    row.powerW = power->powerW;
    row.energyWh = power->energyWh;
    row.validFlags = power->validFlags;
  }
  if (flow) {
    row.flowLpm = flow->litersPerMinute;
    if (flow->valid) row.validFlags |= (1UL << 8);
  }
  row.temperatureC = temperatureC;
  row.errorFlags = errorFlags;
  row.socPct = socPct;
  row.rpm = rpm;
  row.speedKn = speedKn;
  row.courseDeg = courseDeg;
  // Falha de escrita no cartão => modo degradado (CSV pela USB).
  if (!csv.write(row) && sdReady) enterSdDegradedMode();
}

// Seis linhas por amostra, todas com o mesmo seq (CSV_SCHEMA.md, schema 2).
// [NOVO SENSOR] Um nó novo = uma chamada writeCsvRow() com um nome novo
// (e documentar o nó na spec do CSV) [CONTRATO].
static void writeCsvSample(uint32_t nowMs, const Sample& sample) {
  const GpsReading& gpsReading = gps.reading();
  writeCsvRow("MPPT_A", nowMs, &sample.mppt[0], &sample.flowA, sample.errorFlags);
  writeCsvRow("MPPT_B", nowMs, &sample.mppt[1], &sample.flowB, sample.errorFlags);
  writeCsvRow("MPPT_C", nowMs, &sample.mppt[2], nullptr, sample.errorFlags);
  writeCsvRow("BATTERY", nowMs, &sample.battery, nullptr, sample.errorFlags, sample.soc.socPct);
  // Os dados do VESC chegam com o ADR-004 (M9); até lá a linha MOTOR é nan + VESC_STALE.
  writeCsvRow("MOTOR", nowMs, nullptr, nullptr, sample.errorFlags);
  writeCsvRow("NAV", nowMs, nullptr, nullptr, sample.errorFlags, NAN, NAN, gpsReading.speedKnots,
              gpsReading.courseDeg);
}

void setup() {
  // USB serial: diagnóstico e CSV no modo degradado. [AJUSTE] Monitor a 115200.
  Serial.begin(115200);
  // UARTs por PIO (LoRa/GPS), I2C, pinos do SD e dos fluxômetros, reset da Heltec.
  PinMapRevB::begin();

  // UART interna para o C3 (GP8/GP9), separada da UART do LoRa.
  // [AJUSTE] 115200 precisa ser igual ao UART_BAUD do DashboardAntenna.
  Serial2.setTX(Pins::PIN_C3_UART_TX);
  Serial2.setRX(Pins::PIN_C3_UART_RX);
  Serial2.begin(115200);
  radioLink.begin(RevB::SerialLoRa);
  c3Link.begin(Serial2);

  // Fluxômetros: conta pulsos na borda de subida.
  flowA.begin(Pins::PIN_FLOW_A);
  flowB.begin(Pins::PIN_FLOW_B);
  attachInterrupt(digitalPinToInterrupt(Pins::PIN_FLOW_A), flowAInterrupt, RISING);
  attachInterrupt(digitalPinToInterrupt(Pins::PIN_FLOW_B), flowBInterrupt, RISING);
  gps.begin(SerialGps);
  ads.begin();
  battery.begin(Wire, INA226_BATTERY_ADDRESS);

  // Temperaturas: modo assíncrono (não espera os 750 ms de conversão).
  temperatures.begin();
  temperatures.setWaitForConversion(false);
  scanTemperatures();

  // microSD por SPI em PIO (pinos 18/14/19, ADR-006).
  // [AJUSTE] Se algum cartão falhar, reduza a velocidade com SD.begin(cs, cfg, SdSpi).
  sdReady = SD.begin(Pins::PIN_SD_CS, SdSpi);  // PIO SPI on 18/14/19 (ADR-006)
  restoreSoc();
  if (sdReady) sdReady = openSessionFile();
  if (sdReady && !csv.begin(csvFile)) {
    csvFile.close();
    sdReady = false;
  }
  // Modo degradado: a aquisição continua e o CSV sai pela USB.
  if (!sdReady) csv.begin(Serial);
}

void loop() {
  const uint32_t now = millis();
  // Tarefas contínuas (não bloqueantes): GPS e temperaturas.
  gps.update(now);
  serviceTemperatures(now);
  // [AJUSTE] Período de aquisição/telemetria: 1000 ms. Mudar afeta o tempo no
  // ar do LoRa (~0,39 s por frame de 250 B em SF7, ADR-007) e o cálculo de energia/SoC.
  if (now - lastSampleMs < 1000) return;
  const uint32_t elapsedMs = lastSampleMs == 0 ? 0 : now - lastSampleMs;
  lastSampleMs = now;

  Sample sample;
  sample.flowA = flowA.sample(now);
  sample.flowB = flowB.sample(now);

  // 5 V de referência dos ACS712: fora da faixa, as correntes dos MPPTs ficam nan.
  float supply5V = NAN;
  const bool supplyRead = readScaledVoltage(VCC5_REF_ADC, VCC5_DIVIDER_RATIO, supply5V);
  const bool supplyValid = supplyRead && supply5V >= VCC5_MIN_V && supply5V <= VCC5_MAX_V;
  sample.supply5V = supply5V;

  // Três MPPTs: corrente (ACS712) + tensão (divisor HV80) no mesmo nó.
  sample.mppt[0] = readMppt(acsA, I_MPPT_A_ADC, V_MPPT_A_ADC, V_MPPT_A_DIVIDER_RATIO, supply5V, supplyValid, powerA, elapsedMs);
  sample.mppt[1] = readMppt(acsB, I_MPPT_B_ADC, V_MPPT_B_ADC, V_MPPT_B_DIVIDER_RATIO, supply5V, supplyValid, powerB, elapsedMs);
  sample.mppt[2] = readMppt(acsC, I_MPPT_C_ADC, V_MPPT_C_ADC, V_MPPT_C_DIVIDER_RATIO, supply5V, supplyValid, powerC, elapsedMs);

  // Bateria: tensão pelo divisor HV80 e corrente pelo INA226; depois o SoC.
  float vBat = NAN, iBattery = NAN;
  const bool vBatValid = readScaledVoltage(V_BAT_DIV_HV80_ADC, V_BAT_DIVIDER_RATIO, vBat);
  const bool iBatteryValid = battery.readCurrent(iBattery);
  sample.battery = powerBattery.update(vBat, vBatValid, iBattery, iBatteryValid, elapsedMs);
  sample.soc = soc.update(now, vBat, vBatValid, iBattery, iBatteryValid);

  // Flags de erro do ciclo (bits em TelemetryPayloadRevB.h).
  // Erros de ADS são acumulados; só um erro NOVO neste ciclo liga o bit.
  const uint32_t adsErrors = ads.errors();
  const bool adsFault = adsErrors != lastAdsErrors;
  lastAdsErrors = adsErrors;
  sample.errorFlags = (!sdReady ? ERR_SD_OFFLINE : 0) |
                      (!gps.reading().valid ? ERR_GPS_NO_FIX : 0) |
                      (!temperatureCount ? ERR_NO_TEMPERATURE : 0) |
                      (adsFault ? ERR_ADS_ERROR : 0) |
                      (!iBatteryValid ? ERR_INA226_ERROR : 0) |
                      ERR_VESC_STALE |  // ainda sem gateway VESC (ADR-004)
                      (!supplyValid ? ERR_SUPPLY_5V_OUT_OF_RANGE : 0) |
                      (sample.soc.uncalibrated ? ERR_SOC_UNCALIBRATED : 0);

  // Saídas: CSV, telemetria (LoRa + BLE) e persistência do SoC.
  writeCsvSample(now, sample);
  sendTelemetryFrame(now, sample);
  persistSoc(now);
  // Flush do cartão a cada 5 s (equilíbrio entre segurança e desgaste/latência).
  if (sdReady && now - lastFlushMs >= 5000) {
    csvFile.flush();
    lastFlushMs = now;
  }
}
