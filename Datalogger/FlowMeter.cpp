#include "FlowMeter.h"

namespace RevB {

void FlowMeter::begin(uint8_t pin) {
  pin_ = pin;
  pinMode(pin_, INPUT_PULLDOWN);  // sem sinal: nível baixo (sem pulsos falsos)
  lastSampleMs_ = millis();
  lastPulseMs_ = lastSampleMs_;
}

// Rotina curta (roda em interrupção): só conta e marca o horário.
void FlowMeter::onPulse() {
  ++pulseCount_;
  lastPulseMs_ = millis();
}

FlowReading FlowMeter::sample(uint32_t nowMs, uint32_t staleTimeoutMs) {
  // Copia os valores da interrupção de forma atômica.
  uint32_t pulses, lastPulseMs;
  noInterrupts();
  pulses = pulseCount_;
  lastPulseMs = lastPulseMs_;
  interrupts();

  const uint32_t elapsed = nowMs - lastSampleMs_;    // janela desta amostra (ms)
  const uint32_t delta = pulses - lastPulseCount_;   // pulsos na janela
  FlowReading reading;
  reading.pulses = pulses;
  // Um pulso pode chegar depois de nowMs ter sido lido; idade negativa = "recente".
  const int32_t pulseAgeMs = static_cast<int32_t>(nowMs - lastPulseMs);
  reading.stale = pulseAgeMs > static_cast<int32_t>(staleTimeoutMs);
  if (elapsed > 0 && !reading.stale) {
    const float hz = static_cast<float>(delta) * 1000.0f / elapsed;  // frequência dos pulsos
    // [CALIBRAÇÃO] YF-S201: Q [L/min] = f [Hz] / 7,5 (datasheet). Para outro
    // modelo de sensor, troque o 7,5 pelo fator do fabricante ou pelo medido
    // (encher um recipiente de volume conhecido e contar os pulsos).
    reading.litersPerMinute = hz / 7.5f;
    reading.valid = true;
  }
  lastSampleMs_ = nowMs;
  lastPulseCount_ = pulses;
  return reading;
}

} // namespace RevB
