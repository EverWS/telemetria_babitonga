#pragma once

#include <Arduino.h>

// =============================================================================
// Configuração e calibração do Datalogger (compile-time).
//
// Equivale aos defaults de docs/specs/CONFIG_SCHEMA.md (schema_version 2) e
// das decisões da docs/adr/ADR-005-sensores-energia.md. Todo valor de
// calibração da placa fica AQUI, nunca dentro do .ino.
//
// Como usar os marcadores deste arquivo:
//   [AJUSTE]      conferir/alterar antes de compilar para uma placa nova;
//   [CALIBRAÇÃO]  valor medido em bancada (multímetro, alicate, fonte);
//   [NOVO SENSOR] onde acrescentar um canal ou sensor novo;
//   [CONTRATO]    mudar exige atualizar spec + decodificadores (web_dash, APK).
// Depois de mudar qualquer valor: recompile (make compile), rode os testes
// (python -m unittest discover -s Datalogger/tests) e grave na placa.
// =============================================================================
namespace RevB {

// [CONTRATO] Versão do esquema de configuração (CONFIG_SCHEMA.md).
static constexpr uint8_t CONFIG_SCHEMA_VERSION = 2;

// Referência a um canal físico: qual ADS1115 (0..3) e qual entrada AIN (0..3).
struct AdcChannelRef {
  uint8_t device;   // 0 = 0x48, 1 = 0x49, 2 = 0x4A, 3 = 0x4B
  uint8_t channel;  // AIN0..AIN3
};

// -----------------------------------------------------------------------------
// Mapa dos canais ADS1115 (ADR-005). ADS2 (0x4A) e ADS3 (0x4B) estão livres.
// [NOVO SENSOR] Para um sensor analógico novo: escolha um AIN livre em
// ADS2/ADS3, declare aqui um AdcChannelRef, leia com readScaledVoltage() no
// Datalogger.ino e, se ele for para a telemetria, atualize o payload
// (TelemetryPayloadRevB.h) e a spec TELEMETRY_FRAME.md [CONTRATO].
// -----------------------------------------------------------------------------
static constexpr AdcChannelRef I_MPPT_A_ADC = {0, 0};        // corrente MPPT A (ACS712 A)
static constexpr AdcChannelRef I_MPPT_B_ADC = {0, 1};        // corrente MPPT B (ACS712 B)
static constexpr AdcChannelRef I_MPPT_C_ADC = {0, 2};        // corrente MPPT C (ACS712 C)
static constexpr AdcChannelRef VCC5_REF_ADC = {0, 3};        // 5 V que alimenta os ACS712
static constexpr AdcChannelRef V_MPPT_A_ADC = {1, 0};        // tensão na saída do MPPT A
static constexpr AdcChannelRef V_MPPT_B_ADC = {1, 1};        // tensão na saída do MPPT B
static constexpr AdcChannelRef V_BAT_DIV_HV80_ADC = {1, 2};  // tensão da bateria (HV80)
static constexpr AdcChannelRef V_MPPT_C_ADC = {1, 3};        // tensão na saída do MPPT C

// -----------------------------------------------------------------------------
// Sensores de corrente dos MPPTs: ACS712-30A.
// 66 mV/A com 5 V de alimentação, zero em VCC/2 e saída proporcional ao 5 V
// (ratiométrica). O atenuador 10k/20k antes do ADS faz V_sensor = V_ADC * 1,5.
// -----------------------------------------------------------------------------
struct Acs712Calibration {
  float sensitivityVPerAAt5V;  // V/A a 5 V (datasheet: 0,066 para o modelo 30 A)
  float frontEndGain;          // ganho para desfazer o atenuador (20k+10k)/20k = 1,5
  float zeroOffsetA;           // [CALIBRAÇÃO] corrente lida com o MPPT DESLIGADO
};
// [CALIBRAÇÃO] Para cada MPPT: desligue o MPPT, anote a corrente exibida e
// coloque esse valor em zeroOffsetA. Se o módulo for o ACS712-20A (0,100 V/A)
// ou 05B (0,185 V/A), troque a sensibilidade.
static constexpr Acs712Calibration ACS712_MPPT_A = {0.066f, 1.5f, 0.0f};
static constexpr Acs712Calibration ACS712_MPPT_B = {0.066f, 1.5f, 0.0f};
static constexpr Acs712Calibration ACS712_MPPT_C = {0.066f, 1.5f, 0.0f};
// [CALIBRAÇÃO] Relação do atenuador do 5 V de referência (mesmo 10k/20k).
static constexpr float VCC5_DIVIDER_RATIO = 1.5f;
// Faixa aceitável do 5 V: fora dela as três correntes viram nan + flag.
static constexpr float VCC5_MIN_V = 4.75f;
static constexpr float VCC5_MAX_V = 5.25f;
// Quantas conversões são somadas para reduzir o ruído do ACS712 (~1,2 ms cada).
static constexpr uint8_t ACS712_AVERAGE_SAMPLES = 16;

// -----------------------------------------------------------------------------
// Divisores de tensão HV80 (até 80 V). Relação = (R_topo + R_base) / R_base.
// Montagem sugerida: 390k + 390k + 10k sobre 10k = 80,0 (MONTAGEM_HARDWARE).
// [CALIBRAÇÃO] Meça a tensão real com multímetro e a tensão no AIN; grave a
// relação medida (V_real / V_AIN) em cada constante abaixo.
// -----------------------------------------------------------------------------
static constexpr float V_BAT_DIVIDER_RATIO = 80.0f;
// [CALIBRAÇÃO] Valores 1.0 são PROVISÓRIOS: substitua pela relação medida da
// placa, senão a tensão e a potência de cada MPPT ficam erradas.
static constexpr float V_MPPT_A_DIVIDER_RATIO = 1.0f;
static constexpr float V_MPPT_B_DIVIDER_RATIO = 1.0f;
static constexpr float V_MPPT_C_DIVIDER_RATIO = 1.0f;

// -----------------------------------------------------------------------------
// INA226 da bateria: SOMENTE low-side (máx. 36 V em IN+/IN-/VBUS) com shunt
// externo de 75 mV.
//   150 A/75 mV = 0,0005 ohm (recomendado)
//   100 A/75 mV = 0,00075 ohm (limitar a corrente de bateria do VESC a 100 A)
// -----------------------------------------------------------------------------
static constexpr uint8_t INA226_BATTERY_ADDRESS = 0x40;  // [AJUSTE] A0 = A1 = GND
// [CALIBRAÇÃO] Ajuste fino: compare com alicate amperímetro em 2 correntes e
// corrija este valor (R = V_shunt / I_alicate).
static constexpr float INA226_SHUNT_OHM = 0.0005f;
static constexpr bool INA226_LOW_SIDE_ONLY = true;
// Trava de compilação: impede que alguém "libere" o uso em high-side.
static_assert(INA226_LOW_SIDE_ONLY, "INA226 must stay low-side (ADR-005)");
// [AJUSTE] +1: IN+ ligado no lado GND_SYS (descarga positiva). Use -1 se o
// chicote foi montado invertido — corrija aqui, nunca espalhando sinais no código.
static constexpr int8_t INA226_CURRENT_SIGN = 1;
// Registrador de configuração: só shunt, contínuo, 1,1 ms, média de 1024
// conversões (~1,13 s por leitura). O driver confere este valor por readback.
static constexpr uint16_t INA226_CONFIG_REGISTER = 0x4F25;

// -----------------------------------------------------------------------------
// Banco de baterias EletroDC 13s18p Li-ion e cálculo de SoC (ADR-005).
// -----------------------------------------------------------------------------
static constexpr uint8_t BATTERY_SERIES = 13;             // células em série
// [CALIBRAÇÃO] Capacidade útil (ensaio: 43,54 Ah a 45 A). Reduza com o
// envelhecimento ou após medir um ciclo completo de descarga.
static constexpr float BATTERY_CAPACITY_AH = 43.0f;
// "Cheio": tensão >= 54,0 V com |corrente| < 1 A por 60 s => SoC = 100 %.
static constexpr float BATTERY_FULL_SYNC_V = 54.0f;
static constexpr float BATTERY_FULL_SYNC_CURRENT_A = 1.0f;
static constexpr uint32_t BATTERY_FULL_SYNC_HOLD_MS = 60000;
// "Repouso": |corrente| < 0,5 A por 30 min => corrige o SoC pela tabela OCV.
static constexpr float BATTERY_REST_CURRENT_A = 0.5f;
static constexpr uint32_t BATTERY_REST_HOLD_MS = 30UL * 60UL * 1000UL;
// Intervalo de gravação do SoC no microSD (/soc_state.txt).
static constexpr uint32_t BATTERY_SOC_PERSIST_MS = 60000;

// Ponto da curva tensão em repouso (OCV) x estado de carga.
struct OcvPoint {
  float restVoltageV;  // tensão do banco em repouso (V)
  float socPct;        // estado de carga correspondente (%)
};
// [CALIBRAÇÃO] Tabela PROVISÓRIA (ensaio a 45 A + queda interna de 63 mohm).
// Substitua por uma curva medida em repouso. Mantenha a tensão estritamente
// decrescente e o SoC de 100 a 0.
static constexpr OcvPoint BATTERY_OCV_TABLE[] = {
    {54.1f, 100.0f}, {52.7f, 91.0f}, {51.6f, 83.0f}, {50.5f, 74.0f}, {49.5f, 65.0f},
    {48.6f, 57.0f}, {47.9f, 48.0f}, {47.4f, 40.0f}, {46.9f, 31.0f}, {46.4f, 22.0f},
    {45.7f, 14.0f}, {45.0f, 8.0f}, {44.1f, 5.0f}, {42.2f, 1.0f}, {40.3f, 0.0f},
};
static constexpr size_t BATTERY_OCV_POINTS = sizeof(BATTERY_OCV_TABLE) / sizeof(BATTERY_OCV_TABLE[0]);

// -----------------------------------------------------------------------------
// Motor / VESC (ADR-004, marco M9).
// [CALIBRAÇÃO] Pares de polos do Golden Motor 3 kW: confirme com tacômetro.
// Com 0 (desconhecido) o RPM fica nan. RPM = ERPM do VESC / pares de polos.
// -----------------------------------------------------------------------------
static constexpr uint8_t MOTOR_POLE_PAIRS = 0;
// Dado do VESC mais velho que isso é tratado como "stale" (nan + VESC_STALE).
static constexpr uint32_t VESC_STALE_MS = 1000;

// -----------------------------------------------------------------------------
// Temperaturas DS18B20.
// -----------------------------------------------------------------------------
// Tempo de conversão em 12 bits (750 ms no datasheet + margem).
static constexpr uint32_t TEMPERATURE_CONVERSION_MS = 800;
// [CONTRATO] O payload tem 10 posições de temperatura; mais sensores exigem
// mudar TelemetryPayloadRevB.h, a spec e os decodificadores.
static constexpr uint8_t MAX_TEMPERATURE_SENSORS = 10;

} // namespace RevB
