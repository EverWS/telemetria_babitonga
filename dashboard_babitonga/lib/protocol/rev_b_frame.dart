// Codec do frame Rev B (docs/specs/TELEMETRY_FRAME.md).
//
// SOF[2] | version[1] | type[1] | seq16[2] | len16[2] | payload[len] | CRC16[2]
// Little-endian; CRC16-CCITT-FALSE (polinômio 0x1021, início 0xFFFF) calculado
// de version até o fim do payload.
//
// [CONTRATO] Mesmas constantes dos 4 firmwares (TelemetryFrame.h) e do
// web_dash/webserver.py. O teste de paridade em ReceiverLoRa/tests confere
// frameMaxPayload.
import 'dart:typed_data';

/// Tipos de frame. [CONTRATO] Tipo novo exige spec + firmwares.
class FrameType {
  static const int telemetry = 0x01;
  static const int status = 0x02;
  static const int error = 0x03;
  static const int command = 0x04;
  static const int config = 0x05;
  static const int ack = 0x06;
  static const int nack = 0x07;
  static const int vescStatus = 0x08;
}

/// Um frame já validado (CRC correto).
class RevBFrame {
  const RevBFrame(this.type, this.sequence, this.payload);

  final int type;
  final int sequence;
  final Uint8List payload;
}

const int frameSof0 = 0xA5;
const int frameSof1 = 0x5A;
const int frameVersion = 1;

/// O payload schema 3 tem 240 bytes; o limite do frame é 244 (TELEMETRY_FRAME.md).
const int frameMaxPayload = 244;
const int frameOverhead = 10; // SOF(2) + cabeçalho(6) + CRC(2)

/// CRC16-CCITT-FALSE, igual ao do firmware.
int crc16Ccitt(List<int> data) {
  var crc = 0xFFFF;
  for (final byte in data) {
    crc ^= (byte & 0xFF) << 8;
    for (var bit = 0; bit < 8; bit++) {
      crc = (crc & 0x8000) != 0 ? ((crc << 1) ^ 0x1021) & 0xFFFF : (crc << 1) & 0xFFFF;
    }
  }
  return crc;
}

/// Codifica um frame completo (usado pelo modo demo e pelos testes).
Uint8List encodeRevBFrame(int type, int sequence, List<int> payload) {
  if (payload.length > frameMaxPayload) {
    throw ArgumentError('payload exceeds $frameMaxPayload bytes');
  }
  final body = BytesBuilder()
    ..addByte(frameVersion)
    ..addByte(type & 0xFF)
    ..addByte(sequence & 0xFF)
    ..addByte((sequence >> 8) & 0xFF)
    ..addByte(payload.length & 0xFF)
    ..addByte((payload.length >> 8) & 0xFF)
    ..add(payload);
  final bodyBytes = body.toBytes();
  final crc = crc16Ccitt(bodyBytes);
  return (BytesBuilder()
        ..addByte(frameSof0)
        ..addByte(frameSof1)
        ..add(bodyBytes)
        ..addByte(crc & 0xFF)
        ..addByte((crc >> 8) & 0xFF))
      .toBytes();
}

enum _State { waitSof0, waitSof1, header, payload, crc }

/// Decodificador byte a byte, com limite de tamanho. Cada notificação BLE traz
/// frames inteiros, então quem chama zera o decodificador a cada notificação
/// (como o ReceiverLoRa faz a cada pacote LoRa): uma notificação truncada nunca
/// engole a seguinte.
class RevBFrameDecoder {
  int crcErrors = 0;
  int framingErrors = 0;

  _State _state = _State.waitSof0;
  final List<int> _header = <int>[];
  final List<int> _payload = <int>[];
  final List<int> _crc = <int>[];
  int _payloadLength = 0;

  /// Volta a procurar o SOF.
  void reset() {
    _state = _State.waitSof0;
    _header.clear();
    _payload.clear();
    _crc.clear();
    _payloadLength = 0;
  }

  /// Decodifica um bloco de bytes e devolve os frames válidos encontrados.
  List<RevBFrame> feedAll(List<int> bytes) {
    final frames = <RevBFrame>[];
    for (final byte in bytes) {
      final frame = feed(byte);
      if (frame != null) frames.add(frame);
    }
    return frames;
  }

  /// Máquina de estados: SOF0 -> SOF1 -> cabeçalho -> payload -> CRC.
  RevBFrame? feed(int value) {
    final byte = value & 0xFF;
    switch (_state) {
      case _State.waitSof0:
        if (byte == frameSof0) _state = _State.waitSof1;
        return null;
      case _State.waitSof1:
        if (byte == frameSof1) {
          _state = _State.header;
          _header.clear();
        } else {
          framingErrors++;
          _state = byte == frameSof0 ? _State.waitSof1 : _State.waitSof0;
        }
        return null;
      case _State.header:
        _header.add(byte);
        if (_header.length < 6) return null;
        final type = _header[1];
        _payloadLength = _header[4] | (_header[5] << 8);
        if (_header[0] != frameVersion || type < 1 || type > 8 || _payloadLength > frameMaxPayload) {
          framingErrors++;
          reset();
          return null;
        }
        _payload.clear();
        _state = _payloadLength == 0 ? _State.crc : _State.payload;
        return null;
      case _State.payload:
        _payload.add(byte);
        if (_payload.length == _payloadLength) _state = _State.crc;
        return null;
      case _State.crc:
        _crc.add(byte);
        if (_crc.length < 2) return null;
        final received = _crc[0] | (_crc[1] << 8);
        final expected = crc16Ccitt(<int>[..._header, ..._payload]);
        final frame = RevBFrame(_header[1], _header[2] | (_header[3] << 8), Uint8List.fromList(_payload));
        reset();
        if (received != expected) {
          crcErrors++;
          return null;
        }
        return frame;
    }
  }
}
