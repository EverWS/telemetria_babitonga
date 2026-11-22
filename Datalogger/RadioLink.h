#pragma once

#include <Arduino.h>

#include "TelemetryFrame.h"

namespace RevB {

// Contadores de diagnóstico de um enlace UART com frames Rev B.
struct RadioLinkStats {
  uint32_t framesSent = 0;      // frames escritos com sucesso
  uint32_t bytesSent = 0;       // bytes escritos (inclui SOF, cabeçalho e CRC)
  uint32_t writeErrors = 0;     // falhas de escrita/codificação
  uint32_t framesReceived = 0;  // frames válidos recebidos (poll)
  uint32_t sequenceLost = 0;    // saltos de sequência na recepção
  uint32_t crcErrors = 0;       // frames descartados por CRC
  uint32_t framingErrors = 0;   // bytes/cabeçalhos fora do formato
};

// Enlace UART com frames Rev B. Uma instância por destino:
//   radioLink -> Heltec (LoRa)      c3Link -> ESP32-C3 (BLE)
// [NOVO SENSOR] Um novo destino serial (ex.: outro rádio) = nova instância
// com begin(<Stream>) e uma chamada send() em sendTelemetryFrame().
class RadioLink {
public:
  void begin(Stream& serial) { serial_ = &serial; }
  // Codifica e escreve um frame inteiro; telemetria não espera ACK.
  bool send(TelemetryFrame& frame);
  // Lê bytes disponíveis; true quando um frame válido completo chegou.
  // (Reservado para comandos vindos do C3/Heltec e para o frame 0x08 do M9.)
  bool poll(TelemetryFrame& frame);
  const RadioLinkStats& stats() const { return stats_; }

private:
  Stream* serial_ = nullptr;
  TelemetryFrameCodec::Decoder decoder_;
  RadioLinkStats stats_;
  uint32_t observedCrcErrors_ = 0;
  uint32_t observedFramingErrors_ = 0;
  bool hasLastSequence_ = false;
  uint16_t lastSequence_ = 0;
};

} // namespace RevB
