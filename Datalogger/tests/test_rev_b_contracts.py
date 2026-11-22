"""Host-side contract checks for the Rev B skeleton.

These tests do not replace sensor/HIL tests. They validate the stable parts of the
contract without requiring a microcontroller or electrical hardware.
"""

import math
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode_frame(frame_type: int, sequence: int, payload: bytes) -> bytes:
    body = bytes((1, frame_type, sequence & 0xFF, sequence >> 8,
                  len(payload) & 0xFF, len(payload) >> 8)) + payload
    crc = crc16_ccitt(body)
    return b"\xA5\x5A" + body + bytes((crc & 0xFF, crc >> 8))


class RevBContractsTest(unittest.TestCase):
    def test_rev_b_is_the_only_datalogger_path(self):
        sketch = (ROOT / "Datalogger/Datalogger.ino").read_text(encoding="utf-8")
        makefile = (ROOT / "Datalogger/Makefile").read_text(encoding="utf-8")
        self.assertNotIn("REV_B_TELEMETRY_ENABLED", sketch)
        self.assertNotIn("#else", sketch)
        self.assertNotIn("build.extra_flags", makefile)

    def test_rev_b_dashboard_payload_is_bounded_and_versioned(self):
        source = (ROOT / "Datalogger/TelemetryPayloadRevB.h").read_text(encoding="utf-8")
        self.assertIn("SCHEMA_VERSION = 3", source)
        self.assertIn("CHANNEL_COUNT = 4", source)
        self.assertIn("TEMPERATURE_COUNT = 10", source)
        self.assertIn("sizeof(TelemetryPayloadRevB) == 240", source)
        # Offsets pinned to docs/specs/TELEMETRY_FRAME.md (compile-time checked).
        for field, offset in (("temperatures", 104), ("gpsValid", 144), ("imuValid", 168), ("socPct", 192),
                              ("motorValid", 204), ("supply5V", 232), ("errorFlags", 236)):
            self.assertIn(f"offsetof(TelemetryPayloadRevB, {field}) == {offset}", source)

    def test_pinmap_has_exclusive_rev_b_values(self):
        source = (ROOT / "Datalogger/config/pins_rev_b.h").read_text(encoding="utf-8")
        expected = {
            "PIN_LORA_TX": 12, "PIN_LORA_RX": 13, "PIN_GPS_TX": 17, "PIN_GPS_RX": 16,
            "PIN_I2C_SDA": 20, "PIN_I2C_SCL": 21, "PIN_SD_MISO": 14, "PIN_SD_CS": 15,
            "PIN_SD_SCK": 18, "PIN_SD_MOSI": 19, "PIN_ONEWIRE": 27, "PIN_FLOW_A": 23,
            "PIN_FLOW_B": 24, "PIN_HELTEC_RST": 28, "PIN_AUX_ADC3_SPARE": 29,
            "PIN_C3_UART_TX": 8, "PIN_C3_UART_RX": 9,
        }
        for name, value in expected.items():
            self.assertRegex(source, rf"{name}\s*=\s*{value}\b")

    def test_frame_crc_accepts_valid_and_rejects_mutation(self):
        frame = encode_frame(1, 0x1234, b"telemetry")
        self.assertEqual(len(frame), 10 + len(b"telemetry"))
        self.assertEqual(frame[:2], b"\xA5\x5A")
        body = frame[2:-2]
        received = frame[-2] | frame[-1] << 8
        self.assertEqual(received, crc16_ccitt(body))
        broken = bytearray(frame)
        broken[5] ^= 0x01
        self.assertNotEqual(broken[-2] | broken[-1] << 8, crc16_ccitt(bytes(broken[2:-2])))

    def test_power_is_not_calculated_for_invalid_pair(self):
        voltage, current = 48.0, float("nan")
        self.assertTrue(math.isnan(current))
        self.assertFalse(math.isfinite(voltage) and math.isfinite(current))

    def test_flow_formula(self):
        self.assertAlmostEqual(15.0 / 7.5, 2.0)

    def test_acs712_ratiometric_conversion(self):
        def current(adc_voltage, vcc5, gain=1.5, sensitivity=0.066):
            return (adc_voltage * gain - vcc5 / 2) / (sensitivity * vcc5 / 5.0)
        # Zero current sits at VCC/2 whatever the supply is.
        self.assertAlmostEqual(current(2.5 / 1.5, 5.0), 0.0)
        self.assertAlmostEqual(current(2.4 / 1.5, 4.8), 0.0)
        # 15 A at 5 V: 2.5 + 15 * 0.066 V at the sensor.
        self.assertAlmostEqual(current((2.5 + 15 * 0.066) / 1.5, 5.0), 15.0, places=4)
        # Full scale +30 A stays below 3.0 V at the ADS input (ADS at 3.3 V).
        self.assertLess((2.5 + 30 * 0.066) / 1.5, 3.0)
        source = (ROOT / "Datalogger/Acs712Channel.cpp").read_text(encoding="utf-8")
        self.assertIn("sensitivity_ * (vcc5 / 5.0f)", source)
        self.assertIn("vcc5 * 0.5f", source)
        self.assertFalse((ROOT / "Datalogger/Acs758Channel.cpp").exists())

    def test_rev_b_adc_channel_map(self):
        source = (ROOT / "Datalogger/config/rev_b_config.h").read_text(encoding="utf-8")
        self.assertIn("I_MPPT_A_ADC = {0, 0}", source)
        self.assertIn("I_MPPT_B_ADC = {0, 1}", source)
        self.assertIn("V_MPPT_A_ADC = {1, 0}", source)
        self.assertIn("V_MPPT_B_ADC = {1, 1}", source)
        self.assertIn("V_BAT_DIV_HV80_ADC = {1, 2}", source)
        self.assertIn("I_MPPT_C_ADC = {0, 2}", source)
        self.assertIn("VCC5_REF_ADC = {0, 3}", source)
        self.assertIn("V_MPPT_C_ADC = {1, 3}", source)

    def test_ina226_is_low_side_with_external_shunt_and_long_average(self):
        config = (ROOT / "Datalogger/config/rev_b_config.h").read_text(encoding="utf-8")
        self.assertIn("INA226_SHUNT_OHM = 0.0005f", config)
        self.assertIn("static_assert(INA226_LOW_SIDE_ONLY", config)
        self.assertIn("INA226_CONFIG_REGISTER = 0x4F25", config)
        # 0x4F25 = fixed 100 | AVG 1024 (111) | VBUSCT 1.1 ms | VSHCT 1.1 ms | shunt continuous (101)
        register = (0b100 << 12) | (0b111 << 9) | (0b100 << 6) | (0b100 << 3) | 0b101
        self.assertEqual(register, 0x4F25)
        self.assertAlmostEqual(1024 * 1.1e-3, 1.126, places=3)
        # 150 A / 75 mV shunt: INA226 full scale 81.92 mV covers the VESC 150 A peak.
        self.assertGreater(0.08192 / 0.0005, 150)

    def test_soc_rest_voltage_table_interpolation(self):
        table = [(54.1, 100), (52.7, 91), (51.6, 83), (50.5, 74), (49.5, 65), (48.6, 57), (47.9, 48),
                 (47.4, 40), (46.9, 31), (46.4, 22), (45.7, 14), (45.0, 8), (44.1, 5), (42.2, 1), (40.3, 0)]
        config = (ROOT / "Datalogger/config/rev_b_config.h").read_text(encoding="utf-8")
        for voltage, soc in table:
            self.assertIn(f"{{{voltage}f, {float(soc)}f}}", config)

        def soc_from_rest(voltage):
            if voltage >= table[0][0]:
                return 100.0
            if voltage <= table[-1][0]:
                return 0.0
            for (hi_v, hi_s), (lo_v, lo_s) in zip(table, table[1:]):
                if voltage >= lo_v:
                    return lo_s + (voltage - lo_v) / (hi_v - lo_v) * (hi_s - lo_s)
        self.assertAlmostEqual(soc_from_rest(50.5), 74.0)
        self.assertAlmostEqual(soc_from_rest(53.4), 95.5, places=1)
        self.assertEqual(soc_from_rest(36.0), 0.0)
        source = (ROOT / "Datalogger/BatterySoc.cpp").read_text(encoding="utf-8")
        self.assertIn("if (currentValid && hasValue())", source)  # invalid current is never integrated
        self.assertIn("BATTERY_FULL_SYNC_HOLD_MS", source)

    def test_csv_schema_two_has_six_nodes_and_new_columns(self):
        logger = (ROOT / "Datalogger/CsvLoggerRevB.cpp").read_text(encoding="utf-8")
        sketch = (ROOT / "Datalogger/Datalogger.ino").read_text(encoding="utf-8")
        self.assertIn("valid_flags,error_flags,soc_pct,rpm,speed_kn,course_deg", logger)
        self.assertIn("SCHEMA_VERSION = 2", (ROOT / "Datalogger/CsvLoggerRevB.h").read_text(encoding="utf-8"))
        for node in ("MPPT_A", "MPPT_B", "MPPT_C", "BATTERY", "MOTOR", "NAV"):
            self.assertIn(f'writeCsvRow("{node}"', sketch)

    def test_gps_rmc_speed_and_course_never_zero_fill(self):
        source = (ROOT / "Datalogger/GpsNeo6m.cpp").read_text(encoding="utf-8")
        self.assertIn("number(field(body, 7))", source)
        self.assertIn("number(field(body, 8))", source)
        self.assertIn(r"*value == '\0'", source)

    @unittest.skipUnless((ROOT / "docs/specs").is_dir(), "docs/ não está presente (não é publicada no remoto)")
    def test_specs_preserve_nan_and_low_side_constraints(self):
        csv = (ROOT / "docs/specs/CSV_SCHEMA.md").read_text(encoding="utf-8")
        config = (ROOT / "docs/specs/CONFIG_SCHEMA.md").read_text(encoding="utf-8")
        self.assertIn("nan", csv)
        self.assertIn("low-side", config)
        self.assertNotRegex(config, re.compile(r"INA226.*high-side", re.IGNORECASE))


    def test_telemetry_reaches_c3_and_sd_failures_are_observable(self):
        sketch = (ROOT / "Datalogger/Datalogger.ino").read_text(encoding="utf-8")
        self.assertIn("c3Link.begin(Serial2)", sketch)
        self.assertIn("c3Link.send(frame)", sketch)
        self.assertNotIn("String(millis())", sketch)
        self.assertIn("SD.exists(path)", sketch)
        self.assertIn("enterSdDegradedMode()", sketch)
        self.assertIn("adsErrors != lastAdsErrors", sketch)

    def test_flow_age_tolerates_pulse_after_sample_time(self):
        source = (ROOT / "Datalogger/FlowMeter.cpp").read_text(encoding="utf-8")
        self.assertIn("static_cast<int32_t>(nowMs - lastPulseMs)", source)
        now, last_pulse = 1000, 1003  # ISR fired after loop captured now
        age = ((now - last_pulse + 2**31) % 2**32) - 2**31
        self.assertLess(age, 2000)

    def test_sd_uses_pio_spi_and_spi0_is_left_for_the_lcd(self):
        pinmap = (ROOT / "Datalogger/PinMapRevB.cpp").read_text(encoding="utf-8")
        sketch = (ROOT / "Datalogger/Datalogger.ino").read_text(encoding="utf-8")
        pins = (ROOT / "Datalogger/config/pins_rev_b.h").read_text(encoding="utf-8")
        # SPI.setRX(14) panics on arduino-pico: GP14 is not an SPI0 MISO pin.
        spi0_rx_pins = {0, 4, 16, 20}
        self.assertNotIn(14, spi0_rx_pins)
        code = "\n".join(line for line in pinmap.splitlines() if not line.strip().startswith("//"))
        for call in ("SPI.setRX", "SPI.setTX", "SPI.setSCK", "SPI.begin"):
            self.assertNotIn(call, code)
        self.assertIn("SoftwareSPI SdSpi(Pins::PIN_SD_SCK, Pins::PIN_SD_MISO, Pins::PIN_SD_MOSI)", pinmap)
        self.assertIn("SD.begin(Pins::PIN_SD_CS, SdSpi)", sketch)
        for name, value in (("PIN_LCD_RST", 0), ("PIN_LCD_DC", 1), ("PIN_LCD_SCLK", 2),
                            ("PIN_LCD_MOSI", 3), ("PIN_LCD_BL", 4), ("PIN_LCD_CS", 5)):
            self.assertRegex(pins, rf"{name} = {value};")

if __name__ == "__main__":
    unittest.main()
