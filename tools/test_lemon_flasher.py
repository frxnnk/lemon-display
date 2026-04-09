import os
import sys
import types
import unittest
from unittest import mock


sys.path.insert(0, os.path.dirname(__file__))

import lemon_flasher as lf


class FlashFirmwareArgsTest(unittest.TestCase):
    def test_normal_flash_preserves_image_header(self):
        fake_esptool = types.SimpleNamespace(calls=[])

        def fake_main(args):
            fake_esptool.calls.append(list(args))

        fake_esptool.main = fake_main

        with mock.patch.dict(sys.modules, {"esptool": fake_esptool}):
            lf.flash_firmware("COM9", b"firmware-bytes")

        write_call = fake_esptool.calls[0]
        self.assertIn("write-flash", write_call)
        self.assertEqual(write_call[write_call.index("--flash-mode") + 1], "keep")
        self.assertEqual(write_call[write_call.index("--flash-size") + 1], "keep")

    def test_recovery_flash_preserves_image_header(self):
        fake_esptool = types.SimpleNamespace(calls=[])

        def fake_main(args):
            fake_esptool.calls.append(list(args))

        fake_esptool.main = fake_main

        recovery_bins = {
            "bootloader.bin": "bootloader.bin",
            "partitions.bin": "partitions.bin",
            "boot_app0.bin": "boot_app0.bin",
            "spiffs.bin": "spiffs.bin",
        }

        with mock.patch.dict(sys.modules, {"esptool": fake_esptool}):
            with mock.patch.object(
                lf, "_find_recovery_bins", return_value=recovery_bins
            ):
                lf.flash_firmware("COM9", b"firmware-bytes", recovery=True)

        write_call = fake_esptool.calls[1]
        self.assertIn("write-flash", write_call)
        self.assertEqual(write_call[write_call.index("--flash-mode") + 1], "keep")
        self.assertEqual(write_call[write_call.index("--flash-size") + 1], "keep")


if __name__ == "__main__":
    unittest.main()
