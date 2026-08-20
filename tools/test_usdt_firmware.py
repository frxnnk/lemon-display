import configparser
import json
import subprocess
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

    def test_usdt_environment_keeps_the_hardware_memory_flags(self):
        parser = configparser.ConfigParser(interpolation=None)
        parser.read(ROOT / "platformio.ini", encoding="utf-8")
        flags = parser["env:matouch_esp32s3_40_usdt"]["build_flags"]
        self.assertIn("${env:matouch_esp32s3_40.build_flags}", flags)
        self.assertIn("-DLEMON_USDT_MODE=1", flags)
    def test_runtime_auto_applies_ota_and_uses_lemon_yield(self):
        runtime = (ROOT / "src/usdt_lemon_runtime.cpp").read_text(encoding="utf-8")
        worker = (ROOT / "src/usdt_lemon_worker.cpp").read_text(encoding="utf-8")
        data = (ROOT / "src/usdt_lemon_data.cpp").read_text(encoding="utf-8")
        ui = (ROOT / "src/usdt_lemon_ui.cpp").read_text(encoding="utf-8")
        self.assertIn("otaCheckAsset(", worker)
        self.assertIn("OTA_GITHUB_REPO, OTA_USDT_ASSET, APP_VERSION", worker)
        self.assertIn("installUsdtOtaNow()", runtime)
        self.assertIn("LEMON_YIELD", data)
        self.assertNotIn("DOLAR DIGITAL", ui)
        self.assertNotIn("TETHER USDt", ui)
        self.assertIn('data/usdt_logo_64.h', ui)
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
        self.assertIn("USDT_VARIATIONS_RETRY_MS", data)
        self.assertIn("io.peg.variationsValid ? USDT_VARIATIONS_REFRESH_MS", data)
        self.assertIn("out.regionsValid = false", data)

    def test_placeholder_coingecko_key_is_never_sent(self):
        api = (ROOT / "src/api_client.cpp").read_text(encoding="utf-8")
        self.assertIn("coinGeckoKeyConfigured", api)
        self.assertIn('strcmp(COINGECKO_API_KEY, "YOUR_COINGECKO_DEMO_KEY")', api)
        self.assertIn("addCoinGeckoKey && coinGeckoKeyConfigured()", api)

    def test_vercel_network_endpoint_has_its_active_google_root(self):
        api = (ROOT / "src/api_client.cpp").read_text(encoding="utf-8")
        self.assertIn("GTS Root R1", api)
        self.assertIn("MIIFVzCCAz+gAwIBAgINAgPlk28xsBNJiGuiFz", api)

    def test_overview_shows_argentina_flag_and_price_cents(self):
        ui = (ROOT / "src/usdt_lemon_ui.cpp").read_text(encoding="utf-8")
        self.assertIn("drawArgentinaFlag", ui)
        self.assertIn('snprintf(out, outSize, "$%.2f", value)', ui)
        overview = ui[ui.index("void drawOverview") : ui.index("void drawNetworks")]
        self.assertIn("drawArgentinaFlag", overview)

    def test_regions_show_each_country_flag_inside_its_card(self):
        ui = (ROOT / "src/usdt_lemon_ui.cpp").read_text(encoding="utf-8")
        regions = ui[ui.index("void drawRegions") : ui.index("void drawSystem")]
        for function in (
            "drawArgentinaFlag",
            "drawBrazilFlag",
            "drawPeruFlag",
            "drawColombiaFlag",
        ):
            self.assertIn(function, ui)
            self.assertIn(function, regions)

    def test_network_rows_use_each_chain_icon_instead_of_generic_dots(self):
        ui = (ROOT / "src/usdt_lemon_ui.cpp").read_text(encoding="utf-8")
        networks = ui[ui.index("void drawNetworks") : ui.index("void drawMarkets")]
        for function in (
            "drawBnbIcon",
            "drawPolygonIcon",
            "drawTronIcon",
            "drawEthereumIcon",
        ):
            self.assertIn(function, ui)
            self.assertIn(function, networks)
        self.assertNotIn("fillCircle(SAFE + 6", networks)
        self.assertIn(
            "s_canvas.drawString(change, SAFE + 230, y + 25, &Satoshi12);",
            networks,
        )
        self.assertNotIn(
            "s_canvas.drawString(change, SCREEN_W - SAFE, y + 28, &Satoshi9);",
            networks,
        )

    def test_primary_price_refreshes_independently_every_15_seconds(self):
        runtime = (ROOT / "src/usdt_lemon_runtime.cpp").read_text(encoding="utf-8")
        worker = (ROOT / "src/usdt_lemon_worker.cpp").read_text(encoding="utf-8")
        worker_header = (ROOT / "src/usdt_lemon_worker.h").read_text(encoding="utf-8")
        self.assertIn("PRICE_REFRESH_INTERVAL_MS = 15UL * 1000UL", runtime)
        self.assertIn("usdtWorkerRequestPrice", runtime)
        self.assertIn("usdtWorkerRequestPrice", worker)
        self.assertIn("usdtWorkerRequestPrice", worker_header)
        self.assertIn("USDT_WORKER_PRICE_COMPLETE", worker_header)

    def test_peg_card_draws_a_real_live_sparkline(self):
        header = (ROOT / "src/usdt_lemon_data.h").read_text(encoding="utf-8")
        data = (ROOT / "src/usdt_lemon_data.cpp").read_text(encoding="utf-8")
        ui = (ROOT / "src/usdt_lemon_ui.cpp").read_text(encoding="utf-8")
        overview = ui[ui.index("void drawOverview") : ui.index("void drawNetworks")]
        self.assertIn("USDT_PEG_SAMPLE_COUNT = 24", header)
        self.assertIn("float pegSamples[USDT_PEG_SAMPLE_COUNT]", header)
        self.assertIn("appendPegSample(out, usd);", data)
        self.assertIn("void drawPegSparkline", ui)
        self.assertIn("drawPegSparkline(data.peg", overview)
        self.assertIn('snprintf(pegSuffix, sizeof(pegSuffix), "DESVIO %+.1f BPS", bps);', overview)

    def test_market_data_runs_outside_the_touch_loop_after_ntp_sync(self):
        runtime = (ROOT / "src/usdt_lemon_runtime.cpp").read_text(encoding="utf-8")
        worker = (ROOT / "src/usdt_lemon_worker.cpp").read_text(encoding="utf-8")
        header = (ROOT / "src/usdt_lemon_data.h").read_text(encoding="utf-8")
        for function in (
            "usdtDataFetchPrice",
            "usdtDataFetchRates",
            "usdtDataFetchYield",
            "usdtDataFetchVariations",
        ):
            self.assertIn(function, header)
            self.assertIn(function, worker)
            self.assertNotIn(function + "(", runtime)
        self.assertIn("xTaskCreatePinnedToCore", worker)
        self.assertIn("usdtWorkerPoll", runtime)
        self.assertIn("if (!timeReady())", runtime)
        loop = runtime[runtime.index("void usdtLemonLoop()") :]
        self.assertLess(loop.index("touchLoop()"), loop.index("serviceWorkerUpdates()"))
        self.assertNotIn("otaCheckAsset(", runtime)
        self.assertNotIn("otaLatestTagChanged(", runtime)
        self.assertIn("otaCheckAsset(", worker)
        self.assertIn("otaLatestTagChanged(", worker)
        self.assertNotIn('usdtUiDrawLoading("LEYENDO MERCADO"', runtime)
        self.assertIn("constexpr uint32_t CLOCK_REDRAW_MS = 30UL * 1000UL", runtime)

    def test_dashboard_requests_fail_fast_and_retry_on_next_cycle(self):
        data = (ROOT / "src/usdt_lemon_data.cpp").read_text(encoding="utf-8")
        api_header = (ROOT / "src/api_client.h").read_text(encoding="utf-8")
        self.assertIn("int maxAttempts = 2", api_header)
        self.assertGreaterEqual(data.count(", 5000,"), 4)
        self.assertGreaterEqual(data.count(", 1);"), 4)

    def test_regions_and_network_data_match_product_scope(self):
        ui = (ROOT / "src/usdt_lemon_ui.cpp").read_text(encoding="utf-8")
        config = (ROOT / "src/config.h").read_text(encoding="utf-8")
        data_header = (ROOT / "src/usdt_lemon_data.h").read_text(encoding="utf-8")
        data = (ROOT / "src/usdt_lemon_data.cpp").read_text(encoding="utf-8")
        for expected in ("ARGENTINA", "BRASIL", "PERU", "COLOMBIA"):
            self.assertIn(expected, ui)
        for removed in ("MEXICO", "GLOBAL", "MXN"):
            self.assertNotIn(removed, ui)
        self.assertNotIn("MISMA RED", ui)
        self.assertNotIn("GUIA ESTATICA", ui)
        self.assertIn("USDT EN CIRCULACION", ui)
        self.assertIn("24H", ui)
        self.assertIn("formatUsdSupply", ui)
        self.assertIn("data.networks", ui)
        self.assertIn("LEMON_USDT_NETWORKS_EP", config)
        self.assertNotIn("DEFILLAMA_USDT_EP", config)
        self.assertIn("UsdtNetworkData", data_header)
        self.assertIn("usdtDataFetchNetworks", data_header)
        self.assertIn("parseNetworkSupply", data)

    def test_network_proxy_returns_a_small_stable_contract(self):
        proxy = ROOT / "api" / "usdt-networks.js"
        self.assertTrue(proxy.exists())
        fixture = {
            "peggedAssets": [{
                "symbol": "USDT",
                "chainCirculating": {
                    "BSC": {"current": {"peggedUSD": 110}, "circulatingPrevDay": {"peggedUSD": 100}},
                    "Polygon": {"current": {"peggedUSD": 200}, "circulatingPrevDay": {"peggedUSD": 250}},
                    "Tron": {"current": {"peggedUSD": 300}, "circulatingPrevDay": {"peggedUSD": 300}},
                    "Ethereum": {"current": {"peggedUSD": 450}, "circulatingPrevDay": {"peggedUSD": 400}},
                },
            }],
        }
        script = (
            "const p=require('./api/usdt-networks.js');"
            "const out=p.buildNetworkPayload(JSON.parse(process.argv[1]));"
            "process.stdout.write(JSON.stringify(out));"
        )
        result = subprocess.run(
            ["node", "-e", script, json.dumps(fixture)],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=True,
        )
        payload = json.loads(result.stdout)
        self.assertEqual([item["id"] for item in payload["networks"]],
                         ["bsc", "polygon", "tron", "ethereum"])
        self.assertAlmostEqual(payload["networks"][0]["change24h"], 10.0)
        self.assertAlmostEqual(payload["networks"][1]["change24h"], -20.0)
        self.assertLess(len(result.stdout), 1024)

    def test_header_uses_actionable_retry_copy_instead_of_generic_error(self):
        data = (ROOT / "src/usdt_lemon_data.cpp").read_text(encoding="utf-8")
        self.assertNotIn('return "ERROR"', data)
        self.assertIn('return "REINTENTO"', data)
        self.assertIn("usdtFetchStatusLabel", data)

    def test_header_status_uses_primary_price_health(self):
        ui = (ROOT / "src/usdt_lemon_ui.cpp").read_text(encoding="utf-8")
        self.assertIn("usdtPrimaryFreshness(data.lemonFreshness, data.pegFreshness)", ui)
        self.assertGreaterEqual(ui.count("usdtAuxDataUsable"), 6)
        self.assertNotIn("PRECIO Y PEG EN VIVO", ui)

    def test_usdt_release_version_is_bumped(self):
        config = (ROOT / "src/config.h").read_text(encoding="utf-8")
        self.assertIn('#define APP_VERSION "5.1.1-usdt.13"', config)
    def test_main_boots_live_runtime_before_v1(self):
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        self.assertIn("#if LEMON_USDT_MODE", main)
        self.assertIn("usdtLemonSetup();", main)
        self.assertLess(main.index("#if LEMON_USDT_MODE"), main.index("Lemon Interface v4.0"))
if __name__ == "__main__":
    unittest.main()
