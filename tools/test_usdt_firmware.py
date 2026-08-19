import configparser
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
class UsdtFirmwareContractTests(unittest.TestCase):
    def test_has_live_environment_and_ota_channel(self):
        parser = configparser.ConfigParser(interpolation=None)
        parser.read(ROOT / "platformio.ini", encoding="utf-8")
        self.assertIn("env:matouch_esp32s3_40_usdt", parser.sections())
        self.assertIn("-DLEMON_USDT_MODE=1", parser["env:matouch_esp32s3_40_usdt"]["build_flags"])
        config = (ROOT / "src/config.h").read_text(encoding="utf-8")
        self.assertIn("#define LEMON_USDT_MODE 0", config)
        self.assertIn("#define OTA_USDT_ASSET \"firmware-usdt.bin\"", config)
        self.assertIn("LEMON_YIELD_EP", config)
        self.assertIn("CRIPTOYA_LEMON_USDT_EP", config)
    def test_runtime_auto_applies_ota_and_uses_lemon_yield(self):
        runtime = (ROOT / "src/usdt_lemon_runtime.cpp").read_text(encoding="utf-8")
        data = (ROOT / "src/usdt_lemon_data.cpp").read_text(encoding="utf-8")
        ui = (ROOT / "src/usdt_lemon_ui.cpp").read_text(encoding="utf-8")
        self.assertIn("otaCheckAsset(OTA_GITHUB_REPO, OTA_USDT_ASSET, APP_VERSION)", runtime)
        self.assertIn("installUsdtOtaNow()", runtime)
        self.assertIn("LEMON_YIELD", data)
        self.assertIn("DOLAR DIGITAL", ui)
        self.assertIn("BNB CHAIN", ui)
        self.assertNotIn("INTEL", ui)
        self.assertNotIn("ALERT", ui)
    def test_main_boots_live_runtime_before_v1(self):
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        self.assertIn("#if LEMON_USDT_MODE", main)
        self.assertIn("usdtLemonSetup();", main)
        self.assertLess(main.index("#if LEMON_USDT_MODE"), main.index("Lemon Interface v4.0"))
if __name__ == "__main__":
    unittest.main()
