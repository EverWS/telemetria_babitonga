// =============================================================================
// DashboardAntenna Rev B — ESP32-C3 da LilyGO T-PicoC3.
//
// Ponte entre a UART interna (frames Rev B vindos do RP2040) e o BLE do tablet
// (APK dashboard_babitonga). Sem bloqueio: filas pequenas nos dois sentidos,
// telemetria nunca espera ACK e falha do BLE não afeta a aquisição.
// Contrato BLE: docs/specs/DASHBOARD_BLE.md.
//
// Compilar e gravar: cd DashboardAntenna && make compile && make upload PORT=COMx
//   Gravação pela USB-C da T-PicoC3 com o cabo invertido (lado B = ESP32-C3).
//   [AJUSTE] PORT no Makefile.
// Monitor serial (115200): linha "REV_B_DASH ..." a cada 5 s.
// =============================================================================
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <HardwareSerial.h>

#include "TelemetryFrame.h"

// [AJUSTE] Precisa ser igual ao Serial2.begin() do Datalogger.
static constexpr uint32_t UART_BAUD = 115200;
// UART interna da placa: C3 RX6 <- RP2040 GP8, C3 TX7 -> RP2040 GP9.
static constexpr int UART_RX_PIN = 6;
static constexpr int UART_TX_PIN = 7;
// Capacidade das filas BLE e UART (DASHBOARD_BLE.md).
static constexpr uint8_t QUEUE_CAPACITY = 4;
// Comandos críticos (comando/config/ACK/NACK) são repetidos até 3 vezes.
static constexpr uint8_t MAX_CRITICAL_ATTEMPTS = 3;
static constexpr uint32_t RETRY_INTERVAL_MS = 50;
// Heartbeat (frame 0x02) a cada 2 s para o APK saber que o C3 está vivo.
static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 2000;
static constexpr uint32_t STATS_INTERVAL_MS = 5000;
// Silêncio maior que isso no meio de um frame => descarta o frame parcial.
static constexpr uint32_t UART_FRAME_GAP_MS = 50;
// O payload schema 3 (240 B) vira um frame de 250 B (máx. 254); o notify
// precisa de MTU >= 257, então o servidor oferece o máximo do BLE (DASHBOARD_BLE.md).
static constexpr uint16_t BLE_PREFERRED_MTU = 517;
// Um frame inteiro quase enche o buffer UART padrão de 256 bytes.
static constexpr size_t UART_RX_BUFFER_SIZE = 1024;
static constexpr size_t MAX_ENCODED_FRAME = RevB::TelemetryFrameCodec::OVERHEAD +
                                             RevB::TelemetryFrame::MAX_PAYLOAD;

// [CONTRATO] UUIDs do serviço e características (iguais aos do APK em
// lib/ble/dashboard_link.dart). Mudar aqui quebra a conexão do tablet.
static constexpr auto SERVICE_UUID = "21241d37-fc68-4c30-9e72-2e23f727b4d4";
static constexpr auto TELEMETRY_UUID = "df903778-7fd2-40d3-a6ed-25f2e2d1185f";  // notify: frames 0x01 (e 0x08 no M9)
static constexpr auto CONTROL_UUID = "1f5b3aa7-2e71-4d0c-a2d3-b4f0ed1c1c20";    // write: comandos; notify: ACK/NACK
static constexpr auto HEARTBEAT_UUID = "e2f0f8a1-3c70-4df5-8b40-9ae6b08e9aa4";  // notify: estado do C3

// Item de fila com controle de retentativa.
struct QueueItem {
  RevB::TelemetryFrame frame;
  uint8_t attempts = 0;
  uint32_t nextAttemptMs = 0;
};

// Contadores do relatório "REV_B_DASH".
struct DashboardStats {
  uint32_t uartValidFrames = 0;     // frames válidos vindos do RP2040
  uint32_t uartCrcErrors = 0;
  uint32_t uartFramingErrors = 0;
  uint32_t bleInvalidCommands = 0;  // comandos do tablet rejeitados
  uint32_t bleCrcErrors = 0;
  uint32_t bleQueueDrops = 0;       // telemetria descartada por fila BLE cheia
  uint32_t uartQueueDrops = 0;      // comandos descartados por fila UART cheia
  uint32_t uartWriteErrors = 0;
  uint32_t bleNotifications = 0;    // notificações enviadas ao tablet
  uint32_t bleWriteDrops = 0;       // escritas BLE que chegaram com a caixa ocupada
};

static BLECharacteristic* telemetryCharacteristic = nullptr;
static BLECharacteristic* controlCharacteristic = nullptr;
static BLECharacteristic* heartbeatCharacteristic = nullptr;
// Fila para o tablet (bleQueue) e fila para o RP2040 (uartQueue).
static QueueItem bleQueue[QUEUE_CAPACITY];
static QueueItem uartQueue[QUEUE_CAPACITY];
static uint8_t bleHead = 0;
static uint8_t bleTail = 0;
static uint8_t bleSize = 0;
static uint8_t uartHead = 0;
static uint8_t uartTail = 0;
static uint8_t uartSize = 0;
static uint16_t localSequence = 0;  // seq16 dos frames gerados pelo C3 (heartbeat)
static uint32_t lastHeartbeatMs = 0;
static uint32_t lastStatsMs = 0;
static bool bleConnected = false;
static DashboardStats stats;
static RevB::TelemetryFrameCodec::Decoder uartDecoder;
static uint32_t lastUartByteMs = 0;

// As escritas BLE chegam na task da pilha BLE. Elas são entregues ao loop()
// por esta caixa de mensagem de uma posição, para que só o loop() mexa nas filas.
static portMUX_TYPE bleWriteMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t pendingBleWrite[MAX_ENCODED_FRAME] = {};
static size_t pendingBleWriteLength = 0;
static bool pendingBleWriteReady = false;

// Tipos com retentativa e prioridade: comando, config, ACK e NACK.
static bool isCritical(uint8_t type) {
  return type == 0x04 || type == 0x05 || type == 0x06 || type == 0x07;
}

// Codifica um frame Rev B (mesmo formato do Datalogger) em output.
static size_t encodeFrame(const RevB::TelemetryFrame& frame, uint8_t* output,
                          size_t capacity) {
  if (!output || frame.length > RevB::TelemetryFrame::MAX_PAYLOAD ||
      capacity < RevB::TelemetryFrameCodec::OVERHEAD + frame.length)
    return 0;

  size_t offset = 0;
  output[offset++] = RevB::TelemetryFrame::SOF_0;
  output[offset++] = RevB::TelemetryFrame::SOF_1;
  output[offset++] = RevB::TelemetryFrame::VERSION;
  output[offset++] = frame.type;
  output[offset++] = static_cast<uint8_t>(frame.sequence & 0xFF);
  output[offset++] = static_cast<uint8_t>(frame.sequence >> 8);
  output[offset++] = static_cast<uint8_t>(frame.length & 0xFF);
  output[offset++] = static_cast<uint8_t>(frame.length >> 8);
  memcpy(output + offset, frame.payload, frame.length);
  offset += frame.length;
  const uint16_t crc = RevB::TelemetryFrameCodec::crc16(output + 2, 6 + frame.length);
  output[offset++] = static_cast<uint8_t>(crc & 0xFF);
  output[offset++] = static_cast<uint8_t>(crc >> 8);
  return offset;
}

// Remove o item "index" (relativo à cabeça) de uma fila circular.
static void removeItem(QueueItem* queue, uint8_t& head, uint8_t& tail,
                       uint8_t& size, uint8_t index) {
  for (uint8_t i = index; i + 1 < size; ++i) {
    const uint8_t from = static_cast<uint8_t>((head + i + 1) % QUEUE_CAPACITY);
    const uint8_t to = static_cast<uint8_t>((head + i) % QUEUE_CAPACITY);
    queue[to] = queue[from];
  }
  tail = static_cast<uint8_t>((head + size - 1) % QUEUE_CAPACITY);
  --size;
}

// Fila para o tablet. Cheia: telemetria é descartada; item crítico expulsa
// a telemetria mais antiga.
static bool enqueueBle(const RevB::TelemetryFrame& frame, uint32_t nowMs) {
  if (bleSize == QUEUE_CAPACITY) {
    if (!isCritical(frame.type)) {
      ++stats.bleQueueDrops;
      return false;
    }
    int telemetryIndex = -1;
    for (uint8_t i = 0; i < bleSize; ++i) {
      const uint8_t index = static_cast<uint8_t>((bleHead + i) % QUEUE_CAPACITY);
      if (!isCritical(bleQueue[index].frame.type)) {
        telemetryIndex = i;
        break;
      }
    }
    if (telemetryIndex < 0) {
      ++stats.bleQueueDrops;
      return false;
    }
    removeItem(bleQueue, bleHead, bleTail, bleSize, static_cast<uint8_t>(telemetryIndex));
    ++stats.bleQueueDrops;
  }
  bleQueue[bleTail].frame = frame;
  bleQueue[bleTail].attempts = 0;
  bleQueue[bleTail].nextAttemptMs = nowMs;
  bleTail = static_cast<uint8_t>((bleTail + 1) % QUEUE_CAPACITY);
  ++bleSize;
  return true;
}

// Fila para o RP2040 (comandos/config vindos do tablet).
static bool enqueueUart(const RevB::TelemetryFrame& frame, uint32_t nowMs) {
  if (uartSize == QUEUE_CAPACITY) {
    ++stats.uartQueueDrops;
    return false;
  }
  uartQueue[uartTail].frame = frame;
  uartQueue[uartTail].attempts = 0;
  uartQueue[uartTail].nextAttemptMs = nowMs;
  uartTail = static_cast<uint8_t>((uartTail + 1) % QUEUE_CAPACITY);
  ++uartSize;
  return true;
}

static void popBle() {
  bleHead = static_cast<uint8_t>((bleHead + 1) % QUEUE_CAPACITY);
  --bleSize;
}

static void popUart() {
  uartHead = static_cast<uint8_t>((uartHead + 1) % QUEUE_CAPACITY);
  --uartSize;
}

// Responde ao tablet com NACK (0x07). Motivos: 0x02 inválido, 0x03 fila cheia.
static void enqueueNack(uint16_t sequence, uint8_t reason, uint32_t nowMs) {
  RevB::TelemetryFrame nack;
  nack.type = 0x07;
  nack.sequence = sequence;
  nack.length = 1;
  nack.payload[0] = reason;
  enqueueBle(nack, nowMs);
}

// Regras de aceite de comando/config (DASHBOARD_BLE.md).
static bool validateControl(const RevB::TelemetryFrame& frame) {
  if (frame.type == 0x04) return frame.length > 0;  // comando precisa de payload
  if (frame.type != 0x05 || frame.length < 2) return false;
  // Config: versão 1 do esquema e a trava de segurança low_side_only = 1 explícitas.
  return frame.payload[0] == 1 && frame.payload[1] == 1;
}

// Envia o próximo item da fila BLE como notificação (só com o tablet conectado).
static void drainBle(uint32_t nowMs) {
  if (!bleConnected || bleSize == 0 || nowMs < bleQueue[bleHead].nextAttemptMs)
    return;

  uint8_t encoded[MAX_ENCODED_FRAME] = {};
  const size_t encodedLength = encodeFrame(bleQueue[bleHead].frame, encoded, sizeof(encoded));
  if (encodedLength == 0) {
    popBle();
    return;
  }

  // Escolhe a característica pelo tipo do frame.
  BLECharacteristic* destination = telemetryCharacteristic;
  if (bleQueue[bleHead].frame.type == 0x02) destination = heartbeatCharacteristic;
  if (isCritical(bleQueue[bleHead].frame.type)) destination = controlCharacteristic;
  if (!destination) return;
  destination->setValue(encoded, encodedLength);
  destination->notify();
  ++stats.bleNotifications;

  // Telemetria sai uma vez; itens críticos repetem até MAX_CRITICAL_ATTEMPTS.
  ++bleQueue[bleHead].attempts;
  const bool retry = isCritical(bleQueue[bleHead].frame.type) &&
                     bleQueue[bleHead].attempts < MAX_CRITICAL_ATTEMPTS;
  if (retry)
    bleQueue[bleHead].nextAttemptMs = nowMs + RETRY_INTERVAL_MS;
  else
    popBle();
}

// Envia o próximo comando da fila UART ao RP2040 (sem bloquear).
static void drainUart(uint32_t nowMs) {
  if (uartSize == 0 || nowMs < uartQueue[uartHead].nextAttemptMs) return;

  uint8_t encoded[MAX_ENCODED_FRAME] = {};
  const size_t encodedLength = encodeFrame(uartQueue[uartHead].frame, encoded, sizeof(encoded));
  if (encodedLength == 0) {
    popUart();
    return;
  }
  // Só escreve se o frame inteiro couber no buffer de TX (não bloqueia).
  if (Serial1.availableForWrite() < static_cast<int>(encodedLength)) return;

  const size_t written = Serial1.write(encoded, encodedLength);
  if (written == encodedLength) {
    popUart();
    return;
  }
  ++stats.uartWriteErrors;
  ++uartQueue[uartHead].attempts;
  if (uartQueue[uartHead].attempts >= MAX_CRITICAL_ATTEMPTS)
    popUart();
  else
    uartQueue[uartHead].nextAttemptMs = nowMs + RETRY_INTERVAL_MS;
}

// Lê os frames do RP2040 e põe na fila para o tablet.
static void pollUart(uint32_t nowMs) {
  while (Serial1.available()) {
    // Um silêncio no meio do frame não pode fazer o decodificador engolir o seguinte.
    const uint32_t byteMs = millis();
    if (byteMs - lastUartByteMs > UART_FRAME_GAP_MS) uartDecoder.reset();
    lastUartByteMs = byteMs;
    RevB::TelemetryFrame frame;
    if (!uartDecoder.feed(static_cast<uint8_t>(Serial1.read()), frame)) continue;
    ++stats.uartValidFrames;
    enqueueBle(frame, nowMs);
  }
}

// Trata uma escrita do tablet (já fora da task BLE): valida e encaminha ao RP2040.
static void handleBleWrite(const uint8_t* data, size_t length, uint32_t nowMs) {
  RevB::TelemetryFrameCodec::Decoder decoder;
  bool validFrameSeen = false;
  for (size_t i = 0; i < length; ++i) {
    RevB::TelemetryFrame frame;
    if (!decoder.feed(data[i], frame)) continue;
    validFrameSeen = true;
    if (!validateControl(frame)) {
      ++stats.bleInvalidCommands;
      enqueueNack(frame.sequence, 0x02, nowMs);
      continue;
    }
    if (!enqueueUart(frame, nowMs)) enqueueNack(frame.sequence, 0x03, nowMs);
  }
  stats.bleCrcErrors += decoder.crcErrors();
  if (!validFrameSeen && decoder.crcErrors() == 0) ++stats.bleInvalidCommands;
}

// Conexão/desconexão do tablet. Ao desconectar, volta a anunciar.
class DashboardServerCallbacks final : public BLEServerCallbacks {
public:
  void onConnect(BLEServer*) override { bleConnected = true; }

  void onDisconnect(BLEServer* server) override {
    bleConnected = false;
    if (server) server->startAdvertising();
  }
};

// Escrita na característica de controle (roda na task BLE): só copia para a
// caixa de mensagem; o processamento acontece no loop().
class DashboardControlCallbacks final : public BLECharacteristicCallbacks {
public:
  void onWrite(BLECharacteristic* characteristic) override {
    const String value = characteristic->getValue();
    if (value.length() == 0) return;
    portENTER_CRITICAL(&bleWriteMux);
    if (pendingBleWriteReady || value.length() > sizeof(pendingBleWrite)) {
      ++stats.bleWriteDrops;  // caixa ocupada ou escrita grande demais
    } else {
      memcpy(pendingBleWrite, value.c_str(), value.length());
      pendingBleWriteLength = value.length();
      pendingBleWriteReady = true;
    }
    portEXIT_CRITICAL(&bleWriteMux);
  }
};

// Retira da caixa de mensagem a escrita pendente (se houver) e trata no loop().
static void processPendingBleWrite(uint32_t nowMs) {
  uint8_t data[MAX_ENCODED_FRAME];
  size_t length = 0;
  portENTER_CRITICAL(&bleWriteMux);
  if (pendingBleWriteReady) {
    length = pendingBleWriteLength;
    memcpy(data, pendingBleWrite, length);
    pendingBleWriteReady = false;
  }
  portEXIT_CRITICAL(&bleWriteMux);
  if (length > 0) handleBleWrite(data, length, nowMs);
}

// Heartbeat (0x02): [versão, tablet conectado, ocupação fila BLE, ocupação fila UART].
// [NOVO SENSOR] No M9 os contadores do barramento do VESC entram a partir do byte 4.
static void sendHeartbeat(uint32_t nowMs) {
  if (nowMs - lastHeartbeatMs < HEARTBEAT_INTERVAL_MS) return;
  lastHeartbeatMs = nowMs;
  RevB::TelemetryFrame heartbeat;
  heartbeat.type = 0x02;
  heartbeat.sequence = localSequence++;
  heartbeat.length = 4;
  heartbeat.payload[0] = 1;
  heartbeat.payload[1] = bleConnected ? 1 : 0;
  heartbeat.payload[2] = bleSize;
  heartbeat.payload[3] = uartSize;
  enqueueBle(heartbeat, nowMs);
}

// Relatório de diagnóstico na USB.
static void reportStats(uint32_t nowMs) {
  if (nowMs - lastStatsMs < STATS_INTERVAL_MS) return;
  lastStatsMs = nowMs;
  Serial.printf("REV_B_DASH uart_valid=%lu uart_crc=%lu uart_frameerr=%lu ble_bad=%lu ble_crc=%lu ble_wdrop=%lu ble_drop=%lu uart_drop=%lu uart_err=%lu notify=%lu connected=%s\n",
                static_cast<unsigned long>(stats.uartValidFrames),
                static_cast<unsigned long>(stats.uartCrcErrors + uartDecoder.crcErrors()),
                static_cast<unsigned long>(stats.uartFramingErrors + uartDecoder.framingErrors()),
                static_cast<unsigned long>(stats.bleInvalidCommands),
                static_cast<unsigned long>(stats.bleCrcErrors),
                static_cast<unsigned long>(stats.bleWriteDrops),
                static_cast<unsigned long>(stats.bleQueueDrops),
                static_cast<unsigned long>(stats.uartQueueDrops),
                static_cast<unsigned long>(stats.uartWriteErrors),
                static_cast<unsigned long>(stats.bleNotifications),
                bleConnected ? "YES" : "NO");
}

// Cria o servidor BLE, as 3 características e inicia o anúncio.
static void setupBle() {
  // [CONTRATO] Nome anunciado: o APK procura exatamente "Dashboard Babitonga".
  BLEDevice::init("Dashboard Babitonga");
  BLEDevice::setMTU(BLE_PREFERRED_MTU);
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new DashboardServerCallbacks);
  BLEService* service = server->createService(SERVICE_UUID);

  telemetryCharacteristic = service->createCharacteristic(
      TELEMETRY_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  telemetryCharacteristic->addDescriptor(new BLE2902());  // permite o tablet ativar notify

  controlCharacteristic = service->createCharacteristic(
      CONTROL_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
  controlCharacteristic->setCallbacks(new DashboardControlCallbacks);
  controlCharacteristic->addDescriptor(new BLE2902());

  heartbeatCharacteristic = service->createCharacteristic(
      HEARTBEAT_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  heartbeatCharacteristic->addDescriptor(new BLE2902());

  service->start();
  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(false);
  BLEDevice::startAdvertising();
}

void setup() {
  Serial.begin(115200);  // USB: relatório de diagnóstico
  // Buffer maior antes do begin() para receber o frame inteiro do RP2040.
  Serial1.setRxBufferSize(UART_RX_BUFFER_SIZE);
  Serial1.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  setupBle();
}

void loop() {
  const uint32_t nowMs = millis();
  pollUart(nowMs);               // RP2040 -> fila BLE
  processPendingBleWrite(nowMs); // escrita do tablet -> validação -> fila UART
  drainUart(nowMs);              // fila UART -> RP2040
  sendHeartbeat(nowMs);          // estado do C3 a cada 2 s
  drainBle(nowMs);               // fila BLE -> tablet
  reportStats(nowMs);
}
