import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class TransmitterContractTest(unittest.TestCase):
    def test_uart_pins_and_baud_are_rev_b_selection(self):
        source = (ROOT / "TransmiterLoRa.ino").read_text(encoding="utf-8")
        self.assertIn("UART_BAUD = 115200", source)
        self.assertIn("HELTEC_TX = 17", source)
        self.assertIn("HELTEC_RX_SELECT = 13", source)

    def test_telemetry_path_has_no_i2c_or_blocking_delay(self):
        source = (ROOT / "TransmiterLoRa.ino").read_text(encoding="utf-8")
        self.assertNotIn("Wire.", source)
        self.assertNotIn("delay(", source)
        self.assertIn("decoder.feed", source)
        self.assertIn("radio.sendFrame", source)

    def test_queue_and_integrity_guards_exist(self):
        source = (ROOT / "TransmiterLoRa.ino").read_text(encoding="utf-8")
        self.assertIn("QUEUE_CAPACITY = 4", source)
        self.assertIn("decoder.crcErrors()", source)
        self.assertIn("sequenceLost", source)
        self.assertIn("isCritical", source)


    def test_sequence_filter_resyncs_after_rp2040_reboot(self):
        source = (ROOT / "TransmiterLoRa.ino").read_text(encoding="utf-8")
        self.assertIn("SEQUENCE_REORDER_WINDOW = 8", source)
        self.assertIn("++stats.sequenceResyncs", source)
        self.assertIn("decoder.reset()", source)
        self.assertIn("UART_FRAME_GAP_MS", source)


if __name__ == "__main__":
    unittest.main()
