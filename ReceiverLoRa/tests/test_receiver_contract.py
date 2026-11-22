import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).parents[1]
SKETCH = (ROOT / "ReceiverLoRa.ino").read_text(encoding="utf-8")
RADIO = (ROOT / "Radio.h").read_text(encoding="utf-8")
FRAME = (ROOT / "TelemetryFrame.h").read_text(encoding="utf-8")


def accept_sequence(state, sequence, window=8):
    """Python mirror of acceptSequence() in the Heltec firmwares."""
    if state.get("last") is None:
        state["last"] = sequence
        return True
    delta = (sequence - state["last"]) & 0xFFFF
    if delta == 0:
        return False
    if delta >= 0x8000:
        if ((state["last"] - sequence) & 0xFFFF) <= window:
            return False
        state["last"] = sequence
        return True
    state["last"] = sequence
    return True


class ReceiverContractTests(unittest.TestCase):
    def test_frame_contract_and_bounded_payload(self):
        self.assertIn("SOF_0 = 0xA5", FRAME)
        self.assertIn("SOF_1 = 0x5A", FRAME)
        self.assertIn("MAX_PAYLOAD = 244", FRAME)
        self.assertIn("crc16", FRAME)
        self.assertIn("MAX_PACKET_SIZE", SKETCH)

    def test_invalid_packets_do_not_reach_publisher(self):
        self.assertIn("decoder.feed(packetBuffer[i], frame)", SKETCH)
        self.assertIn("publishFrame(frame, rssi, snr)", SKETCH)
        self.assertRegex(SKETCH, r"if \(acceptSequence\(frame\.sequence\)\) publishFrame")
        self.assertNotIn("Serial.println(msg)", SKETCH)

    def test_loss_and_radio_quality_are_serialized(self):
        self.assertIn("packetRssi", RADIO)
        self.assertIn("packetSnr", RADIO)
        self.assertIn("sequenceLost", SKETCH)
        self.assertIn("rssi=%d snr=%.2f", SKETCH)

    def test_receiver_has_no_sensor_or_blocking_loop(self):
        self.assertNotIn("Wire.", SKETCH)
        self.assertNotIn("delay(", SKETCH)
        self.assertNotIn("panic(", SKETCH)


    def test_sequence_filter_resyncs_after_rp2040_reboot(self):
        self.assertIn("SEQUENCE_REORDER_WINDOW = 8", SKETCH)
        self.assertIn("++stats.sequenceResyncs", SKETCH)
        state = {}
        self.assertTrue(accept_sequence(state, 5000))
        self.assertFalse(accept_sequence(state, 5000))   # duplicate
        self.assertFalse(accept_sequence(state, 4995))   # late frame
        self.assertTrue(accept_sequence(state, 0))       # reboot -> resync
        self.assertTrue(accept_sequence(state, 1))
        state = {"last": 0xFFFF}
        self.assertTrue(accept_sequence(state, 0))       # natural wrap

    def test_all_frame_copies_share_max_payload_and_web_dash_matches(self):
        import re
        root = ROOT.parent
        values = set()
        for folder in ("Datalogger", "TransmiterLoRa", "ReceiverLoRa", "DashboardAntenna"):
            header = (root / folder / "TelemetryFrame.h").read_text(encoding="utf-8")
            values.add(int(re.search(r"MAX_PAYLOAD = (\d+)", header).group(1)))
        web = (root / "web_dash" / "webserver.py").read_text(encoding="utf-8")
        values.add(int(re.search(r"MAX_PAYLOAD = (\d+)", web).group(1)))
        apk = (root / "dashboard_babitonga" / "lib" / "protocol" / "rev_b_frame.dart").read_text(encoding="utf-8")
        values.add(int(re.search(r"frameMaxPayload = (\d+)", apk).group(1)))
        self.assertEqual(values, {244})
        # LoRa packets are limited to 255 bytes: 10 bytes of framing + payload.
        self.assertLessEqual(10 + 244, 255)

if __name__ == "__main__":
    unittest.main()
