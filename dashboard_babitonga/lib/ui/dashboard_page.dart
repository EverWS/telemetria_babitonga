import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../ble/dashboard_link.dart';
import '../protocol/telemetry.dart';
import 'palette.dart';

/// [AJUSTE] Limites dos cards: ajuste ao motor e ao banco de baterias reais
/// (mesmos valores de CONFIG_SCHEMA v2 / web_dash).
const double motorTempAlarmC = 80;
const double socLowPct = 20;

/// [AJUSTE] Nomes das 10 temperaturas (troque pelo local real de cada sensor).
const List<String> tempLabels = [
  'TEMP 01', 'TEMP 02', 'TEMP 03', 'TEMP 04', 'TEMP 05',
  'TEMP 06', 'TEMP 07', 'TEMP 08', 'TEMP 09', 'TEMP 10',
];

/// Formata número + unidade; null vira "--" (nunca 0).
String fmt(double? value, int digits, [String unit = '']) =>
    value == null ? '--' : '${value.toStringAsFixed(digits)}${unit.isEmpty ? '' : ' $unit'}';

/// Tela principal: cabeçalho, 6 indicadores, GPS/IMU, energia, fluxo e
/// temperaturas. Em telas largas (>= 1000 dp) usa duas colunas.
class DashboardPage extends StatelessWidget {
  const DashboardPage({super.key});

  @override
  Widget build(BuildContext context) {
    final link = context.watch<DashboardLink>();
    final snapshot = link.snapshot;
    return Scaffold(
      body: SafeArea(
        child: Column(
          children: [
            _Header(link: link),
            Expanded(
              child: LayoutBuilder(builder: (context, constraints) {
                final wide = constraints.maxWidth >= 1000;
                return SingleChildScrollView(
                  padding: const EdgeInsets.fromLTRB(16, 14, 16, 20),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      _KpiStrip(link: link, columns: wide ? 6 : 3),
                      const SizedBox(height: 16),
                      if (wide)
                        Row(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Expanded(flex: 4, child: _NavigationPanel(snapshot: snapshot)),
                            const SizedBox(width: 14),
                            Expanded(flex: 6, child: _EnergyPanel(snapshot: snapshot, motor: link.motor)),
                          ],
                        )
                      else ...[
                        _NavigationPanel(snapshot: snapshot),
                        const SizedBox(height: 14),
                        _EnergyPanel(snapshot: snapshot, motor: link.motor),
                      ],
                      const SizedBox(height: 16),
                      _TemperaturePanel(snapshot: snapshot, columns: wide ? 10 : 5),
                      const SizedBox(height: 16),
                      _FooterStatus(link: link),
                    ],
                  ),
                );
              }),
            ),
          ],
        ),
      ),
    );
  }
}

/// Cabeçalho: logo, estado da conexão, número do frame e botão DEMO.
class _Header extends StatelessWidget {
  const _Header({required this.link});

  final DashboardLink link;

  @override
  Widget build(BuildContext context) {
    final connected = link.state == LinkState.connected || link.isDemo;
    final dotColor = connected && !link.stale ? Palette.green : (connected ? Palette.sun : Palette.danger);
    return Container(
      decoration: const BoxDecoration(
        color: Palette.surface,
        border: Border(bottom: BorderSide(color: Palette.border)),
      ),
      child: Column(
        children: [
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
            child: Row(
              children: [
                ClipRect(
                  child: SizedBox(
                    width: 44,
                    height: 44,
                    child: OverflowBox(
                      alignment: Alignment.topLeft,
                      maxWidth: 70,
                      maxHeight: 70,
                      child: Transform.translate(
                        offset: const Offset(-12.5, 0),
                        child: Image.asset('assets/logo_babitonga.png', width: 70),
                      ),
                    ),
                  ),
                ),
                const SizedBox(width: 12),
                const Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text.rich(TextSpan(children: [
                        TextSpan(text: 'Equipe Babitonga ', style: TextStyle(fontWeight: FontWeight.w700, fontSize: 18)),
                        TextSpan(
                            text: '· Telemetria',
                            style: TextStyle(fontWeight: FontWeight.w700, fontSize: 18, color: Palette.brandDark)),
                      ])),
                      Text('UFSC · CTJ · Joinville — embarcação solar',
                          style: TextStyle(fontSize: 12, color: Palette.textDim)),
                    ],
                  ),
                ),
                _Pill(
                  child: Row(mainAxisSize: MainAxisSize.min, children: [
                    Container(width: 9, height: 9, decoration: BoxDecoration(color: dotColor, shape: BoxShape.circle)),
                    const SizedBox(width: 8),
                    ConstrainedBox(
                      constraints: const BoxConstraints(maxWidth: 260),
                      child: Text(link.detail,
                          overflow: TextOverflow.ellipsis,
                          style: const TextStyle(fontSize: 12, fontWeight: FontWeight.w600, color: Palette.textDim)),
                    ),
                  ]),
                ),
                const SizedBox(width: 10),
                _Pill(
                  child: Text(link.sequence == null ? 'Frame --' : 'Frame #${link.sequence}',
                      style: const TextStyle(fontSize: 12, color: Palette.brandDark, fontWeight: FontWeight.w600)),
                ),
                const SizedBox(width: 10),
                FilterChip(
                  label: const Text('DEMO'),
                  selected: link.isDemo,
                  onSelected: (value) => link.setDemo(value),
                  selectedColor: Palette.brand.withValues(alpha: .25),
                ),
              ],
            ),
          ),
          Container(
            height: 3,
            decoration: const BoxDecoration(
              gradient: LinearGradient(colors: [Palette.sky, Palette.water, Palette.brand, Palette.sea, Palette.green]),
            ),
          ),
        ],
      ),
    );
  }
}

class _Pill extends StatelessWidget {
  const _Pill({required this.child});

  final Widget child;

  @override
  Widget build(BuildContext context) => Container(
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
        decoration: BoxDecoration(
          color: Palette.surface2,
          borderRadius: BorderRadius.circular(999),
          border: Border.all(color: Palette.border),
        ),
        child: child,
      );
}

/// Card branco com borda e faixa de cor opcional à esquerda.
class _Card extends StatelessWidget {
  const _Card({required this.child, this.accent});

  static const EdgeInsets padding = EdgeInsets.all(14);

  final Widget child;
  final Color? accent;

  @override
  Widget build(BuildContext context) => Container(
        decoration: BoxDecoration(
          color: Palette.surface,
          borderRadius: BorderRadius.circular(12),
          border: Border.all(color: Palette.border),
          boxShadow: const [BoxShadow(color: Color(0x14163C46), blurRadius: 12, offset: Offset(0, 4))],
        ),
        clipBehavior: Clip.antiAlias,
        child: Container(
          decoration: accent == null
              ? null
              : BoxDecoration(border: Border(left: BorderSide(color: accent!, width: 4))),
          padding: padding,
          child: child,
        ),
      );
}

/// Título de seção com a barrinha em degradê.
class _SectionTitle extends StatelessWidget {
  const _SectionTitle(this.text);

  final String text;

  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.only(bottom: 10),
        child: Row(children: [
          Container(
            width: 4,
            height: 16,
            decoration: BoxDecoration(
              borderRadius: BorderRadius.circular(2),
              gradient: const LinearGradient(
                  begin: Alignment.topCenter, end: Alignment.bottomCenter, colors: [Palette.water, Palette.brand]),
            ),
          ),
          const SizedBox(width: 8),
          Text(text.toUpperCase(),
              style: const TextStyle(fontSize: 12, fontWeight: FontWeight.w700, letterSpacing: 1.1)),
        ]),
      );
}

// ---------------------------------------------------------------------------
// Indicadores do topo: velocidade, RPM, temperatura do motor, carga, GPS, sinal.
// [NOVO SENSOR] Um card novo = um _Kpi a mais na lista de _KpiStrip.build().
// ---------------------------------------------------------------------------
class _Kpi {
  const _Kpi(this.label, this.value, this.unit, this.sub, this.accent, {this.alert = false, this.bar});

  final String label;
  final String value;
  final String unit;
  final String sub;
  final Color accent;
  final bool alert;
  final double? bar;
}

class _KpiStrip extends StatelessWidget {
  const _KpiStrip({required this.link, required this.columns});

  final DashboardLink link;
  final int columns;

  @override
  Widget build(BuildContext context) {
    final snapshot = link.snapshot;
    final gps = snapshot?.gps ?? const GpsReading();
    final motor = link.motor;
    final battery = snapshot?.channel('battery');
    final soc = snapshot?.battery.socPct?.clamp(0, 100).toDouble();
    final socSource = switch (snapshot?.battery.source) {
      SocSource.voltageEstimate => ' · estimada',
      SocSource.fullSync => ' · cheia',
      _ => '',
    };
    final motorHot = motor.temperatureC != null && motor.temperatureC! > motorTempAlarmC;
    final kpis = [
      _Kpi('Velocidade', fmt(gps.speedKn, 1), 'nós',
          gps.speedKn != null ? 'SOG · ${fmt(gps.speedKn! * 1.852, 1, 'km/h')}' : (gps.valid ? 'Sem velocidade' : 'Sem fix GPS'),
          Palette.brand),
      _Kpi('Rotação do motor', fmt(motor.rpm, 0), 'RPM',
          motor.valid ? 'Duty ${fmt(motor.dutyPct, 0, '%')}' : 'Sem leitura do VESC', Palette.sun),
      _Kpi('Temperatura do motor', fmt(motor.temperatureC, 1), '°C',
          motor.temperatureC == null ? 'Sem leitura' : (motorHot ? 'Acima de ${motorTempAlarmC.toStringAsFixed(0)} °C' : 'FET ${fmt(motor.fetTemperatureC, 0, '°C')}'),
          Palette.sea,
          alert: motorHot),
      _Kpi('Carga da bateria', fmt(soc, 0), '%',
          '${fmt(battery?.voltageV, 1, 'V')} · ${fmt(battery?.currentA, 1, 'A')}$socSource', Palette.green,
          alert: soc != null && soc < socLowPct, bar: soc),
      _Kpi('GPS', gps.valid ? '${gps.satellites}' : '--', 'sat',
          gps.valid ? 'Fix ${gps.fixQuality} · HDOP ${fmt(gps.hdop, 1)}' : 'Sem fix', Palette.sky,
          alert: snapshot != null && !gps.valid),
      _Kpi('Sinal BLE', link.bleRssi?.toString() ?? '--', 'dBm',
          link.isDemo ? 'Demonstração' : 'MTU ${link.mtu} · CRC ${link.crcErrors}', Palette.water),
    ];
    return LayoutBuilder(builder: (context, constraints) {
      const gap = 12.0;
      final width = (constraints.maxWidth - gap * (columns - 1)) / columns;
      return Wrap(
        spacing: gap,
        runSpacing: gap,
        children: [for (final kpi in kpis) SizedBox(width: width, child: _KpiCard(kpi: kpi))],
      );
    });
  }
}

class _KpiCard extends StatelessWidget {
  const _KpiCard({required this.kpi});

  final _Kpi kpi;

  @override
  Widget build(BuildContext context) {
    final hasValue = kpi.value != '--';
    return _Card(
      accent: kpi.alert ? Palette.danger : kpi.accent,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(kpi.label.toUpperCase(),
              style: const TextStyle(fontSize: 11, fontWeight: FontWeight.w700, letterSpacing: 1, color: Palette.textDim)),
          const SizedBox(height: 4),
          Text.rich(
            TextSpan(children: [
              TextSpan(
                  text: kpi.value,
                  style: TextStyle(
                      fontSize: 26,
                      fontWeight: FontWeight.w700,
                      color: hasValue ? Palette.text : Palette.textFaint,
                      fontFeatures: const [FontFeature.tabularFigures()])),
              if (hasValue)
                TextSpan(text: ' ${kpi.unit}', style: const TextStyle(fontSize: 13, color: Palette.textDim)),
            ]),
            maxLines: 1,
          ),
          Text(kpi.sub,
              maxLines: 1, overflow: TextOverflow.ellipsis, style: const TextStyle(fontSize: 12, color: Palette.textDim)),
          if (kpi.bar != null) ...[
            const SizedBox(height: 6),
            ClipRRect(
              borderRadius: BorderRadius.circular(2),
              child: LinearProgressIndicator(
                value: kpi.bar! / 100,
                minHeight: 4,
                backgroundColor: Palette.border,
                color: kpi.alert ? Palette.danger : Palette.green,
              ),
            ),
          ],
        ],
      ),
    );
  }
}

// ---------------------------------------------------------------------------
// Navegação: posição/rumo do GPS (bússola) e atitude do casco (IMU).
// ---------------------------------------------------------------------------
class _NavigationPanel extends StatelessWidget {
  const _NavigationPanel({required this.snapshot});

  final TelemetrySnapshot? snapshot;

  @override
  Widget build(BuildContext context) {
    final gps = snapshot?.gps ?? const GpsReading();
    final imu = snapshot?.imu ?? const ImuReading();
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        _Card(
          child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            const _SectionTitle('GPS — posição e rumo'),
            Row(children: [
              _Compass(courseDeg: gps.valid ? gps.courseDeg : null),
              const SizedBox(width: 16),
              Expanded(
                child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                  _InfoRow('Latitude', gps.valid ? fmt(gps.latitude, 6) : '--'),
                  _InfoRow('Longitude', gps.valid ? fmt(gps.longitude, 6) : '--'),
                  _InfoRow('Rumo', gps.valid ? fmt(gps.courseDeg, 0, '°') : '--'),
                  _InfoRow('Satélites / HDOP', gps.valid ? '${gps.satellites} / ${fmt(gps.hdop, 1)}' : '--'),
                ]),
              ),
            ]),
          ]),
        ),
        const SizedBox(height: 14),
        _Card(
          child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            const _SectionTitle('IMU — atitude do casco'),
            _TiltBar(label: 'PROA / POPA (pitch)', value: imu.valid ? imu.pitchDeg : null),
            const SizedBox(height: 10),
            _TiltBar(label: 'BB / BE (roll)', value: imu.valid ? imu.rollDeg : null),
          ]),
        ),
      ],
    );
  }
}

class _InfoRow extends StatelessWidget {
  const _InfoRow(this.label, this.value);

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.symmetric(vertical: 3),
        child: Row(children: [
          Expanded(child: Text(label, style: const TextStyle(fontSize: 13, color: Palette.textDim))),
          Text(value,
              style: const TextStyle(
                  fontSize: 15, fontWeight: FontWeight.w700, fontFeatures: [FontFeature.tabularFigures()])),
        ]),
      );
}

class _Compass extends StatelessWidget {
  const _Compass({required this.courseDeg});

  final double? courseDeg;

  @override
  Widget build(BuildContext context) => SizedBox(
        width: 96,
        height: 96,
        child: CustomPaint(painter: _CompassPainter(courseDeg)),
      );
}

class _CompassPainter extends CustomPainter {
  _CompassPainter(this.courseDeg);

  final double? courseDeg;

  @override
  void paint(Canvas canvas, Size size) {
    final center = size.center(Offset.zero);
    final radius = size.width / 2 - 4;
    canvas.drawCircle(center, radius, Paint()..color = Palette.surface2);
    canvas.drawCircle(
        center,
        radius,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 2
          ..color = Palette.border);
    final textPainter = TextPainter(
      text: const TextSpan(text: 'N', style: TextStyle(fontSize: 11, fontWeight: FontWeight.w700, color: Palette.textDim)),
      textDirection: TextDirection.ltr,
    )..layout();
    textPainter.paint(canvas, Offset(center.dx - textPainter.width / 2, center.dy - radius + 3));
    if (courseDeg == null) return;
    final angle = (courseDeg! - 90) * math.pi / 180;
    final tip = center + Offset(math.cos(angle), math.sin(angle)) * (radius - 12);
    final tail = center - Offset(math.cos(angle), math.sin(angle)) * (radius - 30);
    canvas.drawLine(
        tail,
        tip,
        Paint()
          ..strokeWidth = 4
          ..strokeCap = StrokeCap.round
          ..color = Palette.brandDark);
    canvas.drawCircle(tip, 5, Paint()..color = Palette.sun);
  }

  @override
  bool shouldRepaint(_CompassPainter oldDelegate) => oldDelegate.courseDeg != courseDeg;
}

class _TiltBar extends StatelessWidget {
  const _TiltBar({required this.label, required this.value});

  final String label;
  final double? value;

  @override
  Widget build(BuildContext context) {
    const range = 30.0;
    final magnitude = value?.abs() ?? 0;
    final color = value == null
        ? Palette.textFaint
        : magnitude < 5
            ? Palette.ok
            : magnitude < 12
                ? Palette.warn
                : Palette.danger;
    return Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      Row(children: [
        Expanded(child: Text(label, style: const TextStyle(fontSize: 12, color: Palette.textDim))),
        Text(value == null ? '--' : '${value! >= 0 ? '+' : ''}${value!.toStringAsFixed(1)}°',
            style: TextStyle(fontSize: 18, fontWeight: FontWeight.w800, color: color)),
      ]),
      const SizedBox(height: 4),
      LayoutBuilder(builder: (context, constraints) {
        final position = ((value ?? 0).clamp(-range, range) + range) / (2 * range);
        return SizedBox(
          height: 14,
          child: Stack(children: [
            Positioned.fill(
              top: 4,
              bottom: 4,
              child: Container(
                  decoration: BoxDecoration(color: Palette.surface2, border: Border.all(color: Palette.border))),
            ),
            Positioned(left: constraints.maxWidth / 2 - 1, top: 0, bottom: 0, child: Container(width: 2, color: Palette.textFaint)),
            if (value != null)
              Positioned(
                left: (constraints.maxWidth - 12) * position,
                top: 1,
                child: Transform.rotate(
                    angle: math.pi / 4, child: Container(width: 12, height: 12, color: color)),
              ),
          ]),
        );
      }),
    ]);
  }
}

// ---------------------------------------------------------------------------
// Energia: 3 MPPTs, bateria, total solar, entrada do motor e fluxo.
// [NOVO SENSOR] Um nó elétrico novo aparece sozinho se vier em snapshot.channels.
// ---------------------------------------------------------------------------
class _EnergyPanel extends StatelessWidget {
  const _EnergyPanel({required this.snapshot, required this.motor});

  final TelemetrySnapshot? snapshot;
  final MotorStatus motor;

  @override
  Widget build(BuildContext context) {
    final channels = snapshot?.channels ??
        const [
          ChannelReading.empty('mppt_a', 'MPPT A'),
          ChannelReading.empty('mppt_b', 'MPPT B'),
          ChannelReading.empty('mppt_c', 'MPPT C'),
          ChannelReading.empty('battery', 'BATERIA'),
        ];
    final mppts = channels.where((channel) => channel.id.startsWith('mppt')).toList();
    double? sum(Iterable<double?> values) {
      final valid = values.whereType<double>().toList();
      return valid.isEmpty ? null : valid.reduce((a, b) => a + b);
    }

    final battery = channels.last;
    return _Card(
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        const _SectionTitle('Energia — corrente, tensão e potência'),
        LayoutBuilder(builder: (context, constraints) {
          const gap = 10.0;
          // Quatro cards lado a lado só quando cada um tem ~190 dp.
          final columns = constraints.maxWidth >= 780 ? 4 : 2;
          final width = (constraints.maxWidth - gap * (columns - 1)) / columns;
          return Wrap(spacing: gap, runSpacing: gap, children: [
            for (final channel in channels)
              SizedBox(
                width: width,
                child: _MetricCard(
                    title: channel.label, current: channel.currentA, voltage: channel.voltageV, power: channel.powerW),
              ),
          ]);
        }),
        const SizedBox(height: 10),
        LayoutBuilder(builder: (context, constraints) {
          const gap = 10.0;
          final columns = constraints.maxWidth >= 560 ? 3 : 1;
          final width = (constraints.maxWidth - gap * (columns - 1)) / columns;
          return Wrap(spacing: gap, runSpacing: gap, children: [
            SizedBox(
              width: width,
              child: _MetricCard(
                title: 'GERAÇÃO SOLAR',
                current: sum(mppts.map((channel) => channel.currentA)),
                voltage: null,
                power: sum(mppts.map((channel) => channel.powerW)),
                accent: Palette.sun,
              ),
            ),
            SizedBox(
              width: width,
              child: _MetricCard(
                  title: 'BATERIA · RESUMO',
                  current: battery.currentA,
                  voltage: battery.voltageV,
                  power: battery.powerW,
                  accent: Palette.green),
            ),
            SizedBox(
              width: width,
              child: _MetricCard(
                  title: 'MOTOR · VESC',
                  current: motor.inputCurrentA,
                  voltage: motor.inputVoltageV,
                  power: motor.inputPowerW,
                  accent: Palette.brand),
            ),
          ]);
        }),
        const SizedBox(height: 14),
        const _SectionTitle('Fluxo de arrefecimento'),
        Row(children: [
          Expanded(child: _FlowTile(label: 'FLUXO A', value: channels[0].flowLpm)),
          const SizedBox(width: 10),
          Expanded(child: _FlowTile(label: 'FLUXO B', value: channels[1].flowLpm)),
        ]),
      ]),
    );
  }
}

class _MetricCard extends StatelessWidget {
  const _MetricCard(
      {required this.title, required this.current, required this.voltage, required this.power, this.accent});

  final String title;
  final double? current;
  final double? voltage;
  final double? power;
  final Color? accent;

  @override
  Widget build(BuildContext context) {
    final stale = current == null && voltage == null && power == null;
    return Opacity(
      opacity: stale ? .6 : 1,
      child: Container(
        padding: const EdgeInsets.all(12),
        decoration: BoxDecoration(
          color: Palette.surface2,
          borderRadius: BorderRadius.circular(10),
          border: Border.all(color: Palette.border),
        ),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Text(title,
              style: TextStyle(
                  fontSize: 11, fontWeight: FontWeight.w700, letterSpacing: 1, color: accent ?? Palette.brandDark)),
          const SizedBox(height: 8),
          Row(children: [
            _MetricValue('corrente', fmt(current, 2, 'A')),
            _MetricValue('tensão', fmt(voltage, 2, 'V')),
            _MetricValue('potência', fmt(power, 1, 'W')),
          ]),
        ]),
      ),
    );
  }
}

class _MetricValue extends StatelessWidget {
  const _MetricValue(this.label, this.value);

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) => Expanded(
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          FittedBox(
            fit: BoxFit.scaleDown,
            alignment: Alignment.centerLeft,
            child: Text(label.toUpperCase(), maxLines: 1, style: const TextStyle(fontSize: 10, color: Palette.textDim)),
          ),
          FittedBox(
            fit: BoxFit.scaleDown,
            alignment: Alignment.centerLeft,
            child: Text(value,
                style: const TextStyle(
                    fontSize: 15, fontWeight: FontWeight.w700, fontFeatures: [FontFeature.tabularFigures()])),
          ),
        ]),
      );
}

class _FlowTile extends StatelessWidget {
  const _FlowTile({required this.label, required this.value});

  final String label;
  final double? value;

  @override
  Widget build(BuildContext context) => Container(
        padding: const EdgeInsets.all(12),
        decoration: BoxDecoration(
          color: Palette.surface2,
          borderRadius: BorderRadius.circular(10),
          border: Border.all(color: Palette.border),
        ),
        child: Row(children: [
          Text(label, style: const TextStyle(fontSize: 11, fontWeight: FontWeight.w700, color: Palette.textDim)),
          const Spacer(),
          Text(fmt(value, 2, 'L/min'),
              style: TextStyle(
                  fontSize: 20, fontWeight: FontWeight.w700, color: value == null ? Palette.textFaint : Palette.sea)),
        ]),
      );
}

// ---------------------------------------------------------------------------
// Temperaturas (10 DS18B20) com as mesmas faixas de alarme do web_dash.
// [AJUSTE] Faixas em _TempTile: 15–35 °C nos 2 primeiros, 10–45 °C nos demais.
// ---------------------------------------------------------------------------
class _TemperaturePanel extends StatelessWidget {
  const _TemperaturePanel({required this.snapshot, required this.columns});

  final TelemetrySnapshot? snapshot;
  final int columns;

  @override
  Widget build(BuildContext context) {
    final values = snapshot?.temperatures ?? List<double?>.filled(10, null);
    return _Card(
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        const _SectionTitle('Sensores de temperatura · 10 canais'),
        LayoutBuilder(builder: (context, constraints) {
          const gap = 8.0;
          final width = (constraints.maxWidth - gap * (columns - 1)) / columns;
          return Wrap(spacing: gap, runSpacing: gap, children: [
            for (var index = 0; index < 10; index++)
              SizedBox(width: width, child: _TempTile(index: index, value: values[index])),
          ]);
        }),
      ]),
    );
  }
}

class _TempTile extends StatelessWidget {
  const _TempTile({required this.index, required this.value});

  final int index;
  final double? value;

  @override
  Widget build(BuildContext context) {
    final low = index < 2 ? 15.0 : 10.0;
    final high = index < 2 ? 35.0 : 45.0;
    final (color, badge) = value == null
        ? (Palette.textFaint, 'SEM DADOS')
        : value! < low
            ? (Palette.cold, 'BAIXA')
            : value! > high
                ? (Palette.danger, 'ALTA')
                : (Palette.ok, 'OK');
    return Container(
      padding: const EdgeInsets.symmetric(vertical: 10, horizontal: 6),
      decoration: BoxDecoration(
        color: Palette.surface2,
        borderRadius: BorderRadius.circular(10),
        border: Border(top: BorderSide(color: color, width: 3)),
      ),
      child: Column(children: [
        Text(tempLabels[index], style: const TextStyle(fontSize: 10, fontWeight: FontWeight.w700, color: Palette.textDim)),
        const SizedBox(height: 4),
        Text(fmt(value, 1, '°C'), style: TextStyle(fontSize: 17, fontWeight: FontWeight.w700, color: color)),
        const SizedBox(height: 2),
        Text(badge, style: TextStyle(fontSize: 9, fontWeight: FontWeight.w700, color: color, letterSpacing: .8)),
      ]),
    );
  }
}

/// Rodapé: schema, contadores e avisos das flags de erro.
class _FooterStatus extends StatelessWidget {
  const _FooterStatus({required this.link});

  final DashboardLink link;

  @override
  Widget build(BuildContext context) {
    final snapshot = link.snapshot;
    final flags = snapshot?.errorFlags ?? 0;
    final notes = <String>[
      if (flags & ErrorFlag.sdOffline != 0) 'microSD offline',
      if (flags & ErrorFlag.ina226Error != 0) 'INA226 com erro',
      if (flags & ErrorFlag.vescStale != 0) 'VESC sem dados',
      if (flags & ErrorFlag.vescFault != 0) 'VESC em falha (código ${link.motor.fault})',
      if (flags & ErrorFlag.supply5vOutOfRange != 0) '5 V dos ACS712 fora da faixa',
      if (flags & ErrorFlag.socUncalibrated != 0) 'SoC não calibrado',
    ];
    return Text(
      [
        'Schema ${snapshot?.schemaVersion ?? '--'}',
        'frames ${link.framesReceived}',
        'CRC ${link.crcErrors}',
        if (notes.isNotEmpty) notes.join(' · '),
        'Equipe Babitonga · UFSC Centro Tecnológico de Joinville',
      ].join('   ·   '),
      textAlign: TextAlign.center,
      style: TextStyle(fontSize: 11, color: notes.isEmpty ? Palette.textFaint : Palette.danger),
    );
  }
}
