#include "BatteryIna226.h"

namespace RevB {

// Escreve um registrador de 16 bits (byte alto primeiro, conforme o datasheet).
bool BatteryIna226::writeRegister(uint8_t reg, uint16_t value) {
  if (!wire_) return false;
  wire_->beginTransmission(address_);
  wire_->write(reg);
  wire_->write(static_cast<uint8_t>(value >> 8));
  wire_->write(static_cast<uint8_t>(value & 0xFF));
  return wire_->endTransmission() == 0;
}

// Lê um registrador de 16 bits (repeated start entre escrita do ponteiro e leitura).
bool BatteryIna226::readRegister(uint8_t reg, uint16_t& value) {
  if (!wire_) return false;
  wire_->beginTransmission(address_);
  wire_->write(reg);
  if (wire_->endTransmission(false) != 0 || wire_->requestFrom(address_, 2) != 2) return false;
  value = static_cast<uint16_t>(wire_->read()) << 8;
  value |= static_cast<uint16_t>(wire_->read());
  return true;
}

bool BatteryIna226::begin(TwoWire& wire, uint8_t address) {
  wire_ = &wire;
  address_ = address;
  // Verifica se existe um dispositivo no endereço (ACK no I2C).
  wire_->beginTransmission(address_);
  if (wire_->endTransmission() != 0) {
    ++errors_;
    online_ = false;
    return false;
  }
  // Modo só shunt, contínuo, conversões de 1,1 ms e média de 1024 (ADR-005):
  // cada leitura cobre ~1,13 s, o período inteiro de aquisição de 1 s.
  // O readback confirma que o chip aceitou (o bit 15 RST sempre lê 0).
  uint16_t configured = 0;
  online_ = writeRegister(0x00, configRegister_) && readRegister(0x00, configured) &&
            configured == configRegister_;
  if (!online_) ++errors_;
  return online_;
}

bool BatteryIna226::readCurrent(float& currentA) {
  currentA = NAN;
  if (!online_) return false;
  uint16_t raw = 0;
  // Registrador 0x01 = tensão do shunt (complemento de dois).
  if (!readRegister(0x01, raw)) {
    ++errors_;
    return false;
  }
  const int16_t signedRaw = static_cast<int16_t>(raw);
  // LSB da tensão do shunt = 2,5 uV (datasheet). O sinal segue o chicote
  // (positivo = descarga).
  const auto reading = convertShuntVoltage(currentSign_ * static_cast<float>(signedRaw) * 0.0000025f);
  currentA = reading.currentA;
  if (!reading.valid) ++errors_;
  return reading.valid;
}

BatteryIna226Reading BatteryIna226::convertShuntVoltage(float shuntVoltageV) const {
  BatteryIna226Reading reading;
  if (isnan(shuntVoltageV) || isinf(shuntVoltageV) || shuntOhm_ <= 0.0f) return reading;
  // Lei de Ohm: I = V / R. [CALIBRAÇÃO] A precisão depende de INA226_SHUNT_OHM.
  reading.currentA = shuntVoltageV / shuntOhm_;
  reading.valid = !isnan(reading.currentA) && !isinf(reading.currentA);
  return reading;
}

} // namespace RevB
