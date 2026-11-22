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

  // [AJUSTE] Parâmetros do enlace LoRa (ADR-007). Fixados aqui para NÃO
  // depender dos padrões da biblioteca Heltec (que usava SF11 = ~4,1 s por
  // frame). [CONTRATO] Precisam ser IDÊNTICOS no ReceiverLoRa/Radio.h; o teste
  // de paridade em ReceiverLoRa/tests confere. Tempo no ar com estes valores:
  // ~0,39 s por frame de 250 B, o que permite telemetria a 1 Hz.
  static constexpr auto BAND = 915e6;              // frequência central: 915,0 MHz (Brasil)
  static constexpr int SPREADING_FACTOR = 7;       // SF7: menor tempo no ar, menor alcance
  static constexpr long SIGNAL_BANDWIDTH = 125E3;  // 125 kHz
  static constexpr int CODING_RATE_DENOMINATOR = 5;  // CR 4/5
  static constexpr int TX_POWER_DBM = 17;          // PA_BOOST; o modo +20 dBm não é usado
  static constexpr long PREAMBLE_SYMBOLS = 8;
  static constexpr int SYNC_WORD = 0x34;

 public:
  // Liga o SPI, configura os pinos e inicia o rádio com os parâmetros acima.
  // O segundo argumento "true" de LoRa.begin habilita o PA_BOOST da Heltec.
  auto init(void) -> bool {
    SPI.begin(SCK, MISO, MOSI, SS);
    LoRa.setPins(SS, RST, DIO0);

    if (not LoRa.begin(BAND, true)) return false;
    applyLinkParameters();
    return true;
  }

  // Aplica todos os parâmetros do enlace (sobrescreve os padrões da biblioteca).
  static void applyLinkParameters() {
    LoRa.setSpreadingFactor(SPREADING_FACTOR);
    LoRa.setSignalBandwidth(SIGNAL_BANDWIDTH);
    LoRa.setCodingRate4(CODING_RATE_DENOMINATOR);
    LoRa.setTxPower(TX_POWER_DBM, RF_PACONFIG_PASELECT_PABOOST);
    LoRa.setPreambleLength(PREAMBLE_SYMBOLS);
    LoRa.setSyncWord(SYNC_WORD);
    LoRa.disableLowDataRate();  // símbolo de 1 ms em SF7: LDRO desligado
    LoRa.enableCrc();           // CRC do pacote LoRa (além do CRC16 do frame)
  }

  // Envio de texto (legado de depuração; o firmware atual usa sendFrame()).
  template <typename... Args>
  auto send(Args... args) -> void {
    LoRa.beginPacket();
    (LoRa.print(args), ...);
    LoRa.endPacket();
  }

  // Envia um frame Rev B já codificado como UM pacote LoRa (máx. 255 bytes).
  // endPacket() bloqueia até o fim da transmissão (~0,39 s para 250 B em SF7/BW125).
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
