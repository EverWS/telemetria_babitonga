#include "Ads1115Bank.h"

namespace RevB {

// Cria os 4 objetos com seus endereços I2C e monta a tabela de acesso por índice.
Ads1115Bank::Ads1115Bank() : adc0_(0x48), adc1_(0x49), adc2_(0x4A), adc3_(0x4B) {
  devices_[0] = &adc0_;
  devices_[1] = &adc1_;
  devices_[2] = &adc2_;
  devices_[3] = &adc3_;
}

bool Ads1115Bank::begin() {
  bool ok = true;
  for (auto* device : devices_) {
    if (!device->begin()) ok = false;  // ADS ausente/sem resposta
    // Taxa 7 = 860 amostras/s (~1,2 ms por conversão).
    // O ganho fica no padrão da biblioteca: fundo de escala ±6,144 V.
    // [AJUSTE] Para mais resolução em sinais pequenos use device->setGain(...),
    // lembrando que nenhum AIN pode passar de 3,3 V (alimentação do ADS).
    device->setDataRate(7);
  }
  if (!ok) ++errors_;
  return ok;
}

bool Ads1115Bank::readVoltage(uint8_t device, uint8_t channel, float& voltage) {
  voltage = NAN;  // valor inválido por padrão: falha nunca vira 0 V
  if (device >= DEVICE_COUNT || channel > 3) {
    ++errors_;
    return false;
  }
  auto* adc = devices_[device];
  adc->requestADC(channel);  // dispara uma conversão single-shot
  // Espera a conversão com limite de 100 ms para não travar a aquisição.
  const uint32_t deadline = millis() + 100;
  while (!adc->isReady()) {
    if (static_cast<int32_t>(millis() - deadline) >= 0) {
      ++errors_;  // timeout: ADS parado ou I2C com problema
      return false;
    }
    yield();  // deixa o core atender USB/serial enquanto espera
  }
  voltage = adc->toVoltage(adc->getValue());  // código bruto -> volts (conforme o ganho)
  if (isnan(voltage) || isinf(voltage)) {
    ++errors_;
    voltage = NAN;
    return false;
  }
  return true;
}

} // namespace RevB
