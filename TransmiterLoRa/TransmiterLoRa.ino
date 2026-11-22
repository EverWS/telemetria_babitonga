// =============================================================================
// TransmiterLoRa Rev B — Heltec WiFi LoRa 32 V2 no barco.
//
// Modem dedicado: recebe pela UART os frames Rev B do RP2040 (Datalogger),
// valida CRC e sequência, guarda numa fila pequena e transmite cada frame como
// um pacote LoRa. Não lê sensores (ADR-002).
//
// Compilar e gravar: cd TransmiterLoRa && make compile && make upload PORT=COMx
//   [AJUSTE] PORT no Makefile. Biblioteca: Heltec ESP32 (core Heltec-esp32).
//   Antena SEMPRE conectada antes de energizar.
// Monitor serial (115200): linha "UART valid=... link=OK/STALE" a cada 5 s.
// =============================================================================
#include "Radio.h"
#include "TelemetryFrame.h"

// [AJUSTE] Precisa ser igual ao SerialLoRa.begin() do Datalogger (PinMapRevB.cpp).
static constexpr uint32_t UART_BAUD = 115200;
// Pinos da UART2 da Heltec ligados ao RP2040 (GP12 -> RX 13, TX 17 -> GP13).
static constexpr int HELTEC_TX = 17;
static constexpr int HELTEC_RX_SELECT = 13;
// Fila de frames esperando a vez no rádio (telemetria cheia é descartada).
static constexpr uint8_t QUEUE_CAPACITY = 4;
// Sem frame válido por 5 s => enlace UART "STALE" no relatório.
static constexpr uint32_t LINK_TIMEOUT_MS = 5000;
// Intervalo do relatório de diagnóstico na USB.
static constexpr uint32_t STATS_INTERVAL_MS = 5000;
// Silêncio maior que isso no meio de um frame => descarta o frame parcial.
static constexpr uint32_t UART_FRAME_GAP_MS = 50;
// Recuo de sequência até 8 = fora de ordem; maior = RP2040 reiniciou (ressincroniza).
static constexpr uint16_t SEQUENCE_REORDER_WINDOW = 8;
// Um frame de 250 B continua chegando enquanto LoRa.endPacket() bloqueia (~0,4 s).
static constexpr size_t UART_RX_BUFFER_SIZE = 1024;

// Contadores mostrados no relatório serial.
struct LinkStats {
  uint32_t validFrames = 0;      // frames com CRC correto vindos do RP2040
  uint32_t queuedFrames = 0;     // aceitos na fila
  uint32_t sentFrames = 0;       // transmitidos por LoRa
  uint32_t droppedFrames = 0;    // descartados por fila cheia
  uint32_t duplicateFrames = 0;  // mesma sequência repetida
  uint32_t outOfOrderFrames = 0; // chegaram atrasados
  uint32_t sequenceLost = 0;     // saltos de sequência (frames perdidos na UART)
  uint32_t sequenceResyncs = 0;  // reinícios do RP2040 detectados
  uint32_t txErrors = 0;         // falhas de transmissão LoRa
};

// Fila circular de frames.
static RevB::TelemetryFrame frameQueue[QUEUE_CAPACITY];
static uint8_t queueHead = 0;
static uint8_t queueTail = 0;
static uint8_t queueSize = 0;
static RevB::TelemetryFrameCodec::Decoder decoder;
static LinkStats stats;
static uint32_t lastValidFrameMs = 0;
static uint32_t lastStatsMs = 0;
static uint32_t lastUartByteMs = 0;
static bool hasLastSequence = false;
static uint16_t lastSequence = 0;

// Comando e configuração têm prioridade sobre telemetria na fila.
static bool isCritical(uint8_t type) {
  return type == 0x04 || type == 0x05; // COMMAND or CONFIG
}

// Remove o item "index" (posição relativa à cabeça) deslocando os seguintes.
static void removeQueueItem(uint8_t index) {
  for (uint8_t i = index; i + 1 < queueSize; ++i) {
    const uint8_t from = static_cast<uint8_t>((queueHead + i + 1) % QUEUE_CAPACITY);
    const uint8_t to = static_cast<uint8_t>((queueHead + i) % QUEUE_CAPACITY);
    frameQueue[to] = frameQueue[from];
  }
  queueTail = static_cast<uint8_t>((queueHead + queueSize - 1) % QUEUE_CAPACITY);
  --queueSize;
}

// Põe um frame na fila. Fila cheia: telemetria é descartada; um frame crítico
// expulsa a telemetria mais antiga.
static bool enqueue(const RevB::TelemetryFrame& frame) {
  if (queueSize == QUEUE_CAPACITY) {
    if (!isCritical(frame.type)) {
      ++stats.droppedFrames;
      return false;
    }
    int telemetryIndex = -1;
    for (uint8_t i = 0; i < queueSize; ++i) {
      const uint8_t index = static_cast<uint8_t>((queueHead + i) % QUEUE_CAPACITY);
      if (!isCritical(frameQueue[index].type)) {
        telemetryIndex = i;
        break;
      }
    }
    if (telemetryIndex < 0) {
      ++stats.droppedFrames;
      return false;
    }
    removeQueueItem(static_cast<uint8_t>(telemetryIndex));
    ++stats.droppedFrames;
  }
  frameQueue[queueTail] = frame;
  queueTail = static_cast<uint8_t>((queueTail + 1) % QUEUE_CAPACITY);
  ++queueSize;
  ++stats.queuedFrames;
  return true;
}

// Filtro de sequência (TELEMETRY_FRAME.md): descarta duplicados e atrasados,
// conta perdas e ressincroniza após reinício do RP2040.
static bool acceptSequence(uint16_t sequence) {
  if (!hasLastSequence) {
    hasLastSequence = true;
    lastSequence = sequence;
    return true;
  }
  const uint16_t delta = static_cast<uint16_t>(sequence - lastSequence);
  if (delta == 0) {
    ++stats.duplicateFrames;
    return false;
  }
  if (delta >= 0x8000) {
    // Um recuo pequeno é frame atrasado/fora de ordem. Um recuo grande indica
    // que o RP2040 reiniciou e zerou o seq16: ressincroniza em vez de descartar
    // telemetria até a sequência antiga ser alcançada de novo.
    if (static_cast<uint16_t>(lastSequence - sequence) <= SEQUENCE_REORDER_WINDOW) {
      ++stats.outOfOrderFrames;
      return false;
    }
    ++stats.sequenceResyncs;
    lastSequence = sequence;
    return true;
  }
  if (delta > 1) stats.sequenceLost += delta - 1;
  lastSequence = sequence;
  return true;
}

// Lê todos os bytes disponíveis da UART e enfileira frames válidos.
static void pollUart() {
  while (Serial2.available()) {
    // Um silêncio no meio do frame (ex.: reset do RP2040) não pode fazer o
    // decodificador engolir o frame seguinte como payload.
    const uint32_t byteMs = millis();
    if (byteMs - lastUartByteMs > UART_FRAME_GAP_MS) decoder.reset();
    lastUartByteMs = byteMs;
    RevB::TelemetryFrame frame;
    if (!decoder.feed(static_cast<uint8_t>(Serial2.read()), frame)) continue;
    ++stats.validFrames;
    lastValidFrameMs = millis();
    if (acceptSequence(frame.sequence)) enqueue(frame);
  }
}

// Recodifica o frame da cabeça da fila e envia como um pacote LoRa.
static void transmitOne() {
  if (queueSize == 0) return;
  const RevB::TelemetryFrame& frame = frameQueue[queueHead];
  uint8_t encoded[RevB::TelemetryFrameCodec::OVERHEAD + RevB::TelemetryFrame::MAX_PAYLOAD] = {};
  size_t offset = 0;
  encoded[offset++] = RevB::TelemetryFrame::SOF_0;
  encoded[offset++] = RevB::TelemetryFrame::SOF_1;
  encoded[offset++] = RevB::TelemetryFrame::VERSION;
  encoded[offset++] = frame.type;
  encoded[offset++] = static_cast<uint8_t>(frame.sequence & 0xFF);
  encoded[offset++] = static_cast<uint8_t>(frame.sequence >> 8);
  encoded[offset++] = static_cast<uint8_t>(frame.length & 0xFF);
  encoded[offset++] = static_cast<uint8_t>(frame.length >> 8);
  memcpy(encoded + offset, frame.payload, frame.length);
  offset += frame.length;
  const uint16_t crc = RevB::TelemetryFrameCodec::crc16(encoded + 2, 6 + frame.length);
  encoded[offset++] = static_cast<uint8_t>(crc & 0xFF);
  encoded[offset++] = static_cast<uint8_t>(crc >> 8);
  // Telemetria não espera ACK do receptor (ADR-002).
  if (radio.sendFrame(encoded, offset)) ++stats.sentFrames;
  else ++stats.txErrors;
  queueHead = static_cast<uint8_t>((queueHead + 1) % QUEUE_CAPACITY);
  --queueSize;
}

// Relatório de diagnóstico na USB a cada STATS_INTERVAL_MS.
static void reportStats(uint32_t nowMs) {
  if (nowMs - lastStatsMs < STATS_INTERVAL_MS) return;
  lastStatsMs = nowMs;
  Serial.printf("UART valid=%lu queued=%lu sent=%lu drop=%lu dup=%lu ooo=%lu resync=%lu lost=%lu txerr=%lu crc=%lu frameerr=%lu link=%s\n",
                stats.validFrames, stats.queuedFrames, stats.sentFrames, stats.droppedFrames,
                stats.duplicateFrames, stats.outOfOrderFrames, stats.sequenceResyncs, stats.sequenceLost, stats.txErrors,
                decoder.crcErrors(), decoder.framingErrors(),
                static_cast<int32_t>(nowMs - lastValidFrameMs) <= static_cast<int32_t>(LINK_TIMEOUT_MS) ? "OK" : "STALE");
}

void setup() {
  Serial.begin(115200);  // USB: relatório de diagnóstico
  // Buffer maior antes do begin() para não perder bytes durante a transmissão LoRa.
  Serial2.setRxBufferSize(UART_RX_BUFFER_SIZE);
  Serial2.begin(UART_BAUD, SERIAL_8N1, HELTEC_RX_SELECT, HELTEC_TX);
  radio.init();
  lastValidFrameMs = millis();
}

void loop() {
  const uint32_t nowMs = millis();
  pollUart();        // recebe do RP2040
  transmitOne();     // envia um frame por volta (bloqueia ~0,4 s durante o TX)
  reportStats(nowMs);
}
