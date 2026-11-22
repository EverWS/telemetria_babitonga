#pragma once

// =============================================================================
// Pinagem única do RP2040 (LilyGO T-PicoC3) — Rev B / Opção A.
//
// Fonte de verdade: docs/specs/REV_B_PINMAP.md. Este arquivo e a spec mudam
// JUNTOS (regra "spec antes de código"); os testes em
// Datalogger/tests/test_rev_b_contracts.py conferem cada número.
//
// [AJUSTE] Só altere um pino depois de atualizar a spec/ADR e o esquema da
// PCB. Pinos internos da placa (não usar externamente): GP0–GP5 (LCD),
// GP6/GP7 (botões), GP8/GP9 (UART com o ESP32-C3), GP25 (LED), GP26
// (detecção de bateria da placa).
// =============================================================================

#include <Arduino.h>

namespace RevB {
namespace Pins {
// --- UART para a Heltec (TransmiterLoRa). Implementada por PIO (SerialPIO),
//     pois a UART0 de hardware não pode atender LoRa e GPS ao mesmo tempo.
static constexpr uint8_t PIN_LORA_TX = 12;  // RP2040 TX -> Heltec GPIO13 (RX), via 47 ohm
static constexpr uint8_t PIN_LORA_RX = 13;  // RP2040 RX <- Heltec GPIO17 (TX), via 47 ohm

// --- UART do GPS NEO-6M (9600 bps, NMEA), também por PIO.
static constexpr uint8_t PIN_GPS_TX = 17;   // RP2040 TX -> RX do GPS (opcional)
static constexpr uint8_t PIN_GPS_RX = 16;   // RP2040 RX <- TX do GPS

// --- I2C dos sensores (Wire / I2C0): 4x ADS1115, INA226 e IMU LSM6DS3.
//     [AJUSTE] Apenas UM par de pull-ups de 4,7 kohm no barramento inteiro.
static constexpr uint8_t PIN_I2C_SDA = 20;
static constexpr uint8_t PIN_I2C_SCL = 21;

// --- microSD por SPI em PIO (SoftwareSPI, ADR-006). O GP14 não é MISO válido
//     do SPI0 de hardware; nunca configure "SPI" com estes pinos.
static constexpr uint8_t PIN_SD_MISO = 14;
static constexpr uint8_t PIN_SD_CS = 15;    // pull-up 10 kohm: cartão inativo no boot
static constexpr uint8_t PIN_SD_SCK = 18;
static constexpr uint8_t PIN_SD_MOSI = 19;

// --- Barramento OneWire dos DS18B20 (até 10 sensores, 47 ohm série + 4,7 kohm).
static constexpr uint8_t PIN_ONEWIRE = 27;

// --- Fluxômetros YF-S201 (sinal 5 V SEMPRE via buffer Schmitt 3,3 V).
//     [NOVO SENSOR] Um terceiro fluxômetro precisaria de um GPIO livre — hoje
//     só resta a GPIO29 (ver REV_B_PINMAP.md antes de usar).
static constexpr uint8_t PIN_FLOW_A = 23;
static constexpr uint8_t PIN_FLOW_B = 24;

// --- Reset da Heltec: gate de um NMOS (open-drain). Nunca empurrar 3,3 V no EN.
static constexpr uint8_t PIN_HELTEC_RST = 28;

// --- Reserva: entrada ADC3 livre (test point). Não é pino de CAN.
static constexpr uint8_t PIN_AUX_ADC3_SPARE = 29;

// --- UART interna RP2040 <-> ESP32-C3 (trilhas da própria T-PicoC3).
static constexpr uint8_t PIN_C3_UART_TX = 8;   // -> C3 GPIO6 (RX)
static constexpr uint8_t PIN_C3_UART_RX = 9;   // <- C3 GPIO7 (TX)

// --- LCD ST7789V 1,14" da placa, no SPI0 de hardware (ligação de fábrica).
//     Reservado para o display local; ainda não é desenhado pelo firmware.
static constexpr uint8_t PIN_LCD_RST = 0;
static constexpr uint8_t PIN_LCD_DC = 1;
static constexpr uint8_t PIN_LCD_SCLK = 2;
static constexpr uint8_t PIN_LCD_MOSI = 3;
static constexpr uint8_t PIN_LCD_BL = 4;
static constexpr uint8_t PIN_LCD_CS = 5;
} // namespace Pins
} // namespace RevB
