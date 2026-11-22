#pragma once
#include <Arduino.h>

namespace RevB {

// Uma linha CSV por nó e por amostra (docs/specs/CSV_SCHEMA.md, schema 2).
// Campos que não se aplicam ao nó ficam nan.
// [CONTRATO] Coluna nova = mudar este struct, o cabeçalho em CsvLoggerRevB.cpp,
// a ordem de escrita, a spec CSV_SCHEMA.md e o SCHEMA_VERSION.
struct CsvRow {
  const char* node = "UNKNOWN";  // MPPT_A, MPPT_B, MPPT_C, BATTERY, MOTOR, NAV
  uint32_t timestampMs = 0;      // millis() do RP2040
  uint16_t sequence = 0;         // mesmo seq do frame enviado nesta amostra
  float voltageV = NAN, currentA = NAN, powerW = NAN, energyWh = NAN;
  float flowLpm = NAN, temperatureC = NAN;
  uint32_t validFlags = 0, errorFlags = 0;  // máscaras hex (TELEMETRY_FRAME.md)
  float socPct = NAN, rpm = NAN, speedKn = NAN, courseDeg = NAN;
};

// Escreve o CSV em qualquer Print (arquivo do microSD ou Serial USB).
class CsvLoggerRevB {
public:
  static constexpr uint8_t SCHEMA_VERSION = 2;
  // Escreve o cabeçalho; false se o destino falhou.
  bool begin(Print& sink);
  // Escreve uma linha; false se algum campo não foi gravado (cartão removido etc.).
  bool write(const CsvRow& row);
  uint32_t errors() const { return errors_; }

private:
  Print* sink_ = nullptr;
  bool headerWritten_ = false;
  uint32_t errors_ = 0;
};

} // namespace RevB
