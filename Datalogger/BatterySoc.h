#pragma once
#include <Arduino.h>

namespace RevB {

// [CONTRATO] Origem do valor de SoC (vai no payload como soc_source).
enum class SocSource : uint8_t {
  NONE = 0,              // ainda sem estimativa
  COULOMB = 1,           // contagem de Ah (normal em operação)
  VOLTAGE_ESTIMATE = 2,  // estimado pela tabela tensão x carga (OCV)
  FULL_SYNC = 3          // acabou de sincronizar em 100 % (bateria cheia)
};

// Estado de carga publicado.
struct SocState {
  float socPct = NAN;       // 0–100 %; nan se desconhecido
  float ahFromFull = NAN;   // Ah consumidos desde o último "cheio"
  SocSource source = SocSource::NONE;
  bool uncalibrated = true; // true => flag SOC_UNCALIBRATED no payload
};

// Estado de carga por contagem de Ah (coulomb counting) sobre a corrente do
// INA226, sincronizado em 100 % quando a bateria enche e corrigido pela
// tensão em repouso (ADR-005). Só lógica, sem I/O: dá para testar no PC.
// Parâmetros (capacidade, limites, tabela OCV) em config/rev_b_config.h.
class BatterySoc {
public:
  // Restaura um valor salvo no microSD. Sem RTC a idade é desconhecida,
  // então fica "não calibrado" até o próximo cheio ou estimativa em repouso.
  void restore(float ahFromFull);

  // Um passo de aquisição (1 s). Corrente inválida nunca é integrada.
  const SocState& update(uint32_t nowMs, float voltageV, bool voltageValid, float currentA,
                         bool currentValid);

  const SocState& state() const { return state_; }
  bool hasValue() const { return !isnan(state_.ahFromFull); }

  // Tensão em repouso -> SoC por interpolação linear de BATTERY_OCV_TABLE.
  static float socFromRestVoltage(float voltageV);

private:
  SocState state_;
  uint32_t lastUpdateMs_ = 0;
  bool hasLastUpdate_ = false;
  uint32_t fullSinceMs_ = 0;   // desde quando a condição de "cheio" vale
  bool fullCandidate_ = false;
  uint32_t restSinceMs_ = 0;   // desde quando a bateria está em repouso
  bool restCandidate_ = false;

  void applyVoltageEstimate(float voltageV);
  void refreshPercent();
};

} // namespace RevB
