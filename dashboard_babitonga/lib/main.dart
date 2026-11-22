import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';

import 'ble/dashboard_link.dart';
import 'ui/dashboard_page.dart';
import 'ui/palette.dart';

/// Ponto de entrada do APK: cria a conexão BLE e abre o dashboard.
void main() {
  WidgetsFlutterBinding.ensureInitialized();
  // Tablet (Galaxy Note 10.1) fixo na horizontal.
  SystemChrome.setPreferredOrientations([DeviceOrientation.landscapeLeft, DeviceOrientation.landscapeRight]);
  runApp(BabitongaApp(link: DashboardLink()..start()));
}

/// Raiz do app: tema claro da equipe + provedor da conexão BLE.
class BabitongaApp extends StatelessWidget {
  const BabitongaApp({super.key, required this.link});

  final DashboardLink link;

  @override
  Widget build(BuildContext context) => ChangeNotifierProvider<DashboardLink>.value(
        value: link,
        child: MaterialApp(
          title: 'Babitonga · Telemetria',
          theme: Palette.theme(),
          home: const DashboardPage(),
          debugShowCheckedModeBanner: false,
        ),
      );
}
