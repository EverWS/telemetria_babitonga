#pragma once
#include <Arduino.h>

namespace RevB {

// Resultado de uma amostra do fluxômetro.
struct FlowReading {
  float litersPerMinute = NAN;  // vazão (nan quando parado/inválido)
  uint32_t pulses = 0;          // total de pulsos desde o boot
  bool valid = false;           // true se a vazão foi calculada
  bool stale = true;            // true se não chega pulso há mais que o timeout
};

// Fluxômetro YF-S201 por contagem de pulsos em interrupção.
// O sinal de 5 V do sensor passa por um buffer Schmitt 3,3 V antes do GPIO.
// [NOVO SENSOR] Outro fluxômetro = outra instância + outro attachInterrupt()
// no Datalogger.ino (precisa de GPIO livre; ver pins_rev_b.h).
class FlowMeter {
public:
  void begin(uint8_t pin);
  // Chamado pela rotina de interrupção a cada borda de subida.
  void onPulse();
  // Calcula a vazão desde a amostra anterior.
  // staleTimeoutMs: sem pulso por esse tempo => vazão nan (stale), não zero.
  FlowReading sample(uint32_t nowMs, uint32_t staleTimeoutMs = 2000);

private:
  uint8_t pin_ = 255;
  volatile uint32_t pulseCount_ = 0;  // alterado na interrupção
  uint32_t lastSampleMs_ = 0, lastPulseCount_ = 0;
  volatile uint32_t lastPulseMs_ = 0;  // horário do último pulso (interrupção)
};

} // namespace RevB
