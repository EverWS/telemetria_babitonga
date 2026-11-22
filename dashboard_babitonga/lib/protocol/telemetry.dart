// Decodificação do payload de telemetria (docs/specs/TELEMETRY_FRAME.md).
//
// Os schemas 1, 2 e 3 viram um único modelo. Valores que o schema não traz, ou
// que chegam como nan/inf, viram `null` — nunca zero.
//
// [CONTRATO] Os offsets de _decodeSchema3 seguem a tabela da spec. Ao mudar o
// payload no firmware (TelemetryPayloadRevB.h), atualize aqui e no web_dash.
// [NOVO SENSOR] Campo novo: acrescente na classe do bloco, leia no offset em
// _decodeSchema3 e exiba em lib/ui/dashboard_page.dart.
import 'dart:typed_data';

/// nan/inf viram null.
double? _finite(double value) => value.isFinite ? value : null;

/// Um nó elétrico: MPPT A/B/C ou bateria.
class ChannelReading {
  const ChannelReading({
    required this.id,
    required this.label,
    this.voltageV,
    this.currentA,
    this.powerW,
    this.energyWh,
    this.flowLpm,
    this.validFlags = 0,
  });

  const ChannelReading.empty(this.id, this.label)
      : voltageV = null,
        currentA = null,
        powerW = null,
        energyWh = null,
        flowLpm = null,
        validFlags = 0;

  final String id;
  final String label;
  final double? voltageV;
  final double? currentA;
  final double? powerW;
  final double? energyWh;
  final double? flowLpm;
  final int validFlags;
}

/// Posição, fix, velocidade (nós) e rumo do GPS.
class GpsReading {
  const GpsReading({
    this.valid = false,
    this.latitude,
    this.longitude,
    this.hdop,
    this.satellites = 0,
    this.fixQuality = 0,
    this.speedKn,
    this.courseDeg,
  });

  final bool valid;
  final double? latitude;
  final double? longitude;
  final double? hdop;
  final int satellites;
  final int fixQuality;
  final double? speedKn;
  final double? courseDeg;
}

/// Aceleração (g) e atitude (graus) — IMU ainda sem driver no firmware.
class ImuReading {
  const ImuReading({this.valid = false, this.accelX, this.accelY, this.accelZ, this.pitchDeg, this.rollDeg});

  final bool valid;
  final double? accelX;
  final double? accelY;
  final double? accelZ;
  final double? pitchDeg;
  final double? rollDeg;
}

/// Origem do estado de carga (igual ao enum SocSource do firmware).
enum SocSource { none, coulomb, voltageEstimate, fullSync }

/// Estado de carga calculado no RP2040 (ADR-005).
class BatteryState {
  const BatteryState({this.socPct, this.ahFromFull, this.source = SocSource.none});

  final double? socPct;
  final double? ahFromFull;
  final SocSource source;
}

/// Dados do VESC (bloco motor do schema 3 ou frame 0x08).
class MotorStatus {
  const MotorStatus({
    this.valid = false,
    this.fault = 0,
    this.ageMs,
    this.rpm,
    this.temperatureC,
    this.fetTemperatureC,
    this.inputCurrentA,
    this.inputVoltageV,
    this.dutyPct,
  });

  final bool valid;
  final int fault;
  final int? ageMs;
  final double? rpm;
  final double? temperatureC;
  final double? fetTemperatureC;
  final double? inputCurrentA;
  final double? inputVoltageV;
  final double? dutyPct;

  double? get inputPowerW =>
      inputCurrentA != null && inputVoltageV != null ? inputCurrentA! * inputVoltageV! : null;
}

/// Snapshot completo de um frame de telemetria.
class TelemetrySnapshot {
  const TelemetrySnapshot({
    required this.schemaVersion,
    required this.timestampMs,
    required this.channels,
    required this.temperatures,
    this.gps = const GpsReading(),
    this.imu = const ImuReading(),
    this.battery = const BatteryState(),
    this.motor = const MotorStatus(),
    this.supply5V,
    this.errorFlags = 0,
  });

  final int schemaVersion;
  final int timestampMs;

  /// Sempre MPPT A, MPPT B, MPPT C e BATERIA, nesta ordem.
  final List<ChannelReading> channels;

  /// Sempre 10 posições; `null` quando o sensor não existe ou falhou.
  final List<double?> temperatures;
  final GpsReading gps;
  final ImuReading imu;
  final BatteryState battery;
  final MotorStatus motor;
  final double? supply5V;
  final int errorFlags;

  ChannelReading channel(String id) => channels.firstWhere((channel) => channel.id == id);
}

/// Bits de error_flags (TelemetryErrorFlag no firmware). [CONTRATO]
class ErrorFlag {
  static const int sdOffline = 1 << 0;
  static const int gpsNoFix = 1 << 1;
  static const int noTemperature = 1 << 2;
  static const int adsError = 1 << 3;
  static const int ina226Error = 1 << 4;
  static const int vescStale = 1 << 5;
  static const int vescFault = 1 << 6;
  static const int supply5vOutOfRange = 1 << 7;
  static const int socUncalibrated = 1 << 8;
}

const List<(String, String)> channelLabels = [
  ('mppt_a', 'MPPT A'),
  ('mppt_b', 'MPPT B'),
  ('mppt_c', 'MPPT C'),
  ('battery', 'BATERIA'),
];

/// Leitor little-endian de campos do payload.
class _Reader {
  _Reader(Uint8List bytes) : _data = ByteData.sublistView(bytes);

  final ByteData _data;

  int u8(int offset) => _data.getUint8(offset);
  int u16(int offset) => _data.getUint16(offset, Endian.little);
  int u32(int offset) => _data.getUint32(offset, Endian.little);
  double f32(int offset) => _data.getFloat32(offset, Endian.little);

  ChannelReading channel(int offset, String id, String label) => ChannelReading(
        id: id,
        label: label,
        voltageV: _finite(f32(offset)),
        currentA: _finite(f32(offset + 4)),
        powerW: _finite(f32(offset + 8)),
        energyWh: _finite(f32(offset + 12)),
        flowLpm: _finite(f32(offset + 16)),
        validFlags: u32(offset + 20),
      );

  List<double?> temperatures(int offset, int count) {
    final valid = count.clamp(0, 10);
    return List<double?>.generate(10, (index) => index < valid ? _finite(f32(offset + index * 4)) : null);
  }
}

/// Escolhe o decodificador pelo tamanho/primeiro byte; null se desconhecido.
TelemetrySnapshot? decodeTelemetryPayload(Uint8List payload) {
  if (payload.length == 32) return _decodeSchema1(payload);
  if (payload.isEmpty) return null;
  if (payload[0] == 2 && payload.length >= 188) return _decodeSchema2(payload);
  if (payload[0] == 3 && payload.length >= 240) return _decodeSchema3(payload);
  return null;
}

/// Schema 1 (legado, 32 bytes).
TelemetrySnapshot _decodeSchema1(Uint8List payload) {
  final r = _Reader(payload);
  final flags = r.u32(28);
  return TelemetrySnapshot(
    schemaVersion: 1,
    timestampMs: r.u32(0),
    channels: [
      ChannelReading(id: 'mppt_a', label: 'MPPT A', powerW: _finite(r.f32(4)), flowLpm: _finite(r.f32(16)), validFlags: flags & 0xFF),
      ChannelReading(id: 'mppt_b', label: 'MPPT B', powerW: _finite(r.f32(8)), flowLpm: _finite(r.f32(20)), validFlags: (flags >> 8) & 0xFF),
      const ChannelReading.empty('mppt_c', 'MPPT C'),
      ChannelReading(id: 'battery', label: 'BATERIA', powerW: _finite(r.f32(12)), validFlags: (flags >> 16) & 0xFF),
    ],
    temperatures: [_finite(r.f32(24)), ...List<double?>.filled(9, null)],
  );
}

/// Schema 2 (188 bytes, sem MPPT C, SoC nem motor).
TelemetrySnapshot _decodeSchema2(Uint8List payload) {
  final r = _Reader(payload);
  // Ordem do schema 2: MPPT_A, MPPT_B, BATTERY, reserva.
  return TelemetrySnapshot(
    schemaVersion: 2,
    timestampMs: r.u32(4),
    channels: [
      r.channel(8, 'mppt_a', 'MPPT A'),
      r.channel(32, 'mppt_b', 'MPPT B'),
      const ChannelReading.empty('mppt_c', 'MPPT C'),
      r.channel(56, 'battery', 'BATERIA'),
    ],
    temperatures: r.temperatures(104, r.u8(2)),
    gps: GpsReading(
      valid: r.u8(144) != 0,
      fixQuality: r.u8(145),
      satellites: r.u8(146),
      latitude: _finite(r.f32(148)),
      longitude: _finite(r.f32(152)),
      hdop: _finite(r.f32(156)),
    ),
    imu: ImuReading(
      valid: r.u8(160) != 0,
      accelX: _finite(r.f32(164)),
      accelY: _finite(r.f32(168)),
      accelZ: _finite(r.f32(172)),
      pitchDeg: _finite(r.f32(176)),
      rollDeg: _finite(r.f32(180)),
    ),
    errorFlags: r.u32(184),
  );
}

/// Schema 3 (240 bytes). Offsets conforme TELEMETRY_FRAME.md.
TelemetrySnapshot _decodeSchema3(Uint8List payload) {
  final r = _Reader(payload);
  final motorValid = r.u8(204) != 0;
  double? motorValue(int offset) => motorValid ? _finite(r.f32(offset)) : null;
  final socSource = r.u8(200);
  return TelemetrySnapshot(
    schemaVersion: 3,
    timestampMs: r.u32(4),
    channels: [
      for (var index = 0; index < channelLabels.length; index++)
        r.channel(8 + index * 24, channelLabels[index].$1, channelLabels[index].$2),
    ],
    temperatures: r.temperatures(104, r.u8(2)),
    gps: GpsReading(
      valid: r.u8(144) != 0,
      fixQuality: r.u8(145),
      satellites: r.u8(146),
      latitude: _finite(r.f32(148)),
      longitude: _finite(r.f32(152)),
      hdop: _finite(r.f32(156)),
      speedKn: _finite(r.f32(160)),
      courseDeg: _finite(r.f32(164)),
    ),
    imu: ImuReading(
      valid: r.u8(168) != 0,
      accelX: _finite(r.f32(172)),
      accelY: _finite(r.f32(176)),
      accelZ: _finite(r.f32(180)),
      pitchDeg: _finite(r.f32(184)),
      rollDeg: _finite(r.f32(188)),
    ),
    battery: BatteryState(
      socPct: _finite(r.f32(192)),
      ahFromFull: _finite(r.f32(196)),
      source: socSource < SocSource.values.length ? SocSource.values[socSource] : SocSource.none,
    ),
    motor: MotorStatus(
      valid: motorValid,
      fault: r.u8(205),
      ageMs: r.u16(206),
      rpm: motorValue(208),
      temperatureC: motorValue(212),
      fetTemperatureC: motorValue(216),
      inputCurrentA: motorValue(220),
      inputVoltageV: motorValue(224),
      dutyPct: motorValue(228),
    ),
    supply5V: _finite(r.f32(232)),
    errorFlags: r.u32(236),
  );
}

/// Frame 0x08 (status do VESC, 28 bytes) enviado pelo gateway ESP32-C3 (M9).
MotorStatus? decodeVescStatus(Uint8List payload) {
  if (payload.length < 28 || payload[0] != 1) return null;
  final r = _Reader(payload);
  final flags = r.u8(1);
  final valid = (flags & 0x01) != 0 && (flags & 0x04) == 0;
  double? value(int offset) => valid ? _finite(r.f32(offset)) : null;
  return MotorStatus(
    valid: valid,
    fault: r.u8(2),
    ageMs: 0,
    rpm: value(4),
    temperatureC: value(8),
    fetTemperatureC: value(12),
    inputCurrentA: value(16),
    inputVoltageV: value(20),
    dutyPct: value(24),
  );
}

/// Ocupação das filas do DashboardAntenna (heartbeat 0x02).
class GatewayHeartbeat {
  const GatewayHeartbeat({required this.bleQueue, required this.uartQueue});

  final int bleQueue;
  final int uartQueue;
}

/// Frame 0x02 do DashboardAntenna: [versão, tablet conectado, fila BLE, fila UART, ...].
GatewayHeartbeat? decodeHeartbeat(Uint8List payload) {
  if (payload.length < 4 || payload[0] != 1) return null;
  return GatewayHeartbeat(bleQueue: payload[2], uartQueue: payload[3]);
}
