// =============================================================================
// ReceiverLoRa Rev B — Heltec WiFi LoRa 32 V2 em terra.
//
// Recebe os pacotes LoRa do barco, valida CRC e sequência e publica na USB
// (115200) uma linha por frame válido, lida pelo web_dash:
//   REV_B_FRAME type=1 seq=123 len=240 rssi=-94 snr=6.50 lost=0 payload=<hex>
// A cada 5 s também imprime o resumo "REV_B_RX ...".
//
// Compilar e gravar: cd ReceiverLoRa && make compile && make upload PORT=COMx
//   [AJUSTE] PORT no Makefile; a mesma porta vai em TELEMETRIA_SERIAL_PORT do
//   web_dash. Frequência do rádio em Radio.h (igual à do TransmiterLoRa).
// =============================================================================
#include "HardwareSerial.h"
#include "Radio.h"
#include "TelemetryFrame.h"

// Maior pacote aceito: frame completo (10 + 244 = 254 bytes).
static constexpr size_t MAX_PACKET_SIZE = RevB::TelemetryFrameCodec::OVERHEAD +
                                          RevB::TelemetryFrame::MAX_PAYLOAD;
// Intervalo do resumo de diagnóstico.
static constexpr uint32_t STATS_INTERVAL_MS = 5000;
// Recuo de sequência até 8 = fora de ordem; maior = o barco reiniciou.
static constexpr uint16_t SEQUENCE_REORDER_WINDOW = 8;

// Contadores do resumo "REV_B_RX".
struct ReceiverStats {
  uint32_t packets = 0;           // pacotes LoRa recebidos
  uint32_t validFrames = 0;       // frames com CRC correto
  uint32_t publishedFrames = 0;   // frames publicados na USB
  uint32_t oversizedPackets = 0;  // pacotes maiores que o buffer
  uint32_t duplicateFrames = 0;
  uint32_t outOfOrderFrames = 0;
  uint32_t sequenceLost = 0;      // frames perdidos no rádio (saltos de seq)
  uint32_t sequenceResyncs = 0;   // reinícios do barco detectados
};

static uint8_t packetBuffer[MAX_PACKET_SIZE] = {};
static RevB::TelemetryFrameCodec::Decoder decoder;
static ReceiverStats stats;
static uint32_t lastStatsMs = 0;
static bool radioReady = false;
static bool hasLastSequence = false;
static uint16_t lastSequence = 0;

// Mesmo filtro de sequência do TransmiterLoRa (TELEMETRY_FRAME.md).
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
    // Recuo pequeno = frame atrasado. Recuo grande = o RP2040 reiniciou e zerou
    // o seq16: ressincroniza em vez de descartar a telemetria por horas.
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

// Publica um frame validado na USB (formato lido por parse_receiver_line()
// no web_dash). [CONTRATO] Mudar o formato desta linha exige mudar o regex
// _RECEIVER_FRAME em web_dash/webserver.py.
static void publishFrame(const RevB::TelemetryFrame& frame, int rssi, float snr) {
  Serial.printf("REV_B_FRAME type=%u seq=%u len=%u rssi=%d snr=%.2f lost=%lu payload=",
                frame.type, frame.sequence, frame.length, rssi, snr,
                static_cast<unsigned long>(stats.sequenceLost));
  for (uint16_t i = 0; i < frame.length; ++i)
    Serial.printf("%02X", frame.payload[i]);  // payload em hexadecimal
  Serial.println();
  ++stats.publishedFrames;
}

// Verifica se chegou pacote e decodifica os frames contidos nele.
static void pollRadio() {
  size_t packetLength = 0;
  int rssi = 0;
  float snr = 0.0f;
  const int result = radio.readPacket(packetBuffer, sizeof(packetBuffer), packetLength, rssi, snr);
  if (result == 0) return;  // nenhum pacote
  ++stats.packets;
  if (result < 0) {
    ++stats.oversizedPackets;
    return;
  }

  // Cada pacote LoRa é independente: um pacote truncado não pode contaminar
  // o estado do decodificador do pacote seguinte.
  decoder.reset();
  for (size_t i = 0; i < packetLength; ++i) {
    RevB::TelemetryFrame frame;
    if (!decoder.feed(packetBuffer[i], frame)) continue;
    ++stats.validFrames;
    if (acceptSequence(frame.sequence)) publishFrame(frame, rssi, snr);
  }
}

// Resumo de diagnóstico a cada STATS_INTERVAL_MS.
static void reportStats(uint32_t nowMs) {
  if (nowMs - lastStatsMs < STATS_INTERVAL_MS) return;
  lastStatsMs = nowMs;
  Serial.printf("REV_B_RX packets=%lu valid=%lu published=%lu oversized=%lu dup=%lu ooo=%lu resync=%lu lost=%lu crc=%lu frameerr=%lu radio=%s\n",
                static_cast<unsigned long>(stats.packets),
                static_cast<unsigned long>(stats.validFrames),
                static_cast<unsigned long>(stats.publishedFrames),
                static_cast<unsigned long>(stats.oversizedPackets),
                static_cast<unsigned long>(stats.duplicateFrames),
                static_cast<unsigned long>(stats.outOfOrderFrames),
                static_cast<unsigned long>(stats.sequenceResyncs),
                static_cast<unsigned long>(stats.sequenceLost),
                static_cast<unsigned long>(decoder.crcErrors()),
                static_cast<unsigned long>(decoder.framingErrors()),
                radioReady ? "OK" : "OFFLINE");
}

void setup() {
  // [AJUSTE] 115200 precisa ser igual a TELEMETRIA_BAUD_RATE do web_dash.
  Serial.begin(115200);
  radioReady = radio.init();
  if (!radioReady) Serial.println("REV_B_RADIO_OFFLINE");  // rádio não respondeu
}

void loop() {
  if (radioReady) pollRadio();
  reportStats(millis());
}
