# dashboard_babitonga — APK de bordo

Dashboard Flutter para o tablet do barco (Samsung Galaxy Note 10.1, paisagem).
Conecta por **BLE ao `DashboardAntenna`** (ESP32-C3) e mostra as mesmas
informações do `web_dash`, na mesma paleta da logo da Equipe Babitonga.

## Protocolo

- Frames Rev B (`docs/specs/TELEMETRY_FRAME.md`), com CRC16 validado, decoder
  reiniciado a cada notificação e `MAX_PAYLOAD = 244`.
- Payloads de telemetria nos schemas 1, 2 e 3, todos decodificados para o mesmo
  modelo (`lib/protocol/telemetry.dart`). Campos ausentes ou `nan` viram `null`
  e aparecem como `--`, nunca como zero.
- Frame `0x08` (status do VESC, ADR-004): enquanto chegar com menos de 1,5 s,
  prevalece sobre o bloco `motor` do payload de 1 Hz.
- O app pede MTU 517 e avisa se o negociado for menor que 257
  (`docs/specs/DASHBOARD_BLE.md`).

## Interface

Topo, da esquerda para a direita: velocidade (nós), rotação do motor, temperatura
do motor, carga da bateria, GPS e sinal. A bordo não existe RSSI LoRa (o barco
só transmite), por isso o último card mostra o **sinal BLE** do link com o C3.

Abaixo do topo: GPS com bússola de rumo, atitude do casco, energia (MPPT A/B/C,
bateria, geração solar, motor), fluxo de arrefecimento e as 10 temperaturas.

O botão **DEMO** gera frames schema 3 codificados como o firmware e os passa pelo
mesmo decodificador. Serve para testar o tablet sem hardware.

## Desenvolvimento

```text
flutter pub get
flutter analyze
flutter test
```

Para gerar o `.apk`, aceite antes as licenças do Android SDK
(`flutter doctor --android-licenses`). Depois:

```text
flutter build apk --release
```

`minSdk = 21` (Android 5.0). O Note 10.1 **2014 Edition** (SM-P600, Android
5.1.1) é compatível. O Note 10.1 original (GT-N8000, Android 4.x) não é.
Confirme também que a versão do Flutter usada ainda gera APK para a API 21/22.

Fontes do sistema (sem download em tempo de execução) e logo local em
`assets/`: o app funciona sem internet a bordo.
