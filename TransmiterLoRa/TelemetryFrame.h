#pragma once

#include <Arduino.h>

// =============================================================================
// Frame Rev B (docs/specs/TELEMETRY_FRAME.md):
//   SOF[2]=A5 5A | version[1] | type[1] | seq16[2] | len16[2] | payload[len] | CRC16[2]
// Little-endian; CRC16-CCITT-FALSE calculado de version até o fim do payload.
//
// [CONTRATO] Cópia idêntica em TransmiterLoRa, ReceiverLoRa e DashboardAntenna
// (a do Datalogger tem também encode() e FrameType). Ao mudar MAX_PAYLOAD,
// VERSION ou o formato, mude as 4 cópias, o web_dash, o APK e a spec. O teste
// de paridade em ReceiverLoRa/tests falha se algum valor divergir.
// =============================================================================

namespace RevB {

struct TelemetryFrame {
  static constexpr uint8_t VERSION = 1;
  static constexpr uint8_t SOF_0 = 0xA5;
  static constexpr uint8_t SOF_1 = 0x5A;
  static constexpr uint16_t MAX_PAYLOAD = 244; // payload schema 3 (240 B) + margin; frame <= 254 B

  uint8_t type = 0x02;     // 0x01 telemetria, 0x02 status, ... 0x08 VESC (spec)
  uint16_t sequence = 0;   // seq16 do emissor
  uint16_t length = 0;     // bytes válidos em payload[]
  uint8_t payload[MAX_PAYLOAD] = {};
};

class TelemetryFrameCodec {
public:
  static constexpr size_t OVERHEAD = 10;  // SOF(2) + cabeçalho(6) + CRC(2)
  static uint16_t crc16(const uint8_t* data, size_t length);

  // Decodificador byte a byte; ressincroniza ao encontrar um novo SOF.
  class Decoder {
  public:
    // true quando um frame completo e com CRC correto saiu em output.
    bool feed(uint8_t byte, TelemetryFrame& output);
    uint32_t crcErrors() const { return crcErrors_; }
    uint32_t framingErrors() const { return framingErrors_; }
    // Descarta o frame parcial (timeout entre bytes ou novo pacote LoRa).
    void reset();

  private:
    enum class State : uint8_t { WAIT_SOF_0, WAIT_SOF_1, HEADER, PAYLOAD, CRC };
    State state_ = State::WAIT_SOF_0;
    uint8_t header_[6] = {};  // version, type, seq(2), len(2)
    uint8_t payload_[TelemetryFrame::MAX_PAYLOAD] = {};
    uint8_t crc_[2] = {};
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
