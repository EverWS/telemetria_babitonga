#pragma once
#include <Arduino.h>

namespace RevB {

// Resultado da conversão de um ACS712.
struct Acs712Reading {
  float currentA = NAN;  // corrente (positiva = do MPPT para o barramento)
  bool valid = false;
};

// Sensor Hall de corrente ACS712 lido por um atenuador resistivo + ADS1115.
// A saída é ratiométrica: o zero fica em VCC/2 e a sensibilidade acompanha o
// VCC, por isso a conversão usa o 5 V MEDIDO (ADR-005).
// Calibração em config/rev_b_config.h (ACS712_MPPT_A/B/C).
// [NOVO SENSOR] Outro ACS712 = nova calibração no config + nova instância no
// Datalogger.ino + canal ADS livre.
class Acs712Channel {
public:
  Acs712Channel(float sensitivityVPerAAt5V, float frontEndGain, float zeroOffsetA)
      : sensitivity_(sensitivityVPerAAt5V), frontEndGain_(frontEndGain), zeroOffsetA_(zeroOffsetA) {}

  // adcVoltage: média lida no ADS1115; vcc5: alimentação medida do sensor.
  Acs712Reading convert(float adcVoltage, float vcc5) const;

private:
  float sensitivity_;    // V/A a 5 V
  float frontEndGain_;   // desfaz o atenuador (1,5 para 10k/20k)
  float zeroOffsetA_;    // [CALIBRAÇÃO] corrente residual com o MPPT desligado
};

} // namespace RevB
