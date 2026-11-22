#include "TelemetryFrame.h"

namespace RevB {

// CRC16-CCITT-FALSE: polinômio 0x1021, valor inicial 0xFFFF, sem reflexão.
uint16_t TelemetryFrameCodec::crc16(const uint8_t* data, size_t length) {
  uint16_t crc = 0xFFFF;
  while (length--) {
    crc ^= static_cast<uint16_t>(*data++) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
  }
  return crc;
}

// Volta a procurar o SOF, descartando o frame parcial.
void TelemetryFrameCodec::Decoder::reset() {
  state_ = State::WAIT_SOF_0;
  headerIndex_ = 0;
  payloadLength_ = 0;
  payloadIndex_ = 0;
  crcIndex_ = 0;
}

// Confere o CRC; se correto, copia tipo, sequência e payload para output.
bool TelemetryFrameCodec::Decoder::finish(TelemetryFrame& output) {
  const uint16_t received = static_cast<uint16_t>(crc_[0]) |
                            static_cast<uint16_t>(crc_[1]) << 8;
  uint8_t crcInput[6 + TelemetryFrame::MAX_PAYLOAD] = {};
  memcpy(crcInput, header_, sizeof(header_));
  memcpy(crcInput + sizeof(header_), payload_, payloadLength_);
  if (TelemetryFrameCodec::crc16(crcInput, sizeof(header_) + payloadLength_) != received) {
    ++crcErrors_;  // corrompido: descartado, nunca repassado
    reset();
    return false;
  }
  output.type = header_[1];
  output.sequence = static_cast<uint16_t>(header_[2]) |
                    static_cast<uint16_t>(header_[3]) << 8;
  output.length = payloadLength_;
  memcpy(output.payload, payload_, payloadLength_);
  reset();
  return true;
}

// Máquina de estados: SOF0 -> SOF1 -> cabeçalho (6 B) -> payload -> CRC (2 B).
bool TelemetryFrameCodec::Decoder::feed(uint8_t byte, TelemetryFrame& output) {
  switch (state_) {
  case State::WAIT_SOF_0:
    if (byte == TelemetryFrame::SOF_0) state_ = State::WAIT_SOF_1;
    return false;
  case State::WAIT_SOF_1:
    if (byte == TelemetryFrame::SOF_1) {
      state_ = State::HEADER;
      headerIndex_ = 0;
    } else {
      // Outro 0xA5 pode ser o início verdadeiro do frame.
      state_ = byte == TelemetryFrame::SOF_0 ? State::WAIT_SOF_1 : State::WAIT_SOF_0;
      ++framingErrors_;
    }
    return false;
  case State::HEADER:
    header_[headerIndex_++] = byte;
    if (headerIndex_ != sizeof(header_)) return false;
    if (header_[0] != TelemetryFrame::VERSION) {  // versão desconhecida
      ++framingErrors_;
      reset();
      return false;
    }
    payloadLength_ = static_cast<uint16_t>(header_[4]) |
                     static_cast<uint16_t>(header_[5]) << 8;
    if (payloadLength_ > TelemetryFrame::MAX_PAYLOAD) {  // tamanho impossível
      ++framingErrors_;
      reset();
      return false;
    }
    payloadIndex_ = 0;
    crcIndex_ = 0;
    state_ = payloadLength_ == 0 ? State::CRC : State::PAYLOAD;
    return false;
  case State::PAYLOAD:
    payload_[payloadIndex_++] = byte;
    if (payloadIndex_ == payloadLength_) state_ = State::CRC;
    return false;
  case State::CRC:
    crc_[crcIndex_++] = byte;
    if (crcIndex_ != sizeof(crc_)) return false;
    return finish(output);
  }
  return false;
}

} // namespace RevB
