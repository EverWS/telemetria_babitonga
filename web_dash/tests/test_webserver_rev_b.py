import math
import pathlib
import struct
import unittest

from web_dash.webserver import (
    DashboardState,
    Frame,
    FrameDecoder,
    decode_telemetry_payload,
    parse_receiver_line,
    crc16_ccitt,
)


def encode_frame(frame_type, sequence, payload):
    header = struct.pack("<BBHH", 1, frame_type, sequence, len(payload))
    crc = crc16_ccitt(header + payload)
    return b"\xA5\x5A" + header + payload + struct.pack("<H", crc)


def schema_two_payload(latitude=-26.25, longitude=-48.65):
    payload = bytearray(struct.pack("<BBBBI", 2, 3, 2, 0, 1234))
    payload.extend(struct.pack("<5fI", 48.0, 2.0, 96.0, 1.5, 4.0, 0x10 | (1 << 8)))
    payload.extend(struct.pack("<5fI", 49.0, 3.0, 147.0, 2.5, 5.0, 0x0F | (1 << 8)))
    payload.extend(struct.pack("<5fI", 52.0, -1.0, -52.0, 3.5, math.nan, 0x0F))
    payload.extend(struct.pack("<5fI", math.nan, math.nan, math.nan, math.nan, math.nan, 0))
    payload.extend(struct.pack("<10f", 21.5, 22.5, *([math.nan] * 8)))
    payload.extend(struct.pack("<BBBB3f", 1, 1, 8, 0, latitude, longitude, 1.2))
    payload.extend(struct.pack("<B3x5f", 0, math.nan, math.nan, math.nan, math.nan, math.nan))
    payload.extend(struct.pack("<I", 0))
    assert len(payload) == 188
    return bytes(payload)


def schema_three_payload(motor_valid=1, soc=78.5):
    payload = bytearray(struct.pack("<BBBBI", 3, 4, 3, 0, 5678))
    payload.extend(struct.pack("<5fI", 41.0, 10.0, 410.0, 1.0, 4.0, 0x0F | (1 << 8)))   # MPPT_A
    payload.extend(struct.pack("<5fI", 42.0, 11.0, 462.0, 2.0, 5.0, 0x0F | (1 << 8)))   # MPPT_B
    payload.extend(struct.pack("<5fI", 43.0, 12.0, 516.0, 3.0, math.nan, 0x0F))         # MPPT_C
    payload.extend(struct.pack("<5fI", 50.5, 20.0, 1010.0, -4.0, math.nan, 0x0F))       # BATTERY
    payload.extend(struct.pack("<10f", 20.0, 21.0, 22.0, *([math.nan] * 7)))
    payload.extend(struct.pack("<BBBB5f", 1, 1, 9, 0, -26.25, -48.65, 0.9, 6.4, 123.0))
    payload.extend(struct.pack("<B3x5f", 0, *([math.nan] * 5)))
    payload.extend(struct.pack("<ffB3x", soc, 9.2, 1))
    payload.extend(struct.pack("<BBH6f", motor_valid, 0, 120, 1500.0, 45.0, 38.0, 21.0, 50.4, 42.0))
    payload.extend(struct.pack("<fI", 5.02, 1 << 5 if not motor_valid else 0))
    assert len(payload) == 240
    return bytes(payload)


class RevBWebserverTest(unittest.TestCase):
    def test_schema_three_decodes_every_block_at_spec_offsets(self):
        decoded = decode_telemetry_payload(schema_three_payload())
        self.assertEqual(decoded["schema_version"], 3)
        ids = [channel["id"] for channel in decoded["channels"]]
        self.assertEqual(ids, ["mppt_a", "mppt_b", "mppt_c", "battery"])
        self.assertEqual(decoded["channels"][2]["current_a"], 12.0)
        self.assertEqual(decoded["channels"][3]["voltage_v"], 50.5)
        self.assertEqual(decoded["temperatures"][:3], [20.0, 21.0, 22.0])
        self.assertIsNone(decoded["temperatures"][3])
        self.assertAlmostEqual(decoded["gps"]["speed_kn"], 6.4, places=4)
        self.assertAlmostEqual(decoded["gps"]["course_deg"], 123.0, places=4)
        self.assertAlmostEqual(decoded["battery_state"]["soc_pct"], 78.5, places=4)
        self.assertEqual(decoded["battery_state"]["soc_source"], "coulomb")
        self.assertTrue(decoded["motor"]["valid"])
        self.assertEqual(decoded["motor"]["rpm"], 1500.0)
        self.assertEqual(decoded["motor"]["temperature_c"], 45.0)
        self.assertAlmostEqual(decoded["supply_5v_v"], 5.02, places=4)

    def test_schema_three_stale_motor_is_hidden_not_zeroed(self):
        decoded = decode_telemetry_payload(schema_three_payload(motor_valid=0, soc=math.nan))
        self.assertFalse(decoded["motor"]["valid"])
        self.assertIsNone(decoded["motor"]["rpm"])
        self.assertIsNone(decoded["motor"]["temperature_c"])
        self.assertIsNone(decoded["battery_state"]["soc_pct"])
        self.assertEqual(decoded["error_flags"], 1 << 5)

    def test_all_schemas_share_one_shape(self):
        legacy = decode_telemetry_payload(struct.pack("<I6fI", 10, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 0))
        two = decode_telemetry_payload(schema_two_payload())
        three = decode_telemetry_payload(schema_three_payload())
        for decoded in (legacy, two, three):
            self.assertEqual(set(decoded), set(three))
            self.assertEqual([channel["id"] for channel in decoded["channels"]], ["mppt_a", "mppt_b", "mppt_c", "battery"])
        # Schema 2 battery moves from slot 2 to the battery entry; MPPT C is unavailable.
        self.assertEqual(two["channels"][3]["voltage_v"], 52.0)
        self.assertIsNone(two["channels"][2]["voltage_v"])

    def test_max_payload_accepts_schema_three_frames(self):
        frame_bytes = encode_frame(1, 3, schema_three_payload())
        decoder = FrameDecoder()
        frames = [decoder.feed(byte, now=0.0) for byte in frame_bytes]
        frame = next(item for item in frames if item is not None)
        self.assertEqual(len(frame.payload), 240)

    def test_live_preview_contract_is_present(self):
        html = pathlib.Path(__file__).resolve().parents[1].joinpath("templates/index.html").read_text(encoding="utf-8")
        for marker in ("revb-top", "id=\"map\"", "id=\"temp-grid\"", "id=\"metric-lines\"", "DEMO_MODE", "startDemoMode"):
            self.assertIn(marker, html)
        self.assertIn("../static/three.min.js", html)

    def test_leaflet_assets_use_official_sri_and_map_keeps_user_zoom(self):
        html = pathlib.Path(__file__).resolve().parents[1].joinpath("templates/index.html").read_text(encoding="utf-8")
        # A wrong SRI hash makes the browser block leaflet.css and scramble the tiles.
        self.assertIn('leaflet@1.9.4/dist/leaflet.css"\n          integrity="sha256-p4NxAoJBhIIN+hmNHrzRCf9tD/miZyoHS5obTRR9BMY="', html)
        self.assertIn('integrity="sha256-20nQCchB9co0qIjJZRGuk2/Z9VM+kNiyxNV1lvTlZBo="', html)
        self.assertIn("map.on('dragstart'", html)
        self.assertNotIn("if (center) map.setView(point, Math.max(map.getZoom(), 15)", html)

    def test_map_offers_satellite_and_nautical_layers(self):
        html = pathlib.Path(__file__).resolve().parents[1].joinpath("templates/index.html").read_text(encoding="utf-8")
        self.assertIn("World_Imagery/MapServer", html)
        self.assertIn("tiles.openseamap.org/seamark/{z}/{x}/{y}.png", html)
        self.assertIn("'Carta náutica (OpenSeaMap)': seamarks", html)
        self.assertIn("revb.map.base", html)

    def test_map_size_follows_gps_card(self):
        html = pathlib.Path(__file__).resolve().parents[1].joinpath("templates/index.html").read_text(encoding="utf-8")
        self.assertNotIn("#map { flex: none; height: 450px;", html)
        self.assertIn("#map { flex: 1 1 auto; height: auto;", html)
        self.assertIn("aspect-ratio: 4 / 3", html)
        self.assertIn("new ResizeObserver(refreshSize).observe(element)", html)

    def test_light_theme_uses_logo_palette_and_kpis(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        html = root.joinpath("templates/index.html").read_text(encoding="utf-8")
        self.assertTrue(root.joinpath("static/img/logo_babitonga.png").is_file())
        self.assertIn("../static/img/logo_babitonga.png", html)
        for token in ("--bg: #F2F6F8", "--sky: #96CEF2", "--brand: #3DB3AE", "--green: #00A35A", "--sun: #F2AE00"):
            self.assertIn(token, html)
        for old_neon in ("#00d4ff", "#00ff9d", "#050d14", "Orbitron"):
            self.assertNotIn(old_neon, html)
        self.assertIn("renderKpis(snapshot);", html)

    def test_kpi_strip_order_is_speed_rpm_motor_temp_soc_gps_lora(self):
        html = pathlib.Path(__file__).resolve().parents[1].joinpath("templates/index.html").read_text(encoding="utf-8")
        strip = html.split('<section class="kpi-strip"', 1)[1].split("</section>", 1)[0]
        expected = ["kpi-speed", "kpi-rpm", "kpi-motor-temp", "kpi-soc", "kpi-gps", "kpi-link"]
        positions = [strip.index(f'id="{kpi}"') for kpi in expected]
        self.assertEqual(positions, sorted(positions))
        self.assertEqual(strip.count('class="kpi '), 6)
        # Knots come from the GPS field when present, otherwise from the track.
        self.assertIn("KNOTS_PER_MPS = 1.943844", html)
        self.assertIn("gps.speed_kn", html)
        self.assertIn("motor.rpm", html)
        self.assertIn("motor.temperature_c", html)
        self.assertIn("soc_pct", html)

    def test_server_start_allows_local_werkzeug(self):
        source = pathlib.Path(__file__).resolve().parents[1].joinpath("webserver.py").read_text(encoding="utf-8")
        self.assertIn("allow_unsafe_werkzeug=True", source)

    def test_binary_decoder_accepts_frame_and_crc_rejects_mutation(self):
        payload = bytes(range(32))
        frame_bytes = encode_frame(1, 7, payload)
        decoder = FrameDecoder()
        frames = [decoder.feed(byte, now=0.01 * index) for index, byte in enumerate(frame_bytes)]
        frame = next(item for item in frames if item is not None)
        self.assertEqual(frame.sequence, 7)
        self.assertEqual(frame.payload, payload)

        broken = bytearray(frame_bytes)
        broken[-1] ^= 0x01
        decoder.reset()
        for byte in broken:
            decoder.feed(byte)
        self.assertEqual(decoder.crc_errors, 1)

    def test_decoder_resets_after_truncated_frame_timeout(self):
        decoder = FrameDecoder(timeout_seconds=1.0)
        for index, byte in enumerate(b"\xA5\x5A\x01\x01"):
            decoder.feed(byte, now=float(index))
        frame_bytes = encode_frame(1, 9, b"ok")
        result = None
        for index, byte in enumerate(frame_bytes):
            result = decoder.feed(byte, now=10.0 + index * 0.01) or result
        self.assertIsNotNone(result)
        self.assertEqual(result.sequence, 9)
        self.assertGreaterEqual(decoder.framing_errors, 1)

    def test_schema_two_exposes_channels_temperatures_and_gps(self):
        decoded = decode_telemetry_payload(schema_two_payload())
        self.assertEqual(decoded["schema_version"], 2)
        self.assertEqual(decoded["channels"][0]["current_a"], 2.0)
        self.assertEqual(decoded["channels"][1]["flow_l_min"], 5.0)
        self.assertEqual(decoded["temperatures"][:2], [21.5, 22.5])
        self.assertTrue(decoded["gps"]["valid"])
        self.assertAlmostEqual(decoded["gps"]["latitude"], -26.25, places=4)

    def test_legacy_payload_is_supported_without_zero_filling(self):
        payload = struct.pack("<I6fI", 10, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 0)
        decoded = decode_telemetry_payload(payload)
        self.assertEqual(decoded["schema_version"], 1)
        self.assertEqual(decoded["channels"][0]["power_w"], 1.0)
        self.assertIsNone(decoded["channels"][0]["current_a"])
        self.assertIsNone(decoded["gps"]["latitude"])

    def test_receiver_line_and_track_are_bounded(self):
        payload = schema_two_payload()
        line = "REV_B_FRAME type=1 seq=4 len=188 rssi=-70 snr=7.25 lost=2 payload=" + payload.hex().upper()
        frame = parse_receiver_line(line)
        self.assertIsInstance(frame, Frame)
        self.assertEqual(frame.rssi, -70)
        state = DashboardState()
        for index in range(200):
            state.update(Frame(1, index, schema_two_payload(latitude=-26.25 + index / 10000)))
        self.assertEqual(len(state.track), 180)
        self.assertEqual(state.frames_received, 200)


if __name__ == "__main__":
    unittest.main()
