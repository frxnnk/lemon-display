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
        self.assertNotIn("DOLAR DIGITAL", ui)
        self.assertIn("TETHER USDt", ui)
        self.assertIn('data/usdt_logo_28.h', ui)
        self.assertIn("BNB CHAIN", ui)
        self.assertIn("Arbitrum", ui)
        self.assertIn("Solana", ui)
        self.assertIn("v" , ui)
        self.assertNotIn("INTEL", ui)
        self.assertNotIn("ALERT", ui)

    def test_live_data_sources_are_independent_and_cover_regions(self):
        config = (ROOT / "src/config.h").read_text(encoding="utf-8")
        data_header = (ROOT / "src/usdt_lemon_data.h").read_text(encoding="utf-8")
        data = (ROOT / "src/usdt_lemon_data.cpp").read_text(encoding="utf-8")
        self.assertIn("COINBASE_USDT_RATES_EP", config)
        self.assertIn("COINGECKO_USDT_CHART_EP", config)
        self.assertNotIn("COINGECKO_USDT_MARKETS_EP", config)
        self.assertIn("variationsValid", data_header)
        self.assertIn("regionsValid", data_header)
        self.assertIn("variationsLastAttemptMs", data_header)
        self.assertIn("parseCoinbaseRates", data)
        self.assertIn("parseMarketChart", data)
        self.assertGreaterEqual(data.count("DeserializationOption::Filter"), 2)
        self.assertIn("USDT_VARIATIONS_REFRESH_MS", data)
        self.assertIn("out.regionsValid = false", data)

    def test_regions_and_network_copy_match_product_scope(self):
        ui = (ROOT / "src/usdt_lemon_ui.cpp").read_text(encoding="utf-8")
        for expected in ("ARGENTINA", "BRASIL", "PERU", "COLOMBIA"):
            self.assertIn(expected, ui)
        for removed in ("MEXICO", "GLOBAL", "MXN"):
            self.assertNotIn(removed, ui)
        self.assertIn("MISMA RED", ui)
        self.assertIn("VERIFICA EN LA APP", ui)

    def test_header_status_uses_primary_price_health(self):
        ui = (ROOT / "src/usdt_lemon_ui.cpp").read_text(encoding="utf-8")
        self.assertIn("usdtPrimaryFreshness(data.lemonFreshness, data.pegFreshness)", ui)
        self.assertGreaterEqual(ui.count("usdtAuxDataUsable"), 6)
        self.assertNotIn("PRECIO Y PEG EN VIVO", ui)

    def test_usdt_release_version_is_bumped(self):
        config = (ROOT / "src/config.h").read_text(encoding="utf-8")
        self.assertIn('#define APP_VERSION "5.1.1-usdt.5"', config)
    def test_main_boots_live_runtime_before_v1(self):
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        self.assertIn("#if LEMON_USDT_MODE", main)
        self.assertIn("usdtLemonSetup();", main)
        self.assertLess(main.index("#if LEMON_USDT_MODE"), main.index("Lemon Interface v4.0"))
if __name__ == "__main__":
    unittest.main()
