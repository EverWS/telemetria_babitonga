#include "GpsNeo6m.h"
#include <stdlib.h>

namespace RevB {

// Devolve o ponteiro para o campo "index" (separado por vírgulas) da sentença.
char* GpsNeo6m::field(char* text, uint8_t index) {
  char* cursor = text;
  for (uint8_t i = 0; i < index; ++i) {
    cursor = strchr(cursor, ',');
    if (!cursor) return nullptr;
    ++cursor;
  }
  return cursor;
}

// Campo NMEA vazio vira nan, em vez de atof("") == 0.
float GpsNeo6m::number(const char* value) {
  if (!value || *value == '\0' || *value == ',' || *value == '*') return NAN;
  return static_cast<float>(atof(value));
}

// Converte "ddmm.mmmm" + hemisfério (N/S/E/W) em graus decimais com sinal.
float GpsNeo6m::coordinate(const char* value, const char* hemisphere) {
  if (!value || !hemisphere || !*value || !*hemisphere) return NAN;
  const float raw = atof(value), degrees = floor(raw / 100.0f);
  const float result = degrees + (raw - degrees * 100.0f) / 60.0f;  // graus + minutos/60
  return (*hemisphere == 'S' || *hemisphere == 'W') ? -result : result;
}

void GpsNeo6m::parseSentence(uint32_t nowMs) {
  if (length_ < 7) return;  // curta demais para ser uma sentença válida
  sentence_[length_] = '\0';
  char* body = sentence_[0] == '$' ? sentence_ + 1 : sentence_;

  // Checksum NMEA: XOR de todos os caracteres entre '$' e '*'.
  char* checksum = strchr(body, '*');
  if (checksum) {
    uint8_t calculated = 0;
    for (char* cursor = body; cursor < checksum; ++cursor) calculated ^= static_cast<uint8_t>(*cursor);
    const uint8_t expected = static_cast<uint8_t>(strtoul(checksum + 1, nullptr, 16));
    if (calculated != expected) return;  // sentença corrompida: descarta
    *checksum = '\0';
  }

  // Identifica o tipo pelo final do primeiro campo (GPRMC, GNRMC, GPGGA...).
  char* type = field(body, 0);
  if (!type) return;
  char* typeEnd = strchr(type, ',');
  const size_t typeLength = typeEnd ? static_cast<size_t>(typeEnd - type) : strlen(type);

  if (typeLength >= 3 && strncmp(type + typeLength - 3, "RMC", 3) == 0) {
    // RMC: status, posição, velocidade e rumo.
    char* status = field(body, 2);
    char* lat = field(body, 3);
    char* latHem = field(body, 4);
    char* lon = field(body, 5);
    char* lonHem = field(body, 6);
    if (status && lat && latHem && lon && lonHem) {
      reading_.valid = *status == 'A';  // 'A' = ativo, 'V' = inválido
      reading_.latitude = coordinate(lat, latHem);
      reading_.longitude = coordinate(lon, lonHem);
      reading_.lastSentenceMs = nowMs;
      // Velocidade e rumo sobre o fundo só fazem sentido com fix ativo.
      reading_.speedKnots = reading_.valid ? number(field(body, 7)) : NAN;
      reading_.courseDeg = reading_.valid ? number(field(body, 8)) : NAN;
    }
  } else if (typeLength >= 3 && strncmp(type + typeLength - 3, "GGA", 3) == 0) {
    // GGA: qualidade do fix, satélites e HDOP.
    char* lat = field(body, 2);
    char* latHem = field(body, 3);
    char* lon = field(body, 4);
    char* lonHem = field(body, 5);
    char* quality = field(body, 6);
    char* satellites = field(body, 7);
    char* hdop = field(body, 8);
    if (lat && latHem && lon && lonHem && quality && satellites && hdop) {
      reading_.fixQuality = static_cast<uint8_t>(atoi(quality));
      reading_.satellites = static_cast<uint8_t>(atoi(satellites));
      reading_.hdop = atof(hdop);
      reading_.latitude = coordinate(lat, latHem);
      reading_.longitude = coordinate(lon, lonHem);
      reading_.valid = reading_.fixQuality > 0;
      reading_.lastSentenceMs = nowMs;
    }
  }
}

void GpsNeo6m::update(uint32_t nowMs) {
  if (!stream_) return;
  // Sem sentença há mais de 3 s: o fix deixa de valer (não reaproveita valor velho).
  if (reading_.valid && nowMs - reading_.lastSentenceMs > 3000) {
    reading_.valid = false;
    reading_.speedKnots = NAN;
    reading_.courseDeg = NAN;
  }
  // Monta as sentenças byte a byte, sem bloquear.
  while (stream_->available()) {
    const char ch = static_cast<char>(stream_->read());
    if (ch == '$') length_ = 0;  // início de uma nova sentença
    if (length_ + 1 >= sizeof(sentence_)) {
      length_ = 0;  // estouro: descarta a sentença
      continue;
    }
    sentence_[length_++] = ch;
    if (ch == '\n') {  // fim da sentença
      parseSentence(nowMs);
      length_ = 0;
    }
  }
}

} // namespace RevB
