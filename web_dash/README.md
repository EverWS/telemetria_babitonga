# Dashboard web Rev B

O dashboard lê a telemetria Rev B pela serial conectada ao receptor LoRa. O
servidor aceita tanto o frame binário (`SOF A5 5A ... CRC16`) quanto a linha
diagnóstica `REV_B_FRAME ... payload=HEX` emitida pelo `ReceiverLoRa`.

## Execução local

No PowerShell:

```powershell
$env:TELEMETRIA_SERIAL_PORT = "COM8"
python web_dash/webserver.py
```

Variáveis opcionais:

- `TELEMETRIA_BAUD_RATE` — padrão `115200`;
- `TELEMETRIA_SERIAL_RECONNECT_SECONDS` — padrão `3`;
- `TELEMETRIA_GPS_TRACK_LIMIT` — padrão `180` pontos;
- `TELEMETRIA_FRAME_TIMEOUT_SECONDS` — padrão `1.5`.

## Interface

Tema claro com a paleta da logo da Equipe Babitonga
(`static/img/logo_babitonga.png`): céu `#96CEF2`, água `#5DBAD1`, teal
`#3DB3AE`, mar `#00A88A`, verde `#00A35A` e sol `#F2AE00`. As cores ficam em
variáveis CSS no topo de `templates/index.html`. Abaixo dos indicadores ficam
atitude do casco, mapa, temperaturas, energia e fluxo.

### Indicadores do topo (esquerda → direita)

| Card | Origem do dado | Situação no payload Rev B (schema 3, M8) |
|---|---|---|
| Velocidade (nós) | `gps.speed_kn`; sem ele, estimada pela trilha GPS (haversine + média móvel) | Transmitido (RMC do NEO-6M) |
| Rotação do motor (RPM) | `motor.rpm` | Bloco reservado; `nan` até o gateway VESC (M9) |
| Temperatura do motor (°C) | `motor.temperature_c` (alarme > `MOTOR_TEMP_ALARM_C`, 80 °C) | Bloco reservado; `nan` até o gateway VESC (M9) |
| Carga da bateria (%) | `battery_state.soc_pct` (alerta < `BATTERY_SOC_LOW_PCT`, 20 %) | Transmitido (contagem de Ah no RP2040, ADR-005) |
| GPS | satélites, fix e HDOP | Disponível |
| Sinal LoRa | RSSI, SNR e perdas do `ReceiverLoRa` | Disponível |

Campos ausentes aparecem como `--` com o motivo, nunca como zero. O modo demo
preenche todos os cards com valores simulados.

## Mapa

O mapa usa Leaflet, sem chave ou cobrança de API. Pelo seletor de camadas é
possível escolher o mapa base (OpenStreetMap, Esri ou **Satélite** Esri World
Imagery) e ligar/desligar a **carta náutica OpenSeaMap** (boias, balizas,
canais; visível a partir do zoom 9). A escolha fica salva no navegador. O botão
**SEGUIR** volta a acompanhar o barco depois que o mapa é arrastado. O mapa
ocupa toda a área do card GPS: em telas largas acompanha a altura do card de
inclinação; em tablet usa proporção 4:3, em celular 1:1 e em celular deitado
16:9, sempre limitado à altura da tela. Os tiles
exigem acesso à internet no navegador; sem rede, o restante do dashboard
continua disponível.

O processo mantém somente o último snapshot e uma trilha GPS limitada. Não
grava linhas seriais nem acumula histórico indefinido em memória.

## Visualização sem hardware

Para abrir pelo Live Server e visualizar casco, mapa e cards preenchidos:

```text
web_dash/templates/index.html?demo=1
```

Quando o arquivo é aberto pelo caminho `templates/index.html`, o modo DEMO é
ativado automaticamente. No Flask, use `?demo=1` somente para demonstração; sem
esse parâmetro o dashboard usa a serial real.
