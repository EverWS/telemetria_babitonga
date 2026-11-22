#include "BatterySoc.h"

#include "config/rev_b_config.h"

namespace RevB {

float BatterySoc::socFromRestVoltage(float voltageV) {
  if (isnan(voltageV) || isinf(voltageV)) return NAN;
  // Acima do primeiro ponto = 100 %; abaixo do último = 0 %.
  if (voltageV >= BATTERY_OCV_TABLE[0].restVoltageV) return 100.0f;
  const OcvPoint& last = BATTERY_OCV_TABLE[BATTERY_OCV_POINTS - 1];
  if (voltageV <= last.restVoltageV) return 0.0f;
  // Procura o trecho da tabela que contém a tensão e interpola linearmente.
  for (size_t i = 1; i < BATTERY_OCV_POINTS; ++i) {
    const OcvPoint& hi = BATTERY_OCV_TABLE[i - 1];
    const OcvPoint& lo = BATTERY_OCV_TABLE[i];
    if (voltageV >= lo.restVoltageV) {
      const float t = (voltageV - lo.restVoltageV) / (hi.restVoltageV - lo.restVoltageV);
      return lo.socPct + t * (hi.socPct - lo.socPct);
    }
  }
  return 0.0f;
}

void BatterySoc::restore(float ahFromFull) {
  // Ignora valores absurdos (arquivo corrompido ou de outra bateria).
  if (isnan(ahFromFull) || isinf(ahFromFull) || ahFromFull < -1.0f ||
      ahFromFull > BATTERY_CAPACITY_AH * 1.2f) {
    return;
  }
  state_.ahFromFull = ahFromFull;
  state_.source = SocSource::COULOMB;
  state_.uncalibrated = true;  // idade desconhecida (sem relógio de tempo real)
  refreshPercent();
}

// Converte a tensão em repouso em Ah consumidos (estimativa grosseira).
void BatterySoc::applyVoltageEstimate(float voltageV) {
  const float soc = socFromRestVoltage(voltageV);
  if (isnan(soc)) return;
  state_.ahFromFull = BATTERY_CAPACITY_AH * (1.0f - soc / 100.0f);
  state_.source = SocSource::VOLTAGE_ESTIMATE;
  state_.uncalibrated = true;
  refreshPercent();
}

// SoC [%] = 100 x (1 - Ah consumidos / capacidade), limitado a 0..100.
void BatterySoc::refreshPercent() {
  if (isnan(state_.ahFromFull)) {
    state_.socPct = NAN;
    return;
  }
  const float soc = 100.0f * (1.0f - state_.ahFromFull / BATTERY_CAPACITY_AH);
  state_.socPct = soc < 0.0f ? 0.0f : (soc > 100.0f ? 100.0f : soc);
}

const SocState& BatterySoc::update(uint32_t nowMs, float voltageV, bool voltageValid, float currentA,
                                   bool currentValid) {
  const uint32_t elapsedMs = hasLastUpdate_ ? nowMs - lastUpdateMs_ : 0;
  lastUpdateMs_ = nowMs;
  hasLastUpdate_ = true;

  // Boot sem valor salvo: começa pela tabela de tensão.
  if (!hasValue() && voltageValid) applyVoltageEstimate(voltageV);

  // Contagem de Ah: descarga (corrente positiva) aumenta o consumo.
  if (currentValid && hasValue()) {
    state_.ahFromFull += currentA * (static_cast<float>(elapsedMs) / 3600000.0f);
    if (state_.ahFromFull < 0.0f) state_.ahFromFull = 0.0f;  // não passa de "cheio"
    if (state_.source == SocSource::NONE) state_.source = SocSource::COULOMB;
  } else if (!currentValid) {
    state_.uncalibrated = true;  // lacuna na integração
  }

  // Sincronismo de cheio: tensão alta com a corrente de carga já baixa.
  const bool full = voltageValid && currentValid && voltageV >= BATTERY_FULL_SYNC_V &&
                    fabsf(currentA) < BATTERY_FULL_SYNC_CURRENT_A;
  if (full) {
    if (!fullCandidate_) {
      fullCandidate_ = true;
      fullSinceMs_ = nowMs;
    } else if (nowMs - fullSinceMs_ >= BATTERY_FULL_SYNC_HOLD_MS) {
      state_.ahFromFull = 0.0f;
      state_.source = SocSource::FULL_SYNC;
      state_.uncalibrated = false;  // único ponto que torna o SoC confiável
    }
  } else {
    fullCandidate_ = false;
  }

  // Repouso longo: a tensão se aproxima da OCV; corrige a estimativa.
  const bool resting = voltageValid && currentValid && fabsf(currentA) < BATTERY_REST_CURRENT_A;
  if (resting && !full) {
    if (!restCandidate_) {
      restCandidate_ = true;
      restSinceMs_ = nowMs;
    } else if (nowMs - restSinceMs_ >= BATTERY_REST_HOLD_MS) {
      applyVoltageEstimate(voltageV);
      restSinceMs_ = nowMs;  // no máximo uma correção por janela de repouso
    }
  } else {
    restCandidate_ = false;
  }

  // Voltou a circular corrente depois do cheio: segue contando a partir dele.
  if (state_.source == SocSource::FULL_SYNC && currentValid && fabsf(currentA) >= BATTERY_FULL_SYNC_CURRENT_A) {
    state_.source = SocSource::COULOMB;
  }
  refreshPercent();
  return state_;
}

} // namespace RevB
