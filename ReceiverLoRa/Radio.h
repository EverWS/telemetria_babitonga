#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <heltec.h>  // biblioteca da Heltec (traz o driver LoRa do SX1276)

// =============================================================================
// Rádio LoRa SX1276 da Heltec WiFi LoRa 32 V2 (lado de terra: receptor).
// Pinos do rádio são internos da placa (não ligar nada neles por fora).
// =============================================================================
class Radio {
private:
	// Barramento SPI interno do SX1276 na Heltec V2.
	static constexpr auto SCK = 5;
	static constexpr auto MISO = 19;
	static constexpr auto MOSI = 27;
	static constexpr auto SS = 18;
	static constexpr auto RST = 14;
	// [AJUSTE] Na Heltec V2 o DIO0 do SX1276 é o GPIO26. Aqui está 14 (igual ao
	// RST); não afeta o modo atual por polling (parsePacket), mas corrija para
	// 26 antes de usar LoRa.onReceive().
	static constexpr auto DIO0 = 14;

	// [AJUSTE] Frequência: 915 MHz (Brasil). Igual à do TransmiterLoRa.
	static constexpr auto BAND = 915e6; // In Brazil we use 915 MHz for LoRa.

public:
	auto init(void) -> bool {
		SPI.begin(SCK, MISO, MOSI, SS);
		LoRa.setPins(SS, RST, DIO0);
		// FIXME: This is not really working, but it should make this
		// program event-driven, meaning the main loop would not be
		// constantly reading. didn't work, leave it here for future
		// improvements.
		// (Modo por interrupção ainda não funciona — ver o [AJUSTE] do DIO0 acima.)
		// LoRa.onReceive(dataReceived);

		if (not LoRa.begin(BAND, true)) return false;  // true = PA_BOOST da Heltec

		LoRa.enableCrc();  // descarta pacotes LoRa corrompidos no próprio rádio
		LoRa.receive();
		return true;
	}

	// Lê um pacote LoRa recebido para buffer (polling, não bloqueante).
	// Retorna 0 quando não há pacote, 1 quando leu e -1 quando o pacote é
	// maior que o buffer (descartado).
	auto readPacket(uint8_t* buffer, size_t capacity, size_t& length,
	                int& rssi, float& snr) -> int {
		const int packetSize = LoRa.parsePacket();
		if (packetSize <= 0) return 0;

		// Qualidade do enlace deste pacote (vai junto na publicação).
		rssi = LoRa.packetRssi();  // dBm (mais perto de 0 = sinal mais forte)
		snr = LoRa.packetSnr();    // dB  (positivo = acima do ruído)
		length = 0;
		if (static_cast<size_t>(packetSize) > capacity) {
			while (LoRa.available()) (void)LoRa.read();  // esvazia o FIFO
			LoRa.receive();
			return -1;
		}

		while (LoRa.available() && length < capacity)
			buffer[length++] = static_cast<uint8_t>(LoRa.read());
		LoRa.receive();  // volta a escutar
		return 1;
	}
};

// Instância única usada pelo sketch.
inline Radio radio;
