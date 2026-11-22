// Conexão BLE com o DashboardAntenna (ESP32-C3) — docs/specs/DASHBOARD_BLE.md.
// Procura o dispositivo, conecta pedindo MTU 517, assina as 3 características e
// decodifica cada notificação. Reconecta sozinho. Também tem o modo DEMO.
import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

import '../protocol/demo_source.dart';
import '../protocol/rev_b_frame.dart';
import '../protocol/telemetry.dart';

// [CONTRATO] UUIDs e nome iguais aos do DashboardAntenna.ino.
final Guid serviceUuid = Guid('21241d37-fc68-4c30-9e72-2e23f727b4d4');
final Guid telemetryUuid = Guid('df903778-7fd2-40d3-a6ed-25f2e2d1185f');
final Guid controlUuid = Guid('1f5b3aa7-2e71-4d0c-a2d3-b4f0ed1c1c20');
final Guid heartbeatUuid = Guid('e2f0f8a1-3c70-4df5-8b40-9ae6b08e9aa4');
const String deviceName = 'Dashboard Babitonga';

/// Maior frame (schema 3) = 254 bytes; uma notificação carrega MTU - 3 bytes.
const int requiredMtu = 257;
const int requestedMtu = 517;

/// Estados mostrados no cabeçalho do app.
enum LinkState { bluetoothOff, scanning, connecting, connected, demo }

/// Estado da conexão + último snapshot; notifica a interface a cada mudança.
class DashboardLink extends ChangeNotifier {
  LinkState state = LinkState.scanning;
  String detail = 'Procurando o DashboardAntenna…';
  TelemetrySnapshot? snapshot;
  MotorStatus? _gatewayMotor;
  DateTime? _gatewayMotorAt;
  DateTime? lastFrameAt;
  int framesReceived = 0;
  int crcErrors = 0;
  int mtu = 23;
  int? bleRssi;
  int? sequence;
  GatewayHeartbeat? heartbeat;

  final RevBFrameDecoder _decoder = RevBFrameDecoder();
  final List<StreamSubscription<dynamic>> _subscriptions = [];
  StreamSubscription<BluetoothAdapterState>? _adapterSubscription;
  StreamSubscription<List<ScanResult>>? _scanSubscription;
  BluetoothDevice? _device;
  Timer? _demoTimer;
  Timer? _rssiTimer;
  bool _disposed = false;
  bool _started = false;

  bool get isDemo => state == LinkState.demo;

  /// Dados do motor: um 0x08 recente do gateway (5 Hz) tem prioridade sobre a
  /// cópia de 1 Hz dentro do payload. Dado velho nunca aparece como atual.
  MotorStatus get motor {
    final gatewayAt = _gatewayMotorAt;
    if (_gatewayMotor != null && gatewayAt != null && DateTime.now().difference(gatewayAt).inMilliseconds < 1500) {
      return _gatewayMotor!;
    }
    return snapshot?.motor ?? const MotorStatus();
  }

  /// Sem frame há 3 s ou mais (bolinha amarela no cabeçalho).
  bool get stale => lastFrameAt == null || DateTime.now().difference(lastFrameAt!).inSeconds >= 3;

  /// Começa a acompanhar o Bluetooth do tablet (chamado no main.dart).
  void start() {
    _started = true;
    _adapterSubscription = FlutterBluePlus.adapterState.listen((adapterState) {
      if (isDemo) return;
      if (adapterState == BluetoothAdapterState.on) {
        _scan();
      } else {
        _setState(LinkState.bluetoothOff, 'Bluetooth desligado');
      }
    });
  }

  Future<void> _scan() async {
    if (_disposed || isDemo || state == LinkState.connecting || state == LinkState.connected) return;
    _setState(LinkState.scanning, 'Procurando o DashboardAntenna…');
    await _scanSubscription?.cancel();
    _scanSubscription = FlutterBluePlus.onScanResults.listen((results) {
      for (final result in results) {
        if (result.device.platformName == deviceName ||
            result.advertisementData.serviceUuids.contains(serviceUuid)) {
          unawaited(_connect(result.device));
          return;
        }
      }
    });
    try {
      await FlutterBluePlus.startScan(withServices: [serviceUuid], timeout: const Duration(seconds: 15));
    } catch (error) {
      _setState(LinkState.scanning, 'Falha ao iniciar a busca: $error');
    }
    // A busca terminou sem conexão: tenta de novo (sem laço ocupado e sem sleep).
    await FlutterBluePlus.isScanning.where((scanning) => !scanning).first;
    if (!_disposed && state == LinkState.scanning) {
      Future<void>.delayed(const Duration(seconds: 2), _scan);
    }
  }

  Future<void> _connect(BluetoothDevice device) async {
    if (state == LinkState.connecting || state == LinkState.connected || isDemo) return;
    _setState(LinkState.connecting, 'Conectando…');
    await FlutterBluePlus.stopScan();
    await _scanSubscription?.cancel();
    _device = device;
    try {
      _subscriptions.add(device.connectionState.listen((connection) {
        if (connection == BluetoothConnectionState.disconnected && !isDemo) {
          _onDisconnected();
        }
      }));
      await device.connect(timeout: const Duration(seconds: 15), mtu: requestedMtu);
      mtu = device.mtuNow;
      if (mtu < requiredMtu) {
        // Frames maiores que MTU-3 chegam truncados e falham no CRC.
        detail = 'MTU $mtu < $requiredMtu: frames do schema 3 serão truncados';
      }
      final services = await device.discoverServices();
      final service = services.firstWhere((item) => item.uuid == serviceUuid);
      for (final characteristic in service.characteristics) {
        if (characteristic.uuid == telemetryUuid ||
            characteristic.uuid == heartbeatUuid ||
            characteristic.uuid == controlUuid) {
          _subscriptions.add(characteristic.onValueReceived.listen(_onNotification));
          await characteristic.setNotifyValue(true);
        }
      }
      _setState(LinkState.connected, mtu < requiredMtu ? detail : 'Conectado · MTU $mtu');
      _rssiTimer = Timer.periodic(const Duration(seconds: 3), (_) async {
        try {
          bleRssi = await device.readRssi();
          _notify();
        } catch (_) {
          // O RSSI é só informativo.
        }
      });
    } catch (error) {
      await _teardown();
      await device.disconnect();
      _setState(LinkState.scanning, 'Falha na conexão: $error');
      Future<void>.delayed(const Duration(seconds: 2), _scan);
    }
  }

  /// Desconectou: limpa as assinaturas e volta a procurar.
  void _onDisconnected() {
    unawaited(_teardown());
    _setState(LinkState.scanning, 'Desconectado; procurando novamente…');
    Future<void>.delayed(const Duration(seconds: 1), _scan);
  }

  Future<void> _teardown() async {
    _rssiTimer?.cancel();
    _rssiTimer = null;
    for (final subscription in _subscriptions) {
      await subscription.cancel();
    }
    _subscriptions.clear();
    bleRssi = null;
  }

  void _onNotification(List<int> value) {
    // Uma notificação = frames inteiros; uma truncada não contamina a próxima.
    _decoder.reset();
    for (final frame in _decoder.feedAll(value)) {
      handleFrame(frame);
    }
    crcErrors = _decoder.crcErrors;
    _notify();
  }

  /// Encaminha cada tipo de frame ao seu decodificador.
  /// [NOVO SENSOR] Um tipo de frame novo ganha um `case` aqui.
  @visibleForTesting
  void handleFrame(RevBFrame frame) {
    switch (frame.type) {
      case FrameType.telemetry:
        final decoded = decodeTelemetryPayload(frame.payload);
        if (decoded == null) return;
        snapshot = decoded;
        sequence = frame.sequence;
        framesReceived++;
        lastFrameAt = DateTime.now();
      case FrameType.vescStatus:
        final motorStatus = decodeVescStatus(frame.payload);
        if (motorStatus == null) return;
        _gatewayMotor = motorStatus;
        _gatewayMotorAt = DateTime.now();
      case FrameType.status:
        heartbeat = decodeHeartbeat(frame.payload) ?? heartbeat;
      default:
        break;
    }
  }

  /// O modo demo passa frames codificados pelo MESMO decodificador do BLE.
  Future<void> setDemo(bool enabled) async {
    if (enabled == isDemo) return;
    if (enabled) {
      if (_started) {
        await FlutterBluePlus.stopScan();
        await _teardown();
        await _device?.disconnect();
      }
      final source = DemoSource();
      _setState(LinkState.demo, 'Modo demonstração');
      void tick() => _onNotification(source.nextTelemetryFrame());
      tick();
      _demoTimer = Timer.periodic(const Duration(seconds: 1), (_) => tick());
    } else {
      _demoTimer?.cancel();
      _demoTimer = null;
      snapshot = null;
      lastFrameAt = null;
      _setState(LinkState.scanning, 'Procurando o DashboardAntenna…');
      if (_started) unawaited(_scan());
    }
  }

  /// Auxiliar de teste/demo: decodifica bytes de uma notificação.
  @visibleForTesting
  void injectNotification(Uint8List bytes) => _onNotification(bytes);

  void _setState(LinkState next, String message) {
    state = next;
    detail = message;
    _notify();
  }

  void _notify() {
    if (!_disposed) notifyListeners();
  }

  @override
  void dispose() {
    _disposed = true;
    _demoTimer?.cancel();
    _rssiTimer?.cancel();
    _adapterSubscription?.cancel();
    _scanSubscription?.cancel();
    for (final subscription in _subscriptions) {
      subscription.cancel();
    }
    super.dispose();
  }
}
