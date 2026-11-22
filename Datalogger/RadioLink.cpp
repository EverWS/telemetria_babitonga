#include "RadioLink.h"

namespace RevB {

bool RadioLink::send(TelemetryFrame& frame) {
  if (!serial_ || frame.length > TelemetryFrame::MAX_PAYLOAD) {
    ++stats_.writeErrors;
    return false;
  }

  // Buffer do frame codificado (até 10 + 244 = 254 bytes).
  uint8_t encoded[TelemetryFrameCodec::OVERHEAD + TelemetryFrame::MAX_PAYLOAD] = {};
  const size_t length = TelemetryFrameCodec::encode(frame, encoded, sizeof(encoded));
  // A escrita vai para o buffer da UART; se não couber inteira, conta erro.
  if (!length || serial_->write(encoded, length) != length) {
    ++stats_.writeErrors;
    return false;
  }
  ++stats_.framesSent;
  stats_.bytesSent += length;
  return true;
}

bool RadioLink::poll(TelemetryFrame& frame) {
  if (!serial_) return false;
  while (serial_->available()) {
    if (!decoder_.feed(static_cast<uint8_t>(serial_->read()), frame)) continue;
    ++stats_.framesReceived;
    // Conta frames perdidos pelo salto de sequência (só avanços < 32768).
    if (hasLastSequence_) {
      const uint16_t delta = static_cast<uint16_t>(frame.sequence - lastSequence_);
      if (delta > 1 && delta < 0x8000) stats_.sequenceLost += delta - 1;
    }
    hasLastSequence_ = true;
    lastSequence_ = frame.sequence;
    stats_.crcErrors = decoder_.crcErrors();
    stats_.framingErrors = decoder_.framingErrors();
    return true;  // devolve um frame por chamada
  }
  stats_.crcErrors = decoder_.crcErrors();
  stats_.framingErrors = decoder_.framingErrors();
  return false;
}

} // namespace RevB
