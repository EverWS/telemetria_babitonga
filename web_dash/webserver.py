"""Gateway local de telemetria Rev B (servidor do web_dash).

Lê a USB do ReceiverLoRa (Heltec em terra), decodifica os frames Rev B e envia
os dados ao navegador por Socket.IO. Guarda só o último snapshot e uma trilha
GPS curta (memória limitada). Aceita tanto o frame binário da UART quanto a
linha de diagnóstico ``REV_B_FRAME ... payload=HEX`` que o ReceiverLoRa
imprime, então bancada e campo usam o mesmo dashboard.

Como executar (PowerShell):
    $env:TELEMETRIA_SERIAL_PORT = "COM8"     # [AJUSTE] porta do ReceiverLoRa
    python web_dash/webserver.py              # abre em http://localhost:5000

Variáveis de ambiente (todas opcionais, ver web_dash/README.md):
    TELEMETRIA_SERIAL_PORT              porta serial ("" desativa a serial)
    TELEMETRIA_BAUD_RATE                115200 (igual ao Serial.begin do receptor)
    TELEMETRIA_SERIAL_RECONNECT_SECONDS espera entre tentativas de reconexão
    TELEMETRIA_FRAME_TIMEOUT_SECONDS    silêncio que descarta um frame parcial
    TELEMETRIA_GPS_TRACK_LIMIT          pontos guardados na trilha do mapa
"""

from __future__ import annotations

import math
import os
import re
import struct
import threading
import time
from collections import deque
from dataclasses import dataclass
from typing import Any, Optional

import serial  # pacote "pyserial"
from flask import Flask, jsonify, render_template
from flask_socketio import SocketIO, emit


# ---------------------------------------------------------------------------
# Configuração (lida do ambiente). [AJUSTE] Os padrões valem para a bancada.
# ---------------------------------------------------------------------------
SERIAL_PORT = os.getenv("TELEMETRIA_SERIAL_PORT", "COM8").strip()
BAUD_RATE = int(os.getenv("TELEMETRIA_BAUD_RATE", "115200"))
SERIAL_RECONNECT_SECONDS = float(os.getenv("TELEMETRIA_SERIAL_RECONNECT_SECONDS", "3"))
FRAME_TIMEOUT_SECONDS = float(os.getenv("TELEMETRIA_FRAME_TIMEOUT_SECONDS", "1.5"))
GPS_TRACK_LIMIT = max(2, int(os.getenv("TELEMETRIA_GPS_TRACK_LIMIT", "180")))

# [CONTRATO] Constantes do frame (TELEMETRY_FRAME.md). MAX_PAYLOAD precisa ser
# igual ao dos 4 firmwares e do APK (teste de paridade em ReceiverLoRa/tests).
MAX_PAYLOAD = 244  # payload schema 3 (TELEMETRY_FRAME.md)
FRAME_VERSION = 1
FRAME_TYPE_TELEMETRY = 1
SOF = b"\xA5\x5A"

# Servidor web + Socket.IO (modo threading: a leitura serial roda em paralelo).
app = Flask(__name__)
app.config["SECRET_KEY"] = os.getenv("TELEMETRIA_SECRET_KEY", "revb-local-dashboard")
socketio = SocketIO(app, cors_allowed_origins="*", async_mode="threading")
stop_event = threading.Event()  # sinaliza o fim para a thread da serial


@dataclass(frozen=True)
class Frame:
    """Um frame Rev B válido (CRC conferido), com a qualidade do enlace se vier do receptor."""

    frame_type: int
    sequence: int
    payload: bytes
    rssi: Optional[int] = None          # dBm do pacote LoRa
    snr: Optional[float] = None         # dB do pacote LoRa
    sequence_lost: Optional[int] = None  # perdas acumuladas contadas pelo receptor


def crc16_ccitt(data: bytes) -> int:
    """CRC16-CCITT-FALSE (polinômio 0x1021, início 0xFFFF), igual ao firmware."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


class FrameDecoder:
    """Decodificador limitado do frame Rev B (SOF/versão/tipo/seq/len/payload/CRC).

    Recebe byte a byte; um silêncio maior que ``timeout_seconds`` no meio do
    frame descarta o frame parcial (evita engolir o frame seguinte).
    """

    # Estados da máquina de estados.
    WAIT_SOF_0 = 0
    WAIT_SOF_1 = 1
    HEADER = 2
    PAYLOAD = 3
    CRC = 4

    def __init__(self, timeout_seconds: float = FRAME_TIMEOUT_SECONDS):
        self.timeout_seconds = timeout_seconds
        self.crc_errors = 0
        self.framing_errors = 0
        self._state = self.WAIT_SOF_0
        self._header = bytearray()
        self._payload = bytearray()
        self._crc = bytearray()
        self._payload_length = 0
        self._last_byte_at: Optional[float] = None

    def _reset_parser(self) -> None:
        """Volta a procurar o SOF (mantém o relógio do último byte)."""
        self._state = self.WAIT_SOF_0
        self._header.clear()
        self._payload.clear()
        self._crc.clear()
        self._payload_length = 0

    def reset(self) -> None:
        """Reinício completo (usado ao reconectar a serial)."""
        self._reset_parser()
        self._last_byte_at = None

    def feed(self, byte: int, now: Optional[float] = None) -> Optional[Frame]:
        """Entrega um byte; retorna o Frame quando um frame completo e válido termina."""
        now = time.monotonic() if now is None else now
        # Silêncio longo no meio de um frame: descarta e conta erro de framing.
        if self._last_byte_at is not None and now - self._last_byte_at > self.timeout_seconds:
            if self._state != self.WAIT_SOF_0:
                self.framing_errors += 1
            self._reset_parser()
        self._last_byte_at = now
        byte &= 0xFF

        if self._state == self.WAIT_SOF_0:
            if byte == SOF[0]:
                self._state = self.WAIT_SOF_1
            return None

        if self._state == self.WAIT_SOF_1:
            if byte == SOF[1]:
                self._state = self.HEADER
                self._header.clear()
            else:
                self.framing_errors += 1
                # Um novo 0xA5 pode ser o início verdadeiro do frame.
                self._state = self.WAIT_SOF_1 if byte == SOF[0] else self.WAIT_SOF_0
            return None

        if self._state == self.HEADER:
            self._header.append(byte)
            if len(self._header) < 6:
                return None
            # Cabeçalho: versão (B), tipo (B), seq16 (H), len16 (H), little-endian.
            version, frame_type, _, payload_length = struct.unpack("<BBHH", self._header)
            # [CONTRATO] Tipos aceitos 1..7; o 0x08 (VESC) não vem pelo LoRa.
            if version != FRAME_VERSION or not 1 <= frame_type <= 7 or payload_length > MAX_PAYLOAD:
                self.framing_errors += 1
                self._reset_parser()
                return None
            self._payload_length = payload_length
            self._state = self.CRC if payload_length == 0 else self.PAYLOAD
            return None

        if self._state == self.PAYLOAD:
            self._payload.append(byte)
            if len(self._payload) == self._payload_length:
                self._state = self.CRC
            return None

        # Estado CRC: 2 bytes little-endian.
        self._crc.append(byte)
        if len(self._crc) < 2:
            return None
        received_crc = struct.unpack("<H", self._crc)[0]
        expected_crc = crc16_ccitt(bytes(self._header) + bytes(self._payload))
        version, frame_type, sequence, _ = struct.unpack("<BBHH", self._header)
        decoded_payload = bytes(self._payload)
        self._reset_parser()
        if received_crc != expected_crc:
            self.crc_errors += 1  # corrompido: nunca chega ao dashboard
            return None
        return Frame(frame_type, sequence, decoded_payload)


# [CONTRATO] Formato da linha publicada pelo ReceiverLoRa (publishFrame()).
_RECEIVER_FRAME = re.compile(
    r"^REV_B_FRAME\s+type=(?P<type>\d+)\s+seq=(?P<seq>\d+)\s+len=(?P<len>\d+)\s+"
    r"rssi=(?P<rssi>-?\d+)\s+snr=(?P<snr>-?[\d.]+)\s+lost=(?P<lost>\d+)\s+"
    r"payload=(?P<payload>[0-9A-Fa-f]*)$"
)


def parse_receiver_line(line: str) -> Optional[Frame]:
    """Converte uma linha ``REV_B_FRAME`` em Frame (com RSSI/SNR); None se inválida."""
    match = _RECEIVER_FRAME.match(line.strip())
    if not match:
        return None
    values = match.groupdict()
    payload = bytes.fromhex(values["payload"])
    # O tamanho declarado precisa bater com os bytes recebidos.
    if int(values["len"]) != len(payload) or len(payload) > MAX_PAYLOAD:
        return None
    return Frame(
        frame_type=int(values["type"]),
        sequence=int(values["seq"]) & 0xFFFF,
        payload=payload,
        rssi=int(values["rssi"]),
        snr=float(values["snr"]),
        sequence_lost=int(values["lost"]),
    )


def _finite(value: float) -> Optional[float]:
    """nan/inf viram None (JSON null) — nunca zero."""
    return float(value) if math.isfinite(value) else None


def _channel(identifier: str, label: str, values: tuple[float, ...], flags: int) -> dict[str, Any]:
    """Monta o dicionário de um canal elétrico (V, I, P, Wh, fluxo, flags)."""
    voltage, current, power, energy, flow = values
    return {
        "id": identifier,
        "label": label,
        "voltage_v": _finite(voltage),
        "current_a": _finite(current),
        "power_w": _finite(power),
        "energy_wh": _finite(energy),
        "flow_l_min": _finite(flow),
        "valid_flags": flags,
    }


# [CONTRATO] Ordem dos canais no payload schema 3 e códigos de soc_source.
CHANNEL_LABELS = (("mppt_a", "MPPT A"), ("mppt_b", "MPPT B"), ("mppt_c", "MPPT C"), ("battery", "BATERIA"))
SOC_SOURCES = {0: "none", 1: "coulomb", 2: "voltage_estimate", 3: "full_sync"}
PAYLOAD_SCHEMA_2_SIZE = 188
PAYLOAD_SCHEMA_3_SIZE = 240


def _empty_channel(identifier: str, label: str) -> dict[str, Any]:
    """Canal sem dados (todos os valores None)."""
    return _channel(identifier, label, (math.nan,) * 5, 0)


def _empty_motor() -> dict[str, Any]:
    """Bloco do motor sem dados (schemas sem VESC ou VESC stale)."""
    return {
        "valid": False,
        "fault": 0,
        "age_ms": None,
        "rpm": None,
        "temperature_c": None,
        "fet_temperature_c": None,
        "input_current_a": None,
        "input_voltage_v": None,
        "duty_pct": None,
    }


def _empty_battery_state() -> dict[str, Any]:
    """Estado de carga desconhecido."""
    return {"soc_pct": None, "ah_from_full": None, "soc_source": "none"}


def _gps(valid: int, fix: int, satellites: int, latitude: float, longitude: float, hdop: float,
         speed_kn: float = math.nan, course_deg: float = math.nan) -> dict[str, Any]:
    """Monta o bloco GPS (velocidade e rumo só existem no schema 3)."""
    return {
        "valid": bool(valid),
        "latitude": _finite(latitude),
        "longitude": _finite(longitude),
        "hdop": _finite(hdop),
        "satellites": satellites,
        "fix_quality": fix,
        "speed_kn": _finite(speed_kn),
        "course_deg": _finite(course_deg),
    }


def _imu(valid: int, x: float, y: float, z: float, pitch: float, roll: float) -> dict[str, Any]:
    """Monta o bloco da IMU (aceleração em g e ângulos em graus)."""
    return {
        "valid": bool(valid),
        "accel_g": {"x": _finite(x), "y": _finite(y), "z": _finite(z)},
        "pitch_deg": _finite(pitch),
        "roll_deg": _finite(roll),
    }


def _temperatures(values: tuple[float, ...], count: int) -> list[Optional[float]]:
    """Sempre 10 posições; acima de ``count`` (sensores encontrados) fica None."""
    count = max(0, min(count, 10))
    return [_finite(value) for value in values[:count]] + [None] * (10 - count)


def _decode_schema_1(payload: bytes) -> dict[str, Any]:
    """Schema 1 (legado, 32 bytes): só potências, 2 fluxos e 1 temperatura."""
    timestamp, power_a, power_b, power_battery, flow_a, flow_b, temperature, flags = struct.unpack("<I6fI", payload)
    return {
        "schema_version": 1,
        "timestamp_ms": timestamp,
        "channel_count": 3,
        "channels": [
            _channel("mppt_a", "MPPT A", (math.nan, math.nan, power_a, math.nan, flow_a), flags & 0xFF),
            _channel("mppt_b", "MPPT B", (math.nan, math.nan, power_b, math.nan, flow_b), (flags >> 8) & 0xFF),
            _empty_channel("mppt_c", "MPPT C"),
            _channel("battery", "BATERIA", (math.nan, math.nan, power_battery, math.nan, math.nan), (flags >> 16) & 0xFF),
        ],
        "temperatures": [_finite(temperature)] + [None] * 9,
        "gps": _gps(0, 0, 0, math.nan, math.nan, math.nan),
        "imu": _imu(0, *(math.nan,) * 5),
        "battery_state": _empty_battery_state(),
        "motor": _empty_motor(),
        "supply_5v_v": None,
        "error_flags": 0,
    }


def _decode_schema_2(payload: bytes) -> dict[str, Any]:
    """Schema 2 (188 bytes): MPPT A/B, bateria e reserva; sem MPPT C, SoC e motor."""
    _, channel_count, temperature_count, _, timestamp = struct.unpack_from("<BBBBI", payload, 0)
    raw = [struct.unpack_from("<5fI", payload, 8 + 24 * index) for index in range(4)]
    temperatures = struct.unpack_from("<10f", payload, 104)
    gps_valid, fix_quality, satellites, _, latitude, longitude, hdop = struct.unpack_from("<BBBB3f", payload, 144)
    imu_valid = struct.unpack_from("<B", payload, 160)[0]
    imu_values = struct.unpack_from("<5f", payload, 164)
    (error_flags,) = struct.unpack_from("<I", payload, 184)
    # Ordem no schema 2: MPPT_A, MPPT_B, BATTERY, reserva (sem MPPT_C).
    return {
        "schema_version": 2,
        "timestamp_ms": timestamp,
        "channel_count": min(channel_count, 4),
        "channels": [
            _channel("mppt_a", "MPPT A", raw[0][:5], raw[0][5]),
            _channel("mppt_b", "MPPT B", raw[1][:5], raw[1][5]),
            _empty_channel("mppt_c", "MPPT C"),
            _channel("battery", "BATERIA", raw[2][:5], raw[2][5]),
        ],
        "temperatures": _temperatures(temperatures, temperature_count),
        "gps": _gps(gps_valid, fix_quality, satellites, latitude, longitude, hdop),
        "imu": _imu(imu_valid, *imu_values),
        "battery_state": _empty_battery_state(),
        "motor": _empty_motor(),
        "supply_5v_v": None,
        "error_flags": error_flags,
    }


def _decode_schema_3(payload: bytes) -> dict[str, Any]:
    """Schema 3 (240 bytes). [CONTRATO] Offsets da tabela em TELEMETRY_FRAME.md.

    [NOVO SENSOR] Um campo novo no payload precisa ser lido aqui (no offset da
    spec) e acrescentado ao dicionário retornado; depois, exibido no index.html.
    """
    _, channel_count, temperature_count, _, timestamp = struct.unpack_from("<BBBBI", payload, 0)
    channels = []
    for index, (identifier, label) in enumerate(CHANNEL_LABELS):
        values = struct.unpack_from("<5fI", payload, 8 + 24 * index)  # 4 canais de 24 bytes a partir do 8
        channels.append(_channel(identifier, label, values[:5], values[5]))
    temperatures = struct.unpack_from("<10f", payload, 104)
    gps_valid, fix_quality, satellites, _, latitude, longitude, hdop, speed_kn, course_deg = struct.unpack_from(
        "<BBBB5f", payload, 144)
    imu_valid = struct.unpack_from("<B", payload, 168)[0]
    imu_values = struct.unpack_from("<5f", payload, 172)
    soc_pct, ah_from_full, soc_source = struct.unpack_from("<ffB", payload, 192)
    motor_valid, vesc_fault, motor_age_ms = struct.unpack_from("<BBH", payload, 204)
    rpm, motor_temp, fet_temp, input_current, input_voltage, duty = struct.unpack_from("<6f", payload, 208)
    supply_5v, error_flags = struct.unpack_from("<fI", payload, 232)
    motor_ok = bool(motor_valid)
    return {
        "schema_version": 3,
        "timestamp_ms": timestamp,
        "channel_count": min(channel_count, 4),
        "channels": channels,
        "temperatures": _temperatures(temperatures, temperature_count),
        "gps": _gps(gps_valid, fix_quality, satellites, latitude, longitude, hdop, speed_kn, course_deg),
        "imu": _imu(imu_valid, *imu_values),
        "battery_state": {
            "soc_pct": _finite(soc_pct),
            "ah_from_full": _finite(ah_from_full),
            "soc_source": SOC_SOURCES.get(soc_source, "none"),
        },
        # Dado do motor desatualizado nunca aparece como valor atual.
        "motor": {
            "valid": motor_ok,
            "fault": vesc_fault,
            "age_ms": motor_age_ms,
            "rpm": _finite(rpm) if motor_ok else None,
            "temperature_c": _finite(motor_temp) if motor_ok else None,
            "fet_temperature_c": _finite(fet_temp) if motor_ok else None,
            "input_current_a": _finite(input_current) if motor_ok else None,
            "input_voltage_v": _finite(input_voltage) if motor_ok else None,
            "duty_pct": _finite(duty) if motor_ok else None,
        },
        "supply_5v_v": _finite(supply_5v),
        "error_flags": error_flags,
    }


def decode_telemetry_payload(payload: bytes) -> Optional[dict[str, Any]]:
    """Decodifica os payloads schema 1, 2 e 3 em um único modelo JSON.

    Todos os schemas geram o mesmo formato (canais MPPT A/B/C + bateria, GPS,
    IMU, battery_state, motor); campos que o schema não traz ficam ``None``.
    """
    if len(payload) == 32:
        return _decode_schema_1(payload)
    if not payload:
        return None
    if payload[0] == 2 and len(payload) >= PAYLOAD_SCHEMA_2_SIZE:
        return _decode_schema_2(payload)
    if payload[0] == 3 and len(payload) >= PAYLOAD_SCHEMA_3_SIZE:
        return _decode_schema_3(payload)
    return None  # versão desconhecida ou tamanho errado


class DashboardState:
    """Último snapshot decodificado + trilha GPS limitada (thread-safe)."""

    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.latest: Optional[dict[str, Any]] = None
        self.track: deque[dict[str, float]] = deque(maxlen=GPS_TRACK_LIMIT)
        self.frames_received = 0
        self.invalid_payloads = 0
        self.last_error: Optional[str] = None

    def update(self, frame: Frame) -> tuple[Optional[dict[str, Any]], Optional[dict[str, float]]]:
        """Processa um frame de telemetria; retorna (snapshot, ponto GPS novo)."""
        if frame.frame_type != FRAME_TYPE_TELEMETRY:
            return None, None  # status/erro/etc. não alteram o painel
        decoded = decode_telemetry_payload(frame.payload)
        if decoded is None:
            with self.lock:
                self.invalid_payloads += 1
                self.last_error = "payload_schema_or_length"
            return None, None

        # Acrescenta a sequência e a qualidade do enlace LoRa.
        decoded["sequence"] = frame.sequence
        decoded["link"] = {
            "rssi_dbm": frame.rssi,
            "snr_db": _finite(frame.snr) if frame.snr is not None else None,
            "sequence_lost": frame.sequence_lost,
        }
        point = None
        gps = decoded["gps"]
        if gps["valid"] and gps["latitude"] is not None and gps["longitude"] is not None:
            point = {"lat": gps["latitude"], "lng": gps["longitude"]}

        with self.lock:
            self.frames_received += 1
            self.latest = decoded
            # Só grava ponto novo se a posição mudou (trilha sem repetições).
            if point is not None and (not self.track or self.track[-1] != point):
                self.track.append(point)
        return decoded, point

    def snapshot(self, include_track: bool = False) -> dict[str, Any]:
        """Cópia do estado atual (vazio no formato padrão se ainda não chegou frame)."""
        with self.lock:
            data = dict(self.latest) if self.latest is not None else {
                "schema_version": None,
                "timestamp_ms": None,
                "channels": [],
                "temperatures": [None] * 10,
                "gps": _gps(0, 0, 0, math.nan, math.nan, math.nan),
                "imu": _imu(0, *(math.nan,) * 5),
                "battery_state": _empty_battery_state(),
                "motor": _empty_motor(),
                "supply_5v_v": None,
                "error_flags": 0,
                "sequence": None,
                "link": {"rssi_dbm": None, "snr_db": None, "sequence_lost": None},
            }
            if include_track:
                data["gps"] = dict(data["gps"])
                data["gps"]["track"] = list(self.track)
            data["server"] = {"frames_received": self.frames_received, "invalid_payloads": self.invalid_payloads}
            return data


dashboard_state = DashboardState()


def emit_status(connected: bool, detail: str = "") -> None:
    """Avisa os navegadores sobre o estado da porta serial."""
    socketio.emit("serial_status", {"connected": connected, "detail": detail})


def process_frame(frame: Frame) -> None:
    """Atualiza o estado e envia o snapshot (e o ponto GPS novo) a todos os navegadores."""
    decoded, point = dashboard_state.update(frame)
    if decoded is None:
        return
    socketio.emit("telemetry_update", decoded)
    if point is not None:
        socketio.emit("gps_track_point", point)


def read_from_serial() -> None:
    """Lê a serial do receptor em blocos e reconecta sozinho em caso de falha."""
    decoder = FrameDecoder()
    text_buffer = bytearray()  # acumula a linha de texto (REV_B_FRAME ...)
    while not stop_event.is_set():
        if not SERIAL_PORT:
            emit_status(False, "serial_disabled")
            stop_event.wait(SERIAL_RECONNECT_SECONDS)
            continue
        try:
            with serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.25) as connection:
                emit_status(True, SERIAL_PORT)
                while not stop_event.is_set():
                    chunk = connection.read(connection.in_waiting or 1)
                    if not chunk:
                        continue
                    for byte in chunk:
                        # Caminho 1: frame binário direto na serial.
                        frame = decoder.feed(byte)
                        if frame is not None:
                            process_frame(frame)
                        # Caminho 2: linha de texto do ReceiverLoRa (termina em \n ou \r).
                        if byte in (10, 13):
                            if text_buffer:
                                frame = parse_receiver_line(text_buffer.decode("ascii", errors="ignore"))
                                if frame is not None:
                                    process_frame(frame)
                            text_buffer.clear()
                        elif byte == 9 or 32 <= byte <= 126:
                            if len(text_buffer) < 512:  # limite: linha longa demais é descartada
                                text_buffer.append(byte)
                            else:
                                text_buffer.clear()
        except (serial.SerialException, OSError) as exc:
            # Porta sumiu (cabo desconectado etc.): espera e tenta de novo.
            decoder.reset()
            text_buffer.clear()
            emit_status(False, str(exc))
            stop_event.wait(SERIAL_RECONNECT_SECONDS)
        except Exception as exc:  # mantém o dashboard vivo diante de entrada malformada
            dashboard_state.last_error = str(exc)
            decoder.reset()
            text_buffer.clear()
            emit_status(False, "reader_error")
            stop_event.wait(SERIAL_RECONNECT_SECONDS)


@socketio.on("request_snapshot")
def request_snapshot() -> None:
    # Responde só ao cliente que pediu, não a todos os dashboards conectados.
    emit("telemetry_snapshot", dashboard_state.snapshot(include_track=True))


@socketio.on("connect")
def on_connect() -> None:
    # Cliente novo recebe o estado atual e a trilha completa.
    emit("telemetry_snapshot", dashboard_state.snapshot(include_track=True))


@app.route("/")
def index():
    """Página do dashboard (templates/index.html)."""
    return render_template("index.html")


@app.route("/api/health")
def health():
    """Diagnóstico rápido: porta, velocidade e contadores."""
    snapshot = dashboard_state.snapshot()
    return jsonify({"serial_port": SERIAL_PORT, "baud_rate": BAUD_RATE, "snapshot": snapshot["server"]})


if __name__ == "__main__":
    socketio.start_background_task(read_from_serial)
    print(f"Servidor Rev B em http://localhost:5000; serial={SERIAL_PORT or 'desativada'}")
    try:
        # Dashboard local de bancada/campo: o Flask-SocketIO >= 5.3 recusa o
        # Werkzeug sem esta permissão explícita.
        # [AJUSTE] host="0.0.0.0" deixa outros aparelhos da rede abrirem o painel;
        # use "127.0.0.1" para restringir ao próprio computador. Porta 5000.
        socketio.run(app, host="0.0.0.0", port=5000, debug=False, allow_unsafe_werkzeug=True)
    finally:
        stop_event.set()
