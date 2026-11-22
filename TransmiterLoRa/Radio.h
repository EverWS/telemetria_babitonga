#pragma once

#include <SPI.h>
#include <heltec.h>  // biblioteca da Heltec (traz o driver LoRa do SX1276)

// =============================================================================
// Rádio LoRa SX1276 da Heltec WiFi LoRa 32 V2 (lado do barco: transmissor).
// Pinos do rádio são internos da placa (não ligar nada neles por fora).
// =============================================================================
class Radio {
  // Barramento SPI interno do SX1276 na Heltec V2.
  static constexpr auto SCK = 5;
  static constexpr auto MISO = 19;
  static constexpr auto MOSI = 27;
  static constexpr auto SS = 18;
  static constexpr auto RST = 14;
  // [AJUSTE] Na Heltec V2 o DIO0 do SX1276 é o GPIO26. Aqui está 14 (igual ao
  // RST); não afeta o modo atual (sem interrupção de rádio), mas corrija para
  // 26 antes de usar LoRa.onReceive()/onTxDone().
  static constexpr auto DIO0 = 14;

  // [AJUSTE] Frequência: 915 MHz (faixa ISM usada no Brasil). Transmissor e
  // receptor precisam da MESMA frequência e dos mesmos parâmetros de modulação
  // (padrão da biblioteca: SF7, BW 125 kHz, CR 4/5).
  static constexpr auto BAND = 915e6;  // In Brazil we use 915 MHz for LoRa.

 public:
  // Liga o SPI, configura os pinos e inicia o rádio com CRC de pacote LoRa.
  // O segundo argumento "true" de LoRa.begin habilita o PA_BOOST da Heltec.
  auto init(void) -> bool {
    SPI.begin(SCK, MISO, MOSI, SS);
    LoRa.setPins(SS, RST, DIO0);

    return LoRa.begin(BAND, true) and (LoRa.enableCrc(), true);
  }

  // Envio de texto (legado de depuração; o firmware atual usa sendFrame()).
  template <typename... Args>
  auto send(Args... args) -> void {
    LoRa.beginPacket();
    (LoRa.print(args), ...);
    LoRa.endPacket();
  }

  // Envia um frame Rev B já codificado como UM pacote LoRa (máx. 255 bytes).
  // endPacket() bloqueia até o fim da transmissão (~0,4 s para 250 B em SF7).
  auto sendFrame(const uint8_t* frame, size_t length) -> bool {
    if (!frame || length == 0) return false;
    LoRa.beginPacket();
    const auto written = LoRa.write(frame, length);
    return written == length && LoRa.endPacket() == 1;
  }

  // Leitura em texto (legado; o transmissor não recebe pacotes no fluxo atual).
  auto read(void) -> const String& {
    static String data = "";

    data.clear();

    if (not LoRa.parsePacket())
      return data;

    while (LoRa.available())
      data += LoRa.readString();

    return data;
  }
};

// Instância única usada pelo sketch.
inline Radio radio;
