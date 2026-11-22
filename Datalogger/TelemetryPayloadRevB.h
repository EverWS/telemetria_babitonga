#pragma once

#include <Arduino.h>

#include "TelemetryFrame.h"

namespace RevB {

// =============================================================================
// Payload de telemetria — schema 3 (docs/specs/TELEMETRY_FRAME.md, ADR-004/005).
// 240 bytes, little-endian, sem padding (#pragma pack(1)).
// Decodificado por web_dash/webserver.py e dashboard_babitonga (APK).
//
// [CONTRATO] Mudar QUALQUER campo, ordem ou tamanho exige, no mesmo commit:
//   1. atualizar a tabela de offsets em TELEMETRY_FRAME.md;
//   2. subir SCHEMA_VERSION;
//   3. atualizar os decodificadores do web_dash e do APK (que aceitam 1, 2 e 3);
//   4. ajustar os static_assert abaixo (eles travam a compilação se o layout
//      sair da spec).
// O payload precisa caber em TelemetryFrame::MAX_PAYLOAD (244 bytes).
// [NOVO SENSOR] Para publicar um sensor novo, use um campo "reserved" existente
// (sem mudar o tamanho) ou crie um schema 4 seguindo os passos acima.
// =============================================================================
#pragma pack(push, 1)
struct TelemetryPayloadRevB {
  static constexpr uint8_t SCHEMA_VERSION = 3;
  static constexpr uint8_t CHANNEL_COUNT = 4;  // MPPT_A, MPPT_B, MPPT_C, BATTERY
  static constexpr uint8_t TEMPERATURE_COUNT = 10;

  // Índice de cada canal em channels[].
  enum ChannelIndex : uint8_t { MPPT_A = 0, MPPT_B = 1, MPPT_C = 2, BATTERY = 3 };

  // Um nó elétrico (24 bytes).
  struct Channel {
    float voltageV;
    float currentA;
    float powerW;
    float energyWh;
    float flowLpm;        // fluxo A em MPPT_A e fluxo B em MPPT_B; nan nos demais
    uint32_t validFlags;  // bits de PowerValidity + bit 8 = fluxo válido
  };

  // --- Cabeçalho (offset 0) ---
  uint8_t schemaVersion;
  uint8_t channelCount;
  uint8_t temperatureCount;  // DS18B20 encontrados no boot
  uint8_t reserved;
  uint32_t timestampMs;      // millis() do RP2040
  // --- Energia (offset 8) ---
  Channel channels[CHANNEL_COUNT];
  // --- Temperaturas (offset 104) ---
  float temperatures[TEMPERATURE_COUNT];
  // --- GPS (offset 144) ---
  uint8_t gpsValid;
  uint8_t gpsFixQuality;
  uint8_t gpsSatellites;
  uint8_t gpsReserved;
  float latitude;
  float longitude;
  float hdop;
  float speedKn;     // velocidade sobre o fundo (nós)
  float courseDeg;   // rumo sobre o fundo (graus)
  // --- IMU (offset 168) — driver ainda não ativo ---
  uint8_t imuValid;
  uint8_t imuReserved[3];
  float accelXG;
  float accelYG;
  float accelZG;
  float pitchDeg;
  float rollDeg;
  // --- Bateria / SoC (offset 192) ---
  float socPct;
  float ahFromFull;
  uint8_t socSource;        // SocSource (BatterySoc.h)
  uint8_t socReserved[3];
  // --- Motor / VESC (offset 204) — preenchido a partir do M9 ---
  uint8_t motorValid;
  uint8_t vescFault;
  uint16_t motorAgeMs;
  float motorRpm;
  float motorTempC;
  float fetTempC;
  float motorInputCurrentA;
  float motorInputVoltageV;
  float motorDutyPct;
  // --- Diagnóstico (offset 232) ---
  float supply5V;           // 5 V de referência dos ACS712
  uint32_t errorFlags;      // TelemetryErrorFlag
};
#pragma pack(pop)

// Travas de compilação do layout (devem bater com a spec).
static_assert(sizeof(TelemetryPayloadRevB) == 240, "Rev B payload schema 3 layout changed");
static_assert(sizeof(TelemetryPayloadRevB) <= TelemetryFrame::MAX_PAYLOAD,
              "Rev B payload exceeds frame capacity");
static_assert(offsetof(TelemetryPayloadRevB, temperatures) == 104, "schema 3 offset");
static_assert(offsetof(TelemetryPayloadRevB, gpsValid) == 144, "schema 3 offset");
static_assert(offsetof(TelemetryPayloadRevB, imuValid) == 168, "schema 3 offset");
static_assert(offsetof(TelemetryPayloadRevB, socPct) == 192, "schema 3 offset");
static_assert(offsetof(TelemetryPayloadRevB, motorValid) == 204, "schema 3 offset");
static_assert(offsetof(TelemetryPayloadRevB, supply5V) == 232, "schema 3 offset");
static_assert(offsetof(TelemetryPayloadRevB, errorFlags) == 236, "schema 3 offset");

// Bits de error_flags (TELEMETRY_FRAME.md).
// [CONTRATO] Bit novo = documentar na spec e tratar no web_dash/APK.
enum TelemetryErrorFlag : uint32_t {
  ERR_SD_OFFLINE = 1UL << 0,               // microSD ausente ou falhou em operação
  ERR_GPS_NO_FIX = 1UL << 1,               // GPS sem fix
  ERR_NO_TEMPERATURE = 1UL << 2,           // nenhum DS18B20 encontrado
  ERR_ADS_ERROR = 1UL << 3,                // erro NOVO de ADS1115 neste ciclo
  ERR_INA226_ERROR = 1UL << 4,             // INA226 offline ou leitura falhou
  ERR_VESC_STALE = 1UL << 5,               // sem dados do VESC há >= 1 s
  ERR_VESC_FAULT = 1UL << 6,               // VESC reportou falha
  ERR_SUPPLY_5V_OUT_OF_RANGE = 1UL << 7,   // 5 V fora de 4,75–5,25 V
  ERR_SOC_UNCALIBRATED = 1UL << 8,         // SoC estimado/sem sincronismo de cheio
};

} // namespace RevB
