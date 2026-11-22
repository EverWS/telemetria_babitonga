#pragma once
#include <Arduino.h>
#include <ADS1X15.h>  // biblioteca "ADS1X15" (Rob Tillaart) — instalar pelo arduino-cli

namespace RevB {
// Conjunto dos 4 conversores ADS1115 (16 bits) no barramento I2C.
// Endereços fixos pelo pino ADDR: 0x48 (GND), 0x49 (VDD), 0x4A (SDA), 0x4B (SCL).
// [NOVO SENSOR] Os ADS 0x4A e 0x4B estão livres para novos canais analógicos;
// o mapa de canais fica em config/rev_b_config.h.
class Ads1115Bank {
public:
  static constexpr uint8_t DEVICE_COUNT = 4;
  Ads1115Bank();
  // Inicializa os 4 ADS; retorna false se algum não responder (conta erro).
  bool begin();
  // Lê uma entrada (device 0..3, channel AIN0..AIN3) em volts.
  // Em falha devolve false e voltage = nan (nunca 0).
  bool readVoltage(uint8_t device, uint8_t channel, float& voltage);
  // Contador acumulado de falhas (o .ino compara com o ciclo anterior).
  uint32_t errors() const { return errors_; }

private:
  ADS1115* devices_[DEVICE_COUNT];
  ADS1115 adc0_, adc1_, adc2_, adc3_;
  uint32_t errors_ = 0;
};
} // namespace RevB
