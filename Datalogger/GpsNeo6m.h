#pragma once
#include <Arduino.h>

namespace RevB {

// Última posição conhecida do GPS. Campos sem dado = nan / 0.
struct GpsReading {
  bool valid = false;        // fix válido (RMC status 'A' ou GGA qualidade > 0)
  uint8_t fixQuality = 0;    // GGA campo 6 (0 = sem fix, 1 = GPS, 2 = DGPS)
  uint8_t satellites = 0;    // GGA campo 7
  float hdop = NAN;          // GGA campo 8 (precisão horizontal; menor = melhor)
  float latitude = NAN;      // graus decimais (negativo = Sul)
  float longitude = NAN;     // graus decimais (negativo = Oeste)
  uint32_t lastSentenceMs = 0;
  float speedKnots = NAN;    // RMC campo 7: velocidade sobre o fundo (nós)
  float courseDeg = NAN;     // RMC campo 8: rumo sobre o fundo (graus verdadeiros)
};

// Parser NMEA não bloqueante do NEO-6M (sentenças RMC e GGA).
// [NOVO SENSOR] Para usar outra sentença (ex.: VTG, GSA), acrescente um ramo
// em parseSentence() e um campo em GpsReading; se for para a telemetria,
// atualize o payload e a spec [CONTRATO].
class GpsNeo6m {
public:
  void begin(Stream& stream) { stream_ = &stream; }
  // Consome os bytes disponíveis da UART; chamar a cada loop().
  void update(uint32_t nowMs = millis());
  const GpsReading& reading() const { return reading_; }

private:
  Stream* stream_ = nullptr;
  char sentence_[96] = {};  // buffer de uma sentença NMEA (máx. 82 caracteres)
  size_t length_ = 0;
  GpsReading reading_;
  void parseSentence(uint32_t nowMs);
  static float coordinate(const char* value, const char* hemisphere);
  static char* field(char* text, uint8_t index);
  static float number(const char* value);
};

} // namespace RevB
