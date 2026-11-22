#include "Acs712Channel.h"

namespace RevB {

Acs712Reading Acs712Channel::convert(float adcVoltage, float vcc5) const {
  Acs712Reading reading;
  // Sem leitura válida do ADC ou do 5 V, não há corrente (nan, nunca 0).
  if (isnan(adcVoltage) || isinf(adcVoltage) || isnan(vcc5) || isinf(vcc5) || vcc5 <= 0.0f ||
      sensitivity_ <= 0.0f) {
    return reading;
  }
  const float sensorV = adcVoltage * frontEndGain_;          // tensão na saída do ACS712
  const float sensitivity = sensitivity_ * (vcc5 / 5.0f);    // V/A corrigido pelo VCC real
  // I = (V_saída - VCC/2) / sensibilidade - offset de zero.
  reading.currentA = (sensorV - vcc5 * 0.5f) / sensitivity - zeroOffsetA_;
  reading.valid = !isnan(reading.currentA) && !isinf(reading.currentA);
  return reading;
}

} // namespace RevB
