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


    def test_lora_link_parameters_are_pinned_and_identical(self):
        """ADR-007: SF7/BW125/CR4-5/17 dBm/915,0 MHz fixados nos dois rádios."""
        import math
        import re
        names = ("BAND", "SPREADING_FACTOR", "SIGNAL_BANDWIDTH", "CODING_RATE_DENOMINATOR",
                 "TX_POWER_DBM", "PREAMBLE_SYMBOLS", "SYNC_WORD")
        values = {}
        for folder in ("TransmiterLoRa", "ReceiverLoRa"):
            radio = (ROOT.parent / folder / "Radio.h").read_text(encoding="utf-8")
            values[folder] = {name: re.search(rf"{name} = ([0-9xA-Fa-fEe.]+);", radio).group(1) for name in names}
            for call in ("setSpreadingFactor(SPREADING_FACTOR)", "setSignalBandwidth(SIGNAL_BANDWIDTH)",
                         "setCodingRate4(CODING_RATE_DENOMINATOR)", "setTxPower(TX_POWER_DBM",
                         "disableLowDataRate()", "enableCrc()"):
                self.assertIn(call, radio, folder)
        self.assertEqual(values["TransmiterLoRa"], values["ReceiverLoRa"])
        v = values["TransmiterLoRa"]
        self.assertEqual((v["BAND"], v["SPREADING_FACTOR"], v["SIGNAL_BANDWIDTH"]), ("915e6", "7", "125E3"))
        self.assertEqual((v["CODING_RATE_DENOMINATOR"], v["TX_POWER_DBM"]), ("5", "17"))
        # Tempo no ar (fórmula Semtech) do frame de 250 B cabe em 1 s.
        sf, bw, pl, cr = 7, 125e3, 250, 1
        symbols = 8 + max(math.ceil((8 * pl - 4 * sf + 28 + 16) / (4 * sf)) * (cr + 4), 0)
        airtime = (8 + 4.25 + symbols) * (2 ** sf) / bw
        self.assertLess(airtime, 0.5)

if __name__ == "__main__":
    unittest.main()
