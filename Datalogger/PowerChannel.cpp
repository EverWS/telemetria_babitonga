#include "PowerChannel.h"

namespace RevB {

PowerReading PowerChannel::update(float voltageV, bool voltageValid, float currentA,
                                  bool currentValid, uint32_t elapsedMs) {
  PowerReading reading;
  // Valor inválido é publicado como nan, nunca como 0.
  reading.voltageV = voltageValid ? voltageV : NAN;
  reading.currentA = currentValid ? currentA : NAN;
  if (voltageValid) reading.validFlags |= POWER_VOLTAGE_VALID;
  if (currentValid) reading.validFlags |= POWER_CURRENT_VALID;

  // Potência e energia só existem quando V e I são válidos e finitos.
  if (voltageValid && currentValid && !isnan(voltageV) && !isnan(currentA) &&
      !isinf(voltageV) && !isinf(currentA)) {
    reading.powerW = voltageV * currentA;  // P [W] = V [V] x I [A]
    reading.validFlags |= POWER_POWER_VALID;
    // Integração retangular: E [Wh] += P [W] x dt [h]
    energyWh_ += reading.powerW * (static_cast<float>(elapsedMs) / 3600000.0f);
    reading.energyWh = energyWh_;
    reading.validFlags |= POWER_ENERGY_VALID;
  }
  return reading;
}

} // namespace RevB
