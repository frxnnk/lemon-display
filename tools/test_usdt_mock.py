import configparser
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class UsdtMockContractTests(unittest.TestCase):
    def test_has_dedicated_platformio_environment(self):
        parser = configparser.ConfigParser(interpolation=None)
        parser.read(ROOT / "platformio.ini", encoding="utf-8")
        section = "env:matouch_esp32s3_40_usdt_mock"
        self.assertIn(section, parser.sections())
        self.assertEqual(parser[section].get("extends"), "env:matouch_esp32s3_40")
        self.assertIn("-DLEMON_USDT_MOCK_MODE=1", parser[section]["build_flags"])

    def test_mock_is_off_by_default_and_is_integrated_before_normal_runtime(self):
        config = (ROOT / "src/config.h").read_text(encoding="utf-8")
        self.assertIn("#define LEMON_USDT_MOCK_MODE 0", config)
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        self.assertIn('#include "ui_usdt_mock.h"', main)
        self.assertIn("usdtMockSetup();", main)
        self.assertIn("usdtMockTick(millis());", main)

    def test_mock_contains_the_validation_screens_and_no_live_fetch(self):
        source = (ROOT / "src/ui_usdt_mock.cpp").read_text(encoding="utf-8")
        for label in ("USDT CONTROL ROOM", "PEG", "NETWORKS", "MARKETS", "LEMON", "INTEL", "ALERT"):
            self.assertIn(label, source)
        self.assertIn("TOUCH_SWIPE_LEFT", source)
        self.assertIn("TOUCH_SWIPE_RIGHT", source)
        self.assertNotIn("HTTPClient", source)
        self.assertNotIn("api_client.h", source)


if __name__ == "__main__":
    unittest.main()
