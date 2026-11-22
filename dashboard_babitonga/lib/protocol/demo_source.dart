// Frames de demonstração codificados exatamente como o firmware (payload
// schema 3), para o modo DEMO usar o mesmo caminho de decodificação do BLE.
import 'dart:math' as math;
import 'dart:typed_data';

import 'rev_b_frame.dart';

/// Escritor little-endian de tamanho fixo.
class _Writer {
  _Writer(int size) : _data = ByteData(size);

  final ByteData _data;
  int offset = 0;

  void u8(int value) => _data.setUint8(offset++, value);
  void u16(int value) {
    _data.setUint16(offset, value, Endian.little);
    offset += 2;
  }

  void u32(int value) {
    _data.setUint32(offset, value, Endian.little);
    offset += 4;
  }

  void f32(double value) {
    _data.setFloat32(offset, value, Endian.little);
    offset += 4;
  }

  void pad(int count) => offset += count;

  Uint8List done() {
    assert(offset == _data.lengthInBytes, 'payload size $offset != ${_data.lengthInBytes}');
    return _data.buffer.asUint8List();
  }
}

/// Codifica um payload schema 3 (240 bytes, TELEMETRY_FRAME.md). [CONTRATO]
Uint8List encodeSchema3Payload({
  required int timestampMs,
  required List<List<double>> channels, // 4 × [V, I, P, Wh, flow]
  required List<double> temperatures, // up to 10
  bool gpsValid = true,
  double latitude = double.nan,
  double longitude = double.nan,
  double hdop = double.nan,
  int satellites = 0,
  double speedKn = double.nan,
  double courseDeg = double.nan,
  double socPct = double.nan,
  double ahFromFull = double.nan,
  int socSource = 0,
  bool motorValid = false,
  double rpm = double.nan,
  double motorTempC = double.nan,
  double fetTempC = double.nan,
  double motorCurrentA = double.nan,
  double motorVoltageV = double.nan,
  double dutyPct = double.nan,
  double supply5V = double.nan,
  int errorFlags = 0,
}) {
  final w = _Writer(240)
    ..u8(3)
    ..u8(4)
    ..u8(temperatures.length.clamp(0, 10))
    ..u8(0)
    ..u32(timestampMs);
  for (final channel in channels) {
    for (final value in channel) {
      w.f32(value);
    }
    w.u32(0x0F | (channel[4].isFinite ? 1 << 8 : 0));
  }
  for (var index = 0; index < 10; index++) {
    w.f32(index < temperatures.length ? temperatures[index] : double.nan);
  }
  w
    ..u8(gpsValid ? 1 : 0)
    ..u8(gpsValid ? 1 : 0)
    ..u8(satellites)
    ..u8(0)
    ..f32(latitude)
    ..f32(longitude)
    ..f32(hdop)
    ..f32(speedKn)
    ..f32(courseDeg)
    ..u8(0)
    ..pad(3);
  for (var index = 0; index < 5; index++) {
    w.f32(double.nan);
  }
  w
    ..f32(socPct)
    ..f32(ahFromFull)
    ..u8(socSource)
    ..pad(3)
    ..u8(motorValid ? 1 : 0)
    ..u8(0)
    ..u16(motorValid ? 120 : 65535)
    ..f32(rpm)
    ..f32(motorTempC)
    ..f32(fetTempC)
    ..f32(motorCurrentA)
    ..f32(motorVoltageV)
    ..f32(dutyPct)
    ..f32(supply5V)
    ..u32(errorFlags);
  return w.done();
}

/// Gera frames de demonstração realistas (~10 nós na baía da Babitonga).
class DemoSource {
  int _tick = 0;
  int _sequence = 0;

  Uint8List nextTelemetryFrame() {
    final tick = _tick++;
    final phase = tick / 8;
    final course = tick / 120;
    final powerA = 96 + math.sin(phase) * 12;
    final powerB = 142 + math.cos(phase) * 15;
    final powerC = 120 + math.sin(phase * 0.7) * 14;
    final motorCurrent = 24 + math.sin(phase / 2) * 4;
    final solar = powerA + powerB + powerC;
    final payload = encodeSchema3Payload(
      timestampMs: tick * 1000,
      channels: [
        [48.0, powerA / 48, powerA, tick * .026, 4.2 + math.sin(phase) * .5],
        [49.0, powerB / 49, powerB, tick * .039, 5.1 + math.cos(phase * 1.2) * .4],
        [50.5, powerC / 50.5, powerC, tick * .03, double.nan],
        [50.8, motorCurrent - solar / 50.8, 50.8 * motorCurrent - solar, tick * .02, double.nan],
      ],
      temperatures: [12, 24, 50, 28, 8, 48, 26, 55, 18, 32]
          .asMap()
          .entries
          .map((entry) => entry.value + math.sin(phase + entry.key) * .6)
          .toList(),
      latitude: -26.25 + 0.006 * math.sin(course),
      longitude: -48.65 + 0.0067 * math.cos(course),
      hdop: 1.1,
      satellites: 9,
      speedKn: 10.8 + math.sin(phase) * .4,
      courseDeg: (90 - course * 180 / math.pi) % 360,
      socPct: 78 - tick * 0.01,
      ahFromFull: 9.5 + tick * 0.004,
      socSource: 1,
      motorValid: true,
      rpm: 1450 + math.sin(phase) * 120,
      motorTempC: 46 + math.sin(phase / 2) * 3,
      fetTempC: 38,
      motorCurrentA: motorCurrent,
      motorVoltageV: 50.6,
      dutyPct: 42,
      supply5V: 5.01,
    );
    return encodeRevBFrame(FrameType.telemetry, _sequence++, payload);
  }
}
