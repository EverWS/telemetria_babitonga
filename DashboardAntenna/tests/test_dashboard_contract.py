import pathlib
import unittest


ROOT = pathlib.Path(__file__).parents[1]
SKETCH = (ROOT / "DashboardAntenna.ino").read_text(encoding="utf-8")
MAKEFILE = (ROOT / "Makefile").read_text(encoding="utf-8")
# A pasta docs/ pode não existir (ela não é publicada no repositório remoto).
SPEC_PATH = ROOT.parent / "docs" / "specs" / "DASHBOARD_BLE.md"
SPEC = SPEC_PATH.read_text(encoding="utf-8") if SPEC_PATH.exists() else None
ACTIVE = SKETCH.split("#else", 1)[0]


class DashboardContractTests(unittest.TestCase):
    def test_rev_b_build_and_uart_selection(self):
        self.assertIn("arduino-cli compile", MAKEFILE)
        self.assertNotIn("REV_B_DASHBOARD_ENABLED", MAKEFILE)
        self.assertNotIn("#else", SKETCH)
        self.assertIn("UART_RX_PIN = 6", ACTIVE)
        self.assertIn("UART_TX_PIN = 7", ACTIVE)
        self.assertIn("UART_BAUD = 115200", ACTIVE)

    def test_nonblocking_bounded_queues_and_finite_retries(self):
        self.assertIn("QUEUE_CAPACITY = 4", ACTIVE)
        self.assertIn("MAX_CRITICAL_ATTEMPTS = 3", ACTIVE)
        self.assertIn("availableForWrite", ACTIVE)
        self.assertIn("bleQueueDrops", ACTIVE)
        self.assertIn("uartQueueDrops", ACTIVE)
        self.assertNotIn("delay(", ACTIVE)

    def test_control_and_config_validation(self):
        self.assertIn("validateControl", ACTIVE)
        self.assertIn("frame.payload[0] == 1 && frame.payload[1] == 1", ACTIVE)
        self.assertIn("enqueueNack", ACTIVE)
        self.assertIn("bleCrcErrors", ACTIVE)

    def test_ble_contract_and_no_can(self):
        self.assertIn("TELEMETRY_UUID", ACTIVE)
        self.assertIn("CONTROL_UUID", ACTIVE)
        self.assertIn("HEARTBEAT_UUID", ACTIVE)
        self.assertIn("notify()", ACTIVE)
        self.assertNotIn("CAN", ACTIVE)
        if SPEC is not None:  # só confere a spec quando docs/ existe
            self.assertIn("Fila BLE e fila UART têm capacidade 4", SPEC)


    def test_ble_writes_are_handed_to_loop_and_mtu_fits_frame(self):
        self.assertIn("portENTER_CRITICAL(&bleWriteMux)", ACTIVE)
        self.assertIn("processPendingBleWrite(nowMs)", ACTIVE)
        on_write = ACTIVE.split("void onWrite", 1)[1].split("};", 1)[0]
        self.assertNotIn("handleBleWrite", on_write)
        self.assertIn("BLEDevice::setMTU(BLE_PREFERRED_MTU)", ACTIVE)
        self.assertIn("uartDecoder.reset()", ACTIVE)


if __name__ == "__main__":
    unittest.main()
