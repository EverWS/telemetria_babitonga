// Paleta da Equipe Babitonga (mesmas cores do web_dash/templates/index.html).
// [AJUSTE] Mudar uma cor aqui muda o app inteiro.
import 'package:flutter/material.dart';

class Palette {
  static const bg = Color(0xFFF2F6F8);
  static const surface = Color(0xFFFFFFFF);
  static const surface2 = Color(0xFFF6FAFB);
  static const border = Color(0xFFDCE6EA);
  static const text = Color(0xFF1E2C32);
  static const textDim = Color(0xFF5E6A70);
  static const textFaint = Color(0xFF8C989D);

  static const sky = Color(0xFF96CEF2);
  static const water = Color(0xFF5DBAD1);
  static const brand = Color(0xFF3DB3AE);
  static const brandDark = Color(0xFF16807A);
  static const sea = Color(0xFF00A88A);
  static const green = Color(0xFF00A35A);
  static const sun = Color(0xFFF2AE00);

  static const ok = Color(0xFF078A4E);
  static const cold = Color(0xFF2C84B5);
  static const warn = Color(0xFFB57900);
  static const danger = Color(0xFFC93C3C);

  static ThemeData theme() {
    final scheme = ColorScheme.fromSeed(
      seedColor: brand,
      primary: brandDark,
      surface: surface,
      brightness: Brightness.light,
    );
    return ThemeData(
      useMaterial3: true,
      colorScheme: scheme,
      scaffoldBackgroundColor: bg,
      // Fonte do sistema (o barco pode estar sem internet): nada é baixado.
      textTheme: Typography.blackMountainView.apply(bodyColor: text, displayColor: text),
    );
  }
}
