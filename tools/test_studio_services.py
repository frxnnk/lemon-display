import os
import sys
import tempfile
import unittest
from unittest import mock


sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))


class StudioServicesTest(unittest.TestCase):
    def test_scan_ports_wraps_flasher_detection(self):
        from studio.services import StudioServices

        svc = StudioServices(os.path.dirname(os.path.dirname(__file__)))

        with mock.patch("studio.services.flasher.detect_esp_ports",
                        return_value=(["COM7"], ["USB JTAG [ESP32]"])):
            ports = svc.scan_ports()

        self.assertEqual(ports, [{
            "device": "COM7",
            "description": "USB JTAG [ESP32]",
            "isEsp32": True,
        }])

    def test_latest_release_wraps_flasher_release_lookup(self):
        from studio.services import StudioServices

        svc = StudioServices(os.path.dirname(os.path.dirname(__file__)))

        with mock.patch("studio.services.flasher.fetch_latest_release",
                        return_value=("5.1.2", "https://example.test/fw.bin", "abc")):
            release = svc.latest_release()

        self.assertEqual(release["version"], "5.1.2")
        self.assertEqual(release["assetUrl"], "https://example.test/fw.bin")
        self.assertEqual(release["md5"], "abc")

    def test_flash_rejects_unconfirmed_firmware_action(self):
        from studio.services import StudioServices

        svc = StudioServices(os.path.dirname(os.path.dirname(__file__)))

        with tempfile.NamedTemporaryFile(delete=False) as tmp:
            tmp.write(b"firmware")
            firmware_path = tmp.name
        self.addCleanup(lambda: os.path.exists(firmware_path) and os.unlink(firmware_path))

        with self.assertRaises(ValueError):
            svc.flash_firmware({
                "port": "COM9",
                "mode": "normal",
                "firmwareSource": "path",
                "firmwarePath": firmware_path,
                "confirmed": False,
            })

    def test_flash_uses_recovery_flag_when_confirmed(self):
        from studio.services import StudioServices

        svc = StudioServices(os.path.dirname(os.path.dirname(__file__)))

        with tempfile.NamedTemporaryFile(delete=False) as tmp:
            tmp.write(b"firmware")
            firmware_path = tmp.name
        self.addCleanup(lambda: os.path.exists(firmware_path) and os.unlink(firmware_path))

        with mock.patch("studio.services.flasher.flash_firmware", return_value=True) as flash:
            result = svc.flash_firmware({
                "port": "COM9",
                "mode": "recovery",
                "firmwareSource": "path",
                "firmwarePath": firmware_path,
                "confirmed": True,
                "recoveryConfirmed": True,
            })

        self.assertTrue(result["ok"])
        self.assertEqual(result["mode"], "recovery")
        self.assertEqual(flash.call_args.kwargs["recovery"], True)

    def test_deploy_draft_writes_prompt_without_secret_values(self):
        from studio.services import StudioServices

        with tempfile.TemporaryDirectory() as tmp:
            svc = StudioServices(tmp)
            result = svc.create_deploy_draft({
                "prompt": "Build a calendar window",
                "target": "firmware",
                "manifest": {
                    "cards": [{"title": "Calendar", "kind": "custom"}],
                },
                "keys": [{"name": "OPENAI_API_KEY", "masked": "sk********11"}],
            })
            with open(result["path"], "r", encoding="utf-8") as f:
                raw = f.read()

        self.assertIn("Build a calendar window", raw)
        self.assertIn("OPENAI_API_KEY", raw)
        self.assertNotIn("sk-live-secret", raw)
        self.assertEqual(result["target"], "firmware")
        self.assertEqual(result["cardCount"], 1)


if __name__ == "__main__":
    unittest.main()
