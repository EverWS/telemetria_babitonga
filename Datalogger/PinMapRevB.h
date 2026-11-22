#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <SerialPIO.h>     // UART por PIO (core arduino-pico)
#include <SoftwareSPI.h>   // SPI por PIO (core arduino-pico)

#include "config/pins_rev_b.h"

namespace RevB {
// UART por PIO para a Heltec (115200) e para o GPS (9600).
extern SerialPIO SerialLoRa;
extern SerialPIO SerialGps;
// microSD em SPI por PIO: o GP14 não é MISO válido do SPI0 e o SPI0 pertence
// ao LCD da placa (ADR-006). Usar em SD.begin(PIN_SD_CS, SdSpi).
extern SoftwareSPI SdSpi;

// Inicialização dos barramentos e pinos da Rev B (chamar no início do setup()).
class PinMapRevB {
public:
  static void begin();
  // true = mantém a Heltec em reset; false = libera (open-drain via NMOS).
  static void setHeltecReset(bool asserted);
};
} // namespace RevB
