#include "CsvLoggerRevB.h"

namespace RevB {

bool CsvLoggerRevB::begin(Print& sink) {
  sink_ = &sink;
  // [CONTRATO] Cabeçalho do CSV schema 2 (CSV_SCHEMA.md). Mantenha a mesma
  // ordem das colunas usada em write().
  headerWritten_ = sink_->println(
      "schema_version,node,timestamp_ms,seq,voltage_v,current_a,power_w,energy_wh,flow_l_min,temp_c,"
      "valid_flags,error_flags,soc_pct,rpm,speed_kn,course_deg");
  if (!headerWritten_) ++errors_;
  return headerWritten_;
}

bool CsvLoggerRevB::write(const CsvRow& row) {
  if (!sink_ || !headerWritten_) {
    ++errors_;
    return false;
  }
  // Todo campo escreve pelo menos 1 byte; escrita de 0 bytes = destino com falha.
  bool ok = true;
  auto check = [&ok](size_t written) { if (written == 0) ok = false; };
  // Identificação da linha.
  check(sink_->print(SCHEMA_VERSION)); check(sink_->print(',')); check(sink_->print(row.node ? row.node : "UNKNOWN")); check(sink_->print(','));
  check(sink_->print(row.timestampMs)); check(sink_->print(',')); check(sink_->print(row.sequence)); check(sink_->print(','));
  // Grandezas elétricas (4 casas decimais; nan aparece como "nan").
  check(sink_->print(row.voltageV, 4)); check(sink_->print(',')); check(sink_->print(row.currentA, 4)); check(sink_->print(','));
  check(sink_->print(row.powerW, 4)); check(sink_->print(',')); check(sink_->print(row.energyWh, 4)); check(sink_->print(','));
  check(sink_->print(row.flowLpm, 4)); check(sink_->print(',')); check(sink_->print(row.temperatureC, 4)); check(sink_->print(",0x"));
  // Máscaras de validade e erro em hexadecimal.
  check(sink_->print(row.validFlags, HEX)); check(sink_->print(",0x")); check(sink_->print(row.errorFlags, HEX)); check(sink_->print(','));
  // Colunas do schema 2.
  check(sink_->print(row.socPct, 2)); check(sink_->print(',')); check(sink_->print(row.rpm, 1)); check(sink_->print(','));
  check(sink_->print(row.speedKn, 2)); check(sink_->print(',')); check(sink_->println(row.courseDeg, 1));
  if (!ok) ++errors_;
  return ok;
}

} // namespace RevB
