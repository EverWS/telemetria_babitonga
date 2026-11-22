import 'dart:typed_data';

import 'package:dashboard_babitonga/ble/dashboard_link.dart';
import 'package:dashboard_babitonga/protocol/demo_source.dart';
import 'package:dashboard_babitonga/protocol/rev_b_frame.dart';
import 'package:dashboard_babitonga/protocol/telemetry.dart';
import 'package:flutter_test/flutter_test.dart';

Uint8List schema3({bool motorValid = true, double soc = 78.5}) => encodeSchema3Payload(
      timestampMs: 5678,
      channels: [
        [41, 10, 410, 1, 4],
        [42, 11, 462, 2, 5],
        [43, 12, 516, 3, double.nan],
        [50.5, 20, 1010, -4, double.nan],
      ],
      temperatures: [20, 21, 22],
      latitude: -26.25,
      longitude: -48.65,
      hdop: 0.9,
      satellites: 9,
      speedKn: 6.4,
      courseDeg: 123,
      socPct: soc,
      ahFromFull: 9.2,
      socSource: 1,
      motorValid: motorValid,
      rpm: 1500,
      motorTempC: 45,
      fetTempC: 38,
      motorCurrentA: 21,
      motorVoltageV: 50.4,
      dutyPct: 42,
      supply5V: 5.02,
    );

void main() {
  test('CRC16-CCITT-FALSE matches the standard check value', () {
    expect(crc16Ccitt('123456789'.codeUnits), 0x29B1);
  });

  test('frame round-trip and CRC mutation is rejected', () {
    final frame = encodeRevBFrame(FrameType.telemetry, 0x1234, [1, 2, 3]);
    final decoder = RevBFrameDecoder();
    final frames = decoder.feedAll(frame);
    expect(frames.single.sequence, 0x1234);
    expect(frames.single.payload, [1, 2, 3]);

    final broken = Uint8List.fromList(frame)..[frame.length - 1] ^= 1;
    decoder.reset();
    expect(decoder.feedAll(broken), isEmpty);
    expect(decoder.crcErrors, 1);
  });

  test('noise before SOF is ignored and oversized length is rejected', () {
    final decoder = RevBFrameDecoder();
    final frames = decoder.feedAll([0x00, 0xFF, 0xA5, ...encodeRevBFrame(1, 7, [9])]);
    expect(frames.single.sequence, 7);
    // len16 = 300 > frameMaxPayload
    decoder.feedAll([0xA5, 0x5A, 1, 1, 0, 0, 0x2C, 0x01]);
    expect(decoder.framingErrors, greaterThanOrEqualTo(1));
  });

  test('schema 3 payload is 240 bytes and decodes every block at the spec offsets', () {
    final payload = schema3();
    expect(payload.length, 240);
    final snapshot = decodeTelemetryPayload(payload)!;
    expect(snapshot.schemaVersion, 3);
    expect(snapshot.channels.map((channel) => channel.id), ['mppt_a', 'mppt_b', 'mppt_c', 'battery']);
    expect(snapshot.channel('mppt_c').currentA, 12);
    expect(snapshot.channel('battery').voltageV, closeTo(50.5, 1e-4));
    expect(snapshot.temperatures.take(4), [20, 21, 22, null]);
    expect(snapshot.gps.speedKn, closeTo(6.4, 1e-4));
    expect(snapshot.gps.courseDeg, closeTo(123, 1e-4));
    expect(snapshot.battery.socPct, closeTo(78.5, 1e-4));
    expect(snapshot.battery.source, SocSource.coulomb);
    expect(snapshot.motor.valid, isTrue);
    expect(snapshot.motor.rpm, 1500);
    expect(snapshot.motor.inputPowerW, closeTo(21 * 50.4, 1e-2));
    expect(snapshot.supply5V, closeTo(5.02, 1e-4));
  });

  test('stale motor and unknown SoC become null, never zero', () {
    final snapshot = decodeTelemetryPayload(schema3(motorValid: false, soc: double.nan))!;
    expect(snapshot.motor.valid, isFalse);
    expect(snapshot.motor.rpm, isNull);
    expect(snapshot.motor.temperatureC, isNull);
    expect(snapshot.battery.socPct, isNull);
  });

  test('schema 2 maps the battery from slot 2 and leaves MPPT C empty', () {
    final schema3Bytes = schema3();
    final payload = Uint8List(188);
    final data = ByteData.sublistView(payload);
    payload[0] = 2;
    payload[1] = 3;
    payload[2] = 1;
    data.setFloat32(56, 52.0, Endian.little); // BATTERY voltage in schema 2 slot 2
    data.setFloat32(104, 21.5, Endian.little);
    final snapshot = decodeTelemetryPayload(payload)!;
    expect(snapshot.schemaVersion, 2);
    expect(snapshot.channel('battery').voltageV, 52.0);
    expect(snapshot.channel('mppt_c').voltageV, isNull);
    expect(snapshot.temperatures.first, 21.5);
    expect(schema3Bytes.length, isNot(payload.length));
  });

  test('legacy 32-byte payload is supported without zero filling', () {
    final payload = Uint8List(32);
    final data = ByteData.sublistView(payload);
    data.setUint32(0, 10, Endian.little);
    data.setFloat32(4, 1.0, Endian.little);
    final snapshot = decodeTelemetryPayload(payload)!;
    expect(snapshot.schemaVersion, 1);
    expect(snapshot.channel('mppt_a').powerW, 1.0);
    expect(snapshot.channel('mppt_a').currentA, isNull);
  });

  test('VESC status frame 0x08 overrides the 1 Hz motor block while fresh', () {
    final link = DashboardLink();
    final vesc = ByteData(28)
      ..setUint8(0, 1)
      ..setUint8(1, 0x01)
      ..setFloat32(4, 2100, Endian.little)
      ..setFloat32(8, 51, Endian.little);
    link.injectNotification(encodeRevBFrame(FrameType.telemetry, 1, schema3()));
    expect(link.motor.rpm, 1500);
    link.injectNotification(encodeRevBFrame(FrameType.vescStatus, 2, vesc.buffer.asUint8List()));
    expect(link.motor.rpm, 2100);
    expect(link.motor.temperatureC, 51);
    link.dispose();
  });

  test('a truncated notification does not swallow the next one', () {
    final link = DashboardLink();
    final frame = encodeRevBFrame(FrameType.telemetry, 5, schema3());
    link.injectNotification(Uint8List.sublistView(frame, 0, 100));
    expect(link.snapshot, isNull);
    link.injectNotification(frame);
    expect(link.sequence, 5);
    link.dispose();
  });

  test('demo frames go through the real decoder', () {
    final source = DemoSource();
    final decoder = RevBFrameDecoder();
    final frame = decoder.feedAll(source.nextTelemetryFrame()).single;
    final snapshot = decodeTelemetryPayload(frame.payload)!;
    expect(snapshot.schemaVersion, 3);
    expect(snapshot.motor.valid, isTrue);
    expect(snapshot.gps.speedKn, greaterThan(9));
  });
}
