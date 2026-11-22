# Sistema de Telemetria — Equipe Babitonga

Telemetria da embarcação solar da Equipe Babitonga (UFSC · CTJ · Joinville):
firmwares, dashboard web e APK de bordo. O backup do firmware legado fica fora
desta pasta.

## Componentes

- `Datalogger/`: RP2040; sensores, energia, GPS, microSD e frames UART.
- `TransmiterLoRa/`: Heltec embarcada; valida frames UART e transmite por LoRa.
- `ReceiverLoRa/`: Heltec em terra; valida frames LoRa e publica RSSI/SNR.
- `DashboardAntenna/`: ESP32-C3; ponte UART framed e BLE.
- `web_dash/`: dashboard web Flask; decodifica o frame de telemetria (binário ou linha
  `REV_B_FRAME`) vindo do `ReceiverLoRa`. Ver `web_dash/README.md`.
- `dashboard_babitonga/`: APK Flutter de bordo (tablet) conectado por BLE ao
  `DashboardAntenna`.

## Marcadores nos comentários do código

Todo o código tem comentários em português. Pontos de manutenção usam
marcadores fáceis de buscar (ex.: `grep -rn "\[CALIBRAÇÃO\]"`):

| Marcador | Significado |
|---|---|
| `[AJUSTE]` | Valor a conferir/alterar antes de compilar ou instalar (porta COM, velocidade serial, pinos, limites de alarme) |
| `[CALIBRAÇÃO]` | Constante medida em bancada (shunt, divisores, zero dos ACS712, capacidade da bateria, pares de polos) |
| `[NOVO SENSOR]` | Onde acrescentar um sensor, canal, card ou campo |
| `[CONTRATO]` | Mudar exige atualizar a especificação e os decodificadores (frame, payload, CSV, UUIDs) |

As calibrações do Datalogger ficam todas em `Datalogger/config/rev_b_config.h`.

## Build

Cada firmware possui um Makefile próprio. O alvo é `compile`, não `build`:

```text
cd Datalogger
make compile
make upload PORT=COMx

cd ../TransmiterLoRa
make compile

cd ../ReceiverLoRa
make compile

cd ../DashboardAntenna
make compile
```

Os diretórios `build/` são artefatos locais e não fazem parte do código-fonte.

## Estado de validação

Os testes host-side estão em `*/tests/` e validam os contratos básicos:

```text
python -m unittest discover -s Datalogger/tests
```

(repita para `TransmiterLoRa`, `ReceiverLoRa`, `DashboardAntenna` e `web_dash`).
A validação elétrica e o HIL de LoRa/BLE/SD/GPS continuam pendentes.
