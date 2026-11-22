#include "PinMapRevB.h"

namespace RevB {
// UARTs por PIO: (pino TX, pino RX).
SerialPIO SerialLoRa(Pins::PIN_LORA_TX, Pins::PIN_LORA_RX);
SerialPIO SerialGps(Pins::PIN_GPS_TX, Pins::PIN_GPS_RX);
// SPI do microSD por PIO (SCK, MISO, MOSI). O CS é um GPIO comum controlado
// pela biblioteca SD (o CS por hardware do PIO exigiria CS = SCK + 1).
SoftwareSPI SdSpi(Pins::PIN_SD_SCK, Pins::PIN_SD_MISO, Pins::PIN_SD_MOSI);

void PinMapRevB::begin() {
  // [AJUSTE] Velocidades das UARTs: 115200 com a Heltec (TransmiterLoRa usa o
  // mesmo valor) e 9600 com o GPS NEO-6M (padrão de fábrica do módulo).
  SerialLoRa.begin(115200);
  SerialGps.begin(9600);

  // I2C0 dos sensores em GP20 (SDA) / GP21 (SCL).
  Wire.setSDA(Pins::PIN_I2C_SDA);
  Wire.setSCL(Pins::PIN_I2C_SCL);
  Wire.begin();

  // Nunca ligar o SPI0 de hardware aos pinos do SD: SPI.setRX(14) dá panic no
  // boot e o SPI0 é do LCD em GP0-GP5 (ADR-006). O SD usa o SdSpi (PIO).
  pinMode(Pins::PIN_SD_CS, OUTPUT);
  digitalWrite(Pins::PIN_SD_CS, HIGH);  // cartão desmarcado até o SD.begin()

  // Entradas dos fluxômetros com pull-down (sem sinal = nível baixo).
  pinMode(Pins::PIN_FLOW_A, INPUT_PULLDOWN);
  pinMode(Pins::PIN_FLOW_B, INPUT_PULLDOWN);

  // Heltec liberada (fora de reset) no boot.
  setHeltecReset(false);
}

void PinMapRevB::setHeltecReset(bool asserted) {
  // Open-drain: o pino nunca é levado a 3,3 V. Em reset ele vira saída em nível
  // baixo (liga o NMOS); liberado ele vira entrada (alta impedância).
  digitalWrite(Pins::PIN_HELTEC_RST, LOW);
  pinMode(Pins::PIN_HELTEC_RST, asserted ? OUTPUT : INPUT);
}
} // namespace RevB
