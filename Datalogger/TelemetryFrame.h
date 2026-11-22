#pragma once

#include <Arduino.h>

// =============================================================================
// Frame Rev B (docs/specs/TELEMETRY_FRAME.md):
//   SOF[2]=A5 5A | version[1] | type[1] | seq16[2] | len16[2] | payload[len] | CRC16[2]
// Campos numéricos em little-endian; CRC16-CCITT-FALSE de version até o payload.
//
// [CONTRATO] Este arquivo existe em 4 cópias (Datalogger, TransmiterLoRa,
// ReceiverLoRa, DashboardAntenna). Mudar MAX_PAYLOAD, VERSION ou os tipos exige
// mudar TODAS as cópias, o web_dash/webserver.py, o APK
// (lib/protocol/rev_b_frame.dart) e a spec — o teste de paridade em
// ReceiverLoRa/tests falha se algum ficar diferente.
// =============================================================================

namespace RevB {

// Tipos de frame. [CONTRATO] Um tipo novo precisa da spec e dos decodificadores.
enum class FrameType : uint8_t { TELEMETRY = 1, STATUS, ERROR, COMMAND, CONFIG, ACK, NACK };

struct TelemetryFrame {
  static constexpr uint8_t VERSION = 1;
  static constexpr uint8_t SOF_0 = 0xA5;
  static constexpr uint8_t SOF_1 = 0x5A;
  static constexpr uint16_t MAX_PAYLOAD = 244; // payload schema 3 (240 B) + margin; frame <= 254 B
  FrameType type = FrameType::STATUS;
  uint16_t sequence = 0;  // contador do emissor (wrap natural em 65535)
  uint16_t length = 0;    // bytes válidos em payload[]
  uint8_t payload[MAX_PAYLOAD] = {};
};

class TelemetryFrameCodec {
public:
  static constexpr size_t OVERHEAD = 10;  // SOF(2) + cabeçalho(6) + CRC(2)
  static uint16_t crc16(const uint8_t* data, size_t length);
  // Codifica o frame em output; retorna o tamanho total ou 0 se não couber.
  static size_t encode(const TelemetryFrame& frame, uint8_t* output, size_t capacity);

  // Decodificador byte a byte (máquina de estados), ressincroniza no SOF.
  class Decoder {
  public:
    // Entrega um byte; retorna true quando um frame completo e válido saiu em output.
    bool feed(uint8_t byte, TelemetryFrame& output);
    // Volta a procurar SOF (usado em timeout entre bytes ou novo pacote).
    void reset();
    uint32_t crcErrors() const { return crcErrors_; }
    uint32_t framingErrors() const { return framingErrors_; }

  private:
    enum class State : uint8_t { WAIT_SOF_0, WAIT_SOF_1, HEADER, PAYLOAD, CRC };
    State state_ = State::WAIT_SOF_0;
    uint8_t header_[6] = {};  // version, type, seq(2), len(2)
    uint8_t payload_[TelemetryFrame::MAX_PAYLOAD] = {};
    uint8_t crcBytes_[2] = {};
    uint8_t headerIndex_ = 0;
    uint16_t payloadLength_ = 0;
    uint16_t payloadIndex_ = 0;
    uint8_t crcIndex_ = 0;
    uint32_t crcErrors_ = 0;
    uint32_t framingErrors_ = 0;
    bool finish(TelemetryFrame& output);
  };
};

} // namespace RevB
