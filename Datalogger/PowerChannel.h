#pragma once
#include <Arduino.h>

namespace RevB {

// Bits de validade de um canal de potência (vão para valid_flags no payload/CSV).
// [CONTRATO] Os bits são documentados em TELEMETRY_FRAME.md; o bit 8 (fluxo) é
// acrescentado pelo Datalogger.ino.
enum PowerValidity : uint8_t {
  POWER_VOLTAGE_VALID = 1 << 0,  // tensão medida com sucesso
  POWER_CURRENT_VALID = 1 << 1,  // corrente medida com sucesso
  POWER_POWER_VALID = 1 << 2,    // potência calculada (V e I válidos no mesmo nó)
  POWER_ENERGY_VALID = 1 << 3    // energia acumulada atualizada
};

// Leitura de um nó elétrico (MPPT A/B/C ou bateria). Campos inválidos = nan.
struct PowerReading {
  float voltageV = NAN;
  float currentA = NAN;
  float powerW = NAN;
  float energyWh = NAN;
  uint8_t validFlags = 0;
};

// Calcula P = V x I e integra a energia (Wh) de UM nó elétrico.
// Regra da Rev B: só calcula potência se V e I são do mesmo nó e ambos válidos.
// [NOVO SENSOR] Um novo ponto de medição de potência = uma nova instância de
// PowerChannel no Datalogger.ino.
class PowerChannel {
public:
  // elapsedMs: tempo desde a última amostra (para integrar a energia).
  PowerReading update(float voltageV, bool voltageValid, float currentA,
                      bool currentValid, uint32_t elapsedMs);

private:
  float energyWh_ = 0.0f;  // energia acumulada desde o boot (não persiste)
};

} // namespace RevB
