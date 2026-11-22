#pragma once
#include <Arduino.h>
#include <Wire.h>

namespace RevB {

// Resultado da conversão da tensão do shunt em corrente.
struct BatteryIna226Reading {
  float currentA = NAN;  // corrente da bateria (positiva = descarga)
  bool valid = false;
};

// Driver mínimo do INA226 medindo a corrente da bateria num shunt externo
// LOW-SIDE (entre BAT- e GND_SYS). A tensão da bateria NÃO vem daqui: vem do
// divisor HV80 no ADS1115 (o INA226 suporta no máximo 36 V).
// Parâmetros vêm de config/rev_b_config.h (INA226_SHUNT_OHM, INA226_CURRENT_SIGN,
// INA226_CONFIG_REGISTER).
class BatteryIna226 {
public:
  // currentSign: +1 quando IN+ está no lado GND_SYS (descarga positiva), -1 se invertido.
  BatteryIna226(float shuntOhm, int8_t currentSign, uint16_t configRegister)
      : shuntOhm_(shuntOhm), currentSign_(currentSign < 0 ? -1.0f : 1.0f), configRegister_(configRegister) {}
  // Detecta o chip, grava a configuração e confirma por leitura (readback).
  bool begin(TwoWire& wire, uint8_t address = 0x40);
  bool online() const { return online_; }
  // Lê a corrente atual; false + nan em caso de falha.
  bool readCurrent(float& currentA);
  // I = V_shunt / R_shunt (público para facilitar teste/calibração).
  BatteryIna226Reading convertShuntVoltage(float shuntVoltageV) const;
  bool isLowSideOnly() const { return true; }
  uint32_t errors() const { return errors_; }

private:
  float shuntOhm_;          // resistência do shunt externo (ohm)
  float currentSign_;       // +1 ou -1 conforme o chicote
  uint16_t configRegister_; // valor do registrador 0x00 (configuração)
  TwoWire* wire_ = nullptr;
  uint8_t address_ = 0x40;
  bool online_ = false;
  uint32_t errors_ = 0;
  bool writeRegister(uint8_t reg, uint16_t value);
  bool readRegister(uint8_t reg, uint16_t& value);
};

} // namespace RevB
