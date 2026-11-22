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

// Monta o frame completo em output (little-endian). Retorna 0 se não couber.
size_t TelemetryFrameCodec::encode(const TelemetryFrame& frame, uint8_t* output, size_t capacity) {
  if (frame.length > TelemetryFrame::MAX_PAYLOAD || capacity < OVERHEAD + frame.length) return 0;
  size_t i = 0;
  output[i++] = TelemetryFrame::SOF_0;                              // início de frame
  output[i++] = TelemetryFrame::SOF_1;
  output[i++] = TelemetryFrame::VERSION;                            // versão do frame
  output[i++] = static_cast<uint8_t>(frame.type);                   // tipo
  output[i++] = static_cast<uint8_t>(frame.sequence & 0xFF);        // seq16 (LSB)
  output[i++] = static_cast<uint8_t>(frame.sequence >> 8);          // seq16 (MSB)
  output[i++] = static_cast<uint8_t>(frame.length & 0xFF);          // len16 (LSB)
  output[i++] = static_cast<uint8_t>(frame.length >> 8);            // len16 (MSB)
  memcpy(output + i, frame.payload, frame.length);                  // payload
  i += frame.length;
  const uint16_t crc = crc16(output + 2, 6 + frame.length);         // CRC de version..payload
  output[i++] = static_cast<uint8_t>(crc & 0xFF);
  output[i++] = static_cast<uint8_t>(crc >> 8);
  return i;
}

// Descarta o frame em andamento e volta a procurar o SOF.
void TelemetryFrameCodec::Decoder::reset() {
  state_ = State::WAIT_SOF_0;
  headerIndex_ = 0;
  payloadLength_ = 0;
  payloadIndex_ = 0;
  crcIndex_ = 0;
}

// Confere o CRC do frame recebido e, se ok, copia para output.
bool TelemetryFrameCodec::Decoder::finish(TelemetryFrame& output) {
  const uint16_t received = static_cast<uint16_t>(crcBytes_[0]) | static_cast<uint16_t>(crcBytes_[1]) << 8;
  uint8_t crcInput[6 + TelemetryFrame::MAX_PAYLOAD] = {};
  memcpy(crcInput, header_, sizeof(header_));
  memcpy(crcInput + sizeof(header_), payload_, payloadLength_);
  if (TelemetryFrameCodec::crc16(crcInput, sizeof(header_) + payloadLength_) != received) {
    ++crcErrors_;  // frame corrompido: descartado, nunca publicado
    reset();
    return false;
  }
  output.type = static_cast<FrameType>(header_[1]);
  output.sequence = static_cast<uint16_t>(header_[2]) | static_cast<uint16_t>(header_[3]) << 8;
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
      // Um novo 0xA5 pode ser o início real do frame.
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
    payloadLength_ = static_cast<uint16_t>(header_[4]) | static_cast<uint16_t>(header_[5]) << 8;
    if (payloadLength_ > TelemetryFrame::MAX_PAYLOAD) {  // tamanho impossível
      ++framingErrors_;
      reset();
      return false;
    }
    payloadIndex_ = 0;
    state_ = payloadLength_ == 0 ? State::CRC : State::PAYLOAD;
    return false;
  case State::PAYLOAD:
    payload_[payloadIndex_++] = byte;
    if (payloadIndex_ == payloadLength_) {
      state_ = State::CRC;
      crcIndex_ = 0;
    }
    return false;
  case State::CRC:
    crcBytes_[crcIndex_++] = byte;
    if (crcIndex_ != 2) return false;
    return finish(output);
  }
  return false;
}

} // namespace RevB
