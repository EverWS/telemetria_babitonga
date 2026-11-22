import 'package:dashboard_babitonga/ble/dashboard_link.dart';
import 'package:dashboard_babitonga/main.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  testWidgets('KPI strip order and demo data render on a 10.1" landscape tablet', (tester) async {
    tester.view.physicalSize = const Size(2560, 1600);
    tester.view.devicePixelRatio = 2.0;
    addTearDown(tester.view.reset);

    final link = DashboardLink(); // not started: no Bluetooth platform calls
    await tester.pumpWidget(BabitongaApp(link: link));

    // Without data every card shows "--", never zero.
    expect(find.text('VELOCIDADE'), findsOneWidget);
    expect(find.text('Sem fix GPS'), findsOneWidget);

    final labels = ['VELOCIDADE', 'ROTAÇÃO DO MOTOR', 'TEMPERATURA DO MOTOR', 'CARGA DA BATERIA', 'GPS', 'SINAL BLE'];
    final positions = labels.map((label) => tester.getTopLeft(find.text(label)).dx).toList();
    expect(positions, [...positions]..sort());

    await link.setDemo(true);
    await tester.pump();
    expect(find.text('Modo demonstração'), findsOneWidget);
    expect(find.text('MPPT C'), findsOneWidget);
    expect(find.text('MOTOR · VESC'), findsOneWidget);
    expect(tester.takeException(), isNull);

    await link.setDemo(false);
    link.dispose();
  });
}
