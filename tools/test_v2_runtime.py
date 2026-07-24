import configparser
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


class V2RuntimeModelTests(unittest.TestCase):
    def setUp(self):
        self.path = SRC / "v2_runtime_model.h"
        self.assertTrue(self.path.exists(), "v2_runtime_model.h is missing")
        self.source = self.path.read_text(encoding="utf-8")

    def test_scenes_cover_home_tape_context_and_settings(self):
        for scene in ("V2_HOME", "V2_MARKET_TAPE", "V2_CONTEXT", "V2_SETTINGS"):
            self.assertIn(scene, self.source)

    def test_navigation_has_safe_settings_entry_back_and_timeouts(self):
        for token in (
            "v2HandleGesture",
            "v2SettingsTabAt",
            "V2_HOME_TIMEOUT_MS = 0",
            "V2_TAPE_TIMEOUT_MS = 30000",
            "V2_CONTEXT_TIMEOUT_MS = 45000",
            "V2_SETTINGS_TIMEOUT_MS = 90000",
            "V2_MODEL_NAV_ASSERTS",
        ):
            self.assertIn(token, self.source)
        self.assertIn(
            "v2NextScene(V2_SETTINGS, TOUCH_SWIPE_RIGHT",
            self.source,
            "Settings horizontal swipes must change shallow pages instead of exiting",
        )
        self.assertIn("V2_MODEL_DIRECT_TOUCH_ASSERTS", self.source)

    def test_freshness_models_all_required_states(self):
        for state in (
            "V2_LOADING",
            "V2_LIVE",
            "V2_CACHED",
            "V2_STALE",
            "V2_OFFLINE",
            "V2_ERROR",
            "V2_RATE_LIMITED",
        ):
            self.assertIn(state, self.source)
        self.assertIn("v2Freshness", self.source)
        self.assertIn("V2_MODEL_FRESHNESS_ASSERTS", self.source)


class V2StocksAccessTests(unittest.TestCase):
    def test_market_tape_uses_thread_safe_snapshot_accessor(self):
        header = (SRC / "ui_stocks.h").read_text(encoding="utf-8")
        source = (SRC / "ui_stocks.cpp").read_text(encoding="utf-8")
        self.assertIn("stocksGetSnapshotAt", header)
        body = source[source.index("bool stocksGetSnapshotAt"):]
        self.assertIn("lockStocks", body)
        self.assertIn("unlockStocks", body)
        self.assertIn("StockFocusedSnapshot& out", body)


class V2NvsSettingsTests(unittest.TestCase):
    def test_rotation_preference_is_persisted_and_clamped(self):
        header = (SRC / "nvs_storage.h").read_text(encoding="utf-8")
        source = (SRC / "nvs_storage.cpp").read_text(encoding="utf-8")
        self.assertIn("nvsGetV2RotationSeconds", header)
        self.assertIn("nvsSetV2RotationSeconds", header)
        self.assertIn('"v2_rot"', source)
        for value in ("0", "15", "30", "60"):
            self.assertRegex(source, rf"\b{value}\b")


class V2RealBuildContractTests(unittest.TestCase):
    def test_real_runtime_has_a_dedicated_build_environment(self):
        parser = configparser.ConfigParser(interpolation=None)
        parser.read(ROOT / "platformio.ini", encoding="utf-8")
        section = "env:matouch_esp32s3_40_v2_real"
        self.assertIn(section, parser.sections())
        self.assertEqual(parser[section].get("extends"), "env:matouch_esp32s3_40")
        self.assertIn("-DLEMON_V2_REAL_MODE=1", parser[section]["build_flags"])

    def test_real_mode_is_off_by_default_and_separate_from_demo(self):
        source = (SRC / "config.h").read_text(encoding="utf-8")
        self.assertRegex(
            source,
            r"#ifndef LEMON_V2_REAL_MODE\s+#define LEMON_V2_REAL_MODE\s+0\s+#endif",
        )
        self.assertIn("LEMON_V2_DEMO_MODE", source)

    def test_main_routes_real_environment_to_runtime(self):
        source = (SRC / "main.cpp").read_text(encoding="utf-8")
        self.assertIn('#include "v2_runtime.h"', source)
        self.assertIn("#if LEMON_V2_REAL_MODE", source)
        self.assertIn("v2RuntimeSetup();", source)
        self.assertIn("v2RuntimeLoop();", source)


class V2RealUiContractTests(unittest.TestCase):
    def test_ui_uses_32px_safe_inset_and_real_snapshots(self):
        source = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        self.assertIn("V2_SAFE_INSET = 32", source)
        self.assertIn("V2RuntimeSnapshot", source)
        self.assertIn('strstr(stock.status, "429")', source)
        self.assertIn("stocksGetSnapshotAt", (SRC / "v2_runtime.cpp").read_text(encoding="utf-8"))

    def test_ui_does_not_contain_demo_quotes_or_fake_news(self):
        source = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        forbidden = (
            "$ 118.420",
            "6.309,62",
            "NVIDIA CAE",
            "DATOS DE DEMO",
            "PACK IA",
        )
        for fixture in forbidden:
            self.assertNotIn(fixture, source)
        self.assertIn("PROVIDER PENDIENTE", source)

    def test_settings_surface_keeps_canary_controls_shallow(self):
        source = (SRC / "ui_v2_settings.cpp").read_text(encoding="utf-8")
        for label in (
            "BRILLO",
            "FORMATO DE HORA",
            "SONIDO",
            "ROTACION",
            "WATCHLIST",
            "WI-FI",
            "NOTICIAS",
            "FIRMWARE",
            "DIAGNOSTICO",
            "ACTUALIZACION",
        ):
            self.assertIn(label, source)


class V2RenderStabilityTests(unittest.TestCase):
    def test_hero_price_respects_the_real_font_budget_before_the_graph(self):
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        font = (SRC / "data" / "PPNeueMachinaBold24.h").read_text(encoding="utf-8")
        advances = {
            chr(int(code, 16)): int(advance)
            for advance, code in re.findall(
                r"\{\s*\d+,\s*\d+,\s*\d+,\s*(\d+),\s*-?\d+,\s*-?\d+\s*\},"
                r"\s*// 0x([0-9A-F]+)",
                font,
            )
        }

        def text_width(value):
            return sum(advances[char] for char in value)

        self.assertIn("V2_HERO_PRICE_GRAPH_GAP = 12", ui)
        self.assertRegex(
            ui,
            r"V2_HERO_PRICE_MAX_W\s*=\s*V2_HERO_GRAPH_X\s*-\s*V2_SAFE_INSET\s*-\s*"
            r"V2_HERO_PRICE_GRAPH_GAP",
        )
        formatter = ui.split("static void formatHeroPrice", 1)[1].split(
            "static void drawPairSparkline", 1
        )[0]
        self.assertNotIn("pair.suffix", formatter)
        self.assertNotIn('const char* gap = pair.prefix[0] ? " " : ""', formatter)
        self.assertIn("%.0fK", formatter)
        self.assertIn("%.1fM", formatter)

        budget = 252 - 32 - 12
        for representative in ("$99999", "$999K", "$99,9M", "999,99", "99,9K"):
            self.assertLessEqual(
                text_width(representative),
                budget,
                f"{representative} exceeds the {budget}px hero budget",
            )

    def test_home_hero_centers_price_against_the_graph_instead_of_top_aligning_font_boxes(self):
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        self.assertIn("V2_HERO_LABEL_Y = 82", ui)
        self.assertIn("V2_HERO_GRAPH_Y = 106", ui)
        self.assertIn("V2_HERO_CENTER_Y = 144", ui)
        self.assertIn("V2_HOME_CARD_Y = 258", ui)
        self.assertIn("V2_HOME_TAPE_Y = 370", ui)
        pair = ui.split("static void drawPairPanel", 1)[1].split("static void drawHomeCards", 1)[0]
        self.assertIn("setTextDatum(lgfx::middle_left)", pair)
        self.assertIn("V2_HERO_CENTER_Y", pair)
        self.assertIn("V2_HERO_LABEL_Y", pair)

    def test_home_settings_is_a_separate_bottom_button_with_a_clear_controls_icon(self):
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        actions = ui.split("static void drawHomeActions", 1)[1].split("static void drawHeader", 1)[0]
        home = ui.split("static void drawHome(", 1)[1].split("static void drawTape", 1)[0]
        self.assertNotIn("drawSettingsIcon", actions)
        self.assertIn("TOCA PARA CAMBIAR", actions)
        self.assertIn("drawSettingsButton", home)
        self.assertIn("V2_SETTINGS_BUTTON_X = 390", ui)
        self.assertIn("V2_HOME_TAPE_W = 340", ui)

    def test_clock_uses_a_vsync_clipped_push_instead_of_full_scene_redraw(self):
        header = (SRC / "ui_v2_runtime.h").read_text(encoding="utf-8")
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        runtime = (SRC / "v2_runtime.cpp").read_text(encoding="utf-8")
        self.assertIn("v2UiUpdateClock", header)
        self.assertIn("v2UiUpdateClock", ui)
        clock_body = runtime[runtime.index("if (nowMs - s_lastClockDrawMs >= 1000"):]
        self.assertIn("v2UiUpdateClock", clock_body)
        self.assertNotIn("s_dirty = true", clock_body.split("if (s_dirty)", 1)[0])
        self.assertIn("setClipRect", ui)
        self.assertIn("clearClipRect", ui)
        push = ui.split("static void pushClip", 1)[1].split("static void", 1)[0]
        self.assertIn("s_v2Sprite.setClipRect", push)
        self.assertIn("s_v2Sprite.clearClipRect", push)
        self.assertIn("tft.setClipRect", push)

    def test_clock_skips_identical_text_before_touching_the_rgb_framebuffer(self):
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        clock = ui.split("void v2UiUpdateClock", 1)[1].split("void v2UiUpdateStatus", 1)[0]
        self.assertIn("s_lastDrawnTime", ui)
        self.assertIn("strcmp", clock)
        self.assertLess(clock.index("strcmp"), clock.index("fillRect"))

    def test_live_price_updates_have_their_own_clipped_render_path(self):
        header = (SRC / "ui_v2_runtime.h").read_text(encoding="utf-8")
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        self.assertIn("v2UiUpdatePair", header)
        self.assertIn("v2UiUpdatePair", ui)
        self.assertIn("V2_PAIR_CLIP", ui)

    def test_periodic_data_updates_never_invalidate_the_full_scene(self):
        runtime = (SRC / "v2_runtime.cpp").read_text(encoding="utf-8")
        btc_body = runtime.split("static void fetchBtcNow()", 1)[1].split("static void", 1)[0]
        self.assertNotIn("s_dirty = true", btc_body)
        stocks_block = runtime.split("if (stocksConsumeDirty())", 1)[1].split("uint32_t nowMs", 1)[0]
        self.assertNotIn("s_dirty = true", stocks_block)
        self.assertIn("v2UiUpdateData", stocks_block)
        rotation_block = runtime.split("if (rotation > 0", 1)[1].split("if (v2ApplyTimeout", 1)[0]
        self.assertNotIn("s_dirty = true", rotation_block)
        self.assertIn("v2UiUpdateHomeCards", rotation_block)

    def test_all_market_data_surfaces_have_clipped_update_paths(self):
        header = (SRC / "ui_v2_runtime.h").read_text(encoding="utf-8")
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        for function in (
            "v2UiUpdateHomeCards",
            "v2UiUpdateTape",
            "v2UiUpdateContext",
            "v2UiUpdateData",
        ):
            self.assertIn(function, header)
            self.assertIn(function, ui)
        self.assertIn("V2_HOME_CARDS_CLIP", ui)


class V2BootAndHomeParityTests(unittest.TestCase):
    def test_loading_is_drawn_before_wifi_or_provider_calls(self):
        runtime = (SRC / "v2_runtime.cpp").read_text(encoding="utf-8")
        ui_header = (SRC / "ui_v2_runtime.h").read_text(encoding="utf-8")
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        self.assertIn("v2UiDrawLoading", ui_header)
        self.assertIn("v2UiDrawLoading", ui)
        setup = runtime.split("void v2RuntimeSetup()", 1)[1]
        self.assertLess(setup.index("v2UiDrawLoading"), setup.index("wifiSetup"))
        self.assertIn("provisionDrawQR();", runtime)

    def test_loading_restores_progressive_legacy_lemon_wordmark(self):
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        loading = ui.split("void v2UiDrawLoading", 1)[1].split("void v2UiDraw(", 1)[0]
        self.assertIn('#include "data/lemon_logo.h"', ui)
        self.assertIn("drawLoadingLogoProgress", ui)
        self.assertIn("lemon_imagotipo_244", ui)
        self.assertIn("V2_LOADING_LOGO_W", ui)
        self.assertIn("progress * V2_LOADING_LOGO_W", ui)
        self.assertIn("toGray565", ui)
        self.assertNotIn("fillSmoothRoundRect(64, 286", loading)

    def test_loading_only_shows_logo_and_phase_text(self):
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        loading = ui.split("void v2UiDrawLoading", 1)[1].split("void v2UiDraw(", 1)[0]
        self.assertNotIn('drawString("LEMON BOX V2"', loading)
        self.assertNotIn('" / CANARY"', loading)

    def test_home_removes_technical_labels_and_sparkline_baseline(self):
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        pair = ui.split("static void drawPairPanel", 1)[1].split("static void drawHomeCards", 1)[0]
        spark = ui.split("static void drawPairSparkline", 1)[1].split("static void drawHeader", 1)[0]
        self.assertNotIn("COTIZACION CRUZADA", ui)
        self.assertNotIn("CRIPTOYA", ui)
        self.assertNotIn("pairSource", pair)
        self.assertNotIn("drawFastHLine", spark)

    def test_prices_use_positive_and_negative_market_colors(self):
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        self.assertIn("V2_NEGATIVE", ui)
        self.assertIn("trendColor", ui)
        self.assertIn("pairTrend", ui)
        self.assertIn("stock->quote.changePct", ui)

    def test_dollar_lemon_restores_buy_and_sell_with_freshness(self):
        runtime_header = (SRC / "v2_runtime.h").read_text(encoding="utf-8")
        runtime = (SRC / "v2_runtime.cpp").read_text(encoding="utf-8")
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        self.assertIn("lemonFreshness", runtime_header)
        self.assertIn("fetchLemonNow", runtime)
        for label in ("DOLAR LEMON", "COMPRA", "VENTA"):
            self.assertIn(label, ui)
        self.assertIn("CRIPTOYA_LEMON_EP", (SRC / "config.h").read_text(encoding="utf-8"))

    def test_pair_hero_has_real_sparkline_and_compact_large_numbers(self):
        runtime_header = (SRC / "v2_runtime.h").read_text(encoding="utf-8")
        runtime = (SRC / "v2_runtime.cpp").read_text(encoding="utf-8")
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        self.assertIn("SparklineData pairSpark", runtime_header)
        self.assertIn("wsBinanceGetSparkline", runtime)
        self.assertIn("drawPairSparkline", ui)
        self.assertIn("formatHeroPrice", ui)
        self.assertIn("1000000.0f", ui)
        self.assertIn("100000.0f", ui)
        self.assertIn("%.0fK", ui)
        self.assertIn("%.1fM", ui)
        self.assertIn('strcmp(pair.label, "USD")', ui)

    def test_market_tape_uses_a_drawn_chevron_not_a_font_glyph(self):
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        self.assertIn("drawChevron", ui)
        self.assertNotIn('drawString(">"', ui)


class V2WifiRecoveryTests(unittest.TestCase):
    def test_saved_wifi_failure_offers_reconfiguration_without_forgetting_credentials(self):
        runtime = (SRC / "v2_runtime.cpp").read_text(encoding="utf-8")
        setup = runtime.split("void v2RuntimeSetup()", 1)[1].split("void v2RuntimeLoop()", 1)[0]
        connect = setup.split("wifiSetup(ssid, pass);", 1)[1]
        self.assertIn("if (!wifiConnected())", connect)
        self.assertIn("showWifiRecovery(ssid);", connect)
        failure = connect.split("if (!wifiConnected())", 1)[1].split("startNetworkServices", 1)[0]
        self.assertNotIn("nvsForgetWifi", failure)

    def test_provisioned_wifi_is_saved_only_after_connection_succeeds(self):
        runtime = (SRC / "v2_runtime.cpp").read_text(encoding="utf-8")
        provisioning = runtime.split("if (s_provisioning)", 1)[1].split("wifiLoop();", 1)[0]
        self.assertIn("if (wifiConnected())", provisioning)
        self.assertLess(provisioning.index("wifiSetup(ssid, pass);"), provisioning.index("if (wifiConnected())"))
        self.assertLess(provisioning.index("if (wifiConnected())"), provisioning.index("nvsSaveWifi(ssid, pass);"))
        self.assertIn("showWifiRecovery(ssid);", provisioning)

    def test_wifi_recovery_screen_has_an_explicit_touch_action(self):
        model = (SRC / "v2_runtime_model.h").read_text(encoding="utf-8")
        runtime = (SRC / "v2_runtime.cpp").read_text(encoding="utf-8")
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        self.assertIn("V2_WIFI_RECOVERY", model)
        self.assertIn("CONFIGURAR OTRA RED", ui)
        self.assertIn("handleWifiRecoveryTouch", runtime)
        self.assertIn("startProvisioning();", runtime)


class V2OtaChannelTests(unittest.TestCase):
    def test_remote_canary_build_has_the_next_version(self):
        config = (SRC / "config.h").read_text(encoding="utf-8")
        self.assertIn('#define APP_VERSION "5.1.1-beta.49"', config)

    def test_ota_md5_is_normalized_for_case_sensitive_esp_update(self):
        manager = (SRC / "ota_manager.cpp").read_text(encoding="utf-8")
        self.assertIn("normalizeMd5Lower", manager)
        extract_body = manager.split("extractMd5NearAsset", 1)[1].split("OtaInfo otaCheckAsset", 1)[0]
        self.assertIn("normalizeMd5Lower(out)", extract_body)

    def test_v2_uses_an_exact_dedicated_release_asset(self):
        config = (SRC / "config.h").read_text(encoding="utf-8")
        manager_h = (SRC / "ota_manager.h").read_text(encoding="utf-8")
        manager = (SRC / "ota_manager.cpp").read_text(encoding="utf-8")
        runtime = (SRC / "v2_runtime.cpp").read_text(encoding="utf-8")
        self.assertIn('#define OTA_V2_ASSET "firmware-v2.bin"', config)
        self.assertIn("otaCheckAsset", manager_h)
        self.assertIn("otaCheckAsset", manager)
        self.assertIn('asset["name"]', manager)
        self.assertIn("OTA_V2_ASSET", runtime)
        self.assertIn("otaCheckAsset(OTA_GITHUB_REPO, OTA_V2_ASSET, APP_VERSION)", runtime)
        self.assertIn("s_otaInfo.available && !s_otaInfo.md5[0]", runtime)
        self.assertNotIn("otaCheck(OTA_GITHUB_REPO)", runtime)

    def test_latest_release_probe_notifies_home_within_one_minute(self):
        manager_h = (SRC / "ota_manager.h").read_text(encoding="utf-8")
        manager = (SRC / "ota_manager.cpp").read_text(encoding="utf-8")
        runtime = (SRC / "v2_runtime.cpp").read_text(encoding="utf-8")
        self.assertIn("otaLatestTagChanged", manager_h)
        probe = manager.split("bool otaLatestTagChanged", 1)[1].split("OtaInfo otaCheckAsset", 1)[0]
        self.assertIn("/releases/latest", probe)
        self.assertIn('sendRequest("HEAD")', probe)
        self.assertIn("getLocation", probe)
        self.assertIn("isNewer", probe)
        self.assertIn("V2_OTA_PROBE_MS = 60UL * 1000UL", runtime)
        loop = runtime.split("void v2RuntimeLoop()", 1)[1]
        self.assertIn("otaLatestTagChanged(OTA_GITHUB_REPO, APP_VERSION)", loop)
        self.assertIn("checkV2OtaNow(false)", loop)

    def test_v2_update_notice_is_explicit_and_requires_confirmation(self):
        runtime_h = (SRC / "v2_runtime.h").read_text(encoding="utf-8")
        runtime = (SRC / "v2_runtime.cpp").read_text(encoding="utf-8")
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        settings = (SRC / "ui_v2_settings.cpp").read_text(encoding="utf-8")
        for field in ("otaChecked", "otaAvailable", "otaChecking", "otaArmed", "otaVersion"):
            self.assertIn(field, runtime_h)
        self.assertIn("ACTUALIZACION V", ui)
        self.assertIn("TOCA PARA CONFIRMAR", ui)
        self.assertIn("CONFIRMAR UPDATE", settings)
        self.assertIn("s_snapshot.otaArmed", runtime)
        self.assertIn("otaFlash", runtime)

    def test_home_update_banner_arms_then_installs_on_second_tap(self):
        runtime = (SRC / "v2_runtime.cpp").read_text(encoding="utf-8")
        handler = runtime.split("static void handleTouch()", 1)[1].split("void v2RuntimeSetup()", 1)[0]
        self.assertIn("handleHomeUpdateTap", handler)
        self.assertIn("installV2OtaNow", runtime)
        self.assertIn("s_otaArmedUntilMs", runtime)


class V2DirectTouchNavigationTests(unittest.TestCase):
    def test_home_uses_a_drawn_settings_icon_instead_of_hidden_instruction(self):
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        self.assertIn("drawSettingsButton", ui)
        self.assertNotIn("MANTENE AJUSTES", ui)
        self.assertNotIn('drawHeader("HOME"', ui)

    def test_headers_and_settings_tabs_are_tappable(self):
        model = (SRC / "v2_runtime_model.h").read_text(encoding="utf-8")
        runtime = (SRC / "v2_runtime.cpp").read_text(encoding="utf-8")
        settings = (SRC / "ui_v2_settings.cpp").read_text(encoding="utf-8")
        self.assertIn("v2BackScene", model)
        self.assertIn("v2SettingsTabAt", model)
        self.assertIn("settingsPage = static_cast<uint8_t>(tab)", runtime)
        self.assertIn("TOCA UNA SECCION", settings)

    def test_update_check_recovers_live_services_without_full_scene_redraw(self):
        runtime = (SRC / "v2_runtime.cpp").read_text(encoding="utf-8")
        self.assertIn("checkV2OtaNow", runtime)
        check_body = runtime.split("static void checkV2OtaNow", 1)[1].split("static void", 1)[0]
        self.assertIn("wsBinanceStop", check_body)
        self.assertIn("configurePairFeed", check_body)
        self.assertIn("v2UiUpdateStatus", check_body)
        self.assertNotIn("s_dirty = true", check_body)


class V2PairParityTests(unittest.TestCase):
    def test_v2_reuses_binance_websocket_and_backfill(self):
        runtime = (SRC / "v2_runtime.cpp").read_text(encoding="utf-8")
        ws_header = (SRC / "ws_binance.h").read_text(encoding="utf-8")
        for token in (
            '#include "ws_binance.h"',
            "wsBinanceLoop();",
            "wsBinanceReconnect",
            "wsBinanceBackfillSymbol",
            "wsBinanceConnected()",
            "wsBinanceLastPriceMs()",
        ):
            self.assertIn(token, runtime)
        self.assertIn("wsBinanceLastPriceMs", ws_header)

    def test_v2_exposes_all_existing_quote_pairs_without_trading_actions(self):
        config = (SRC / "config.h").read_text(encoding="utf-8")
        model = (SRC / "v2_runtime_model.h").read_text(encoding="utf-8")
        runtime_header = (SRC / "v2_runtime.h").read_text(encoding="utf-8")
        runtime = (SRC / "v2_runtime.cpp").read_text(encoding="utf-8")
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        for pair in ('"BTC/USD"', '"BTC/ETH"', '"BTC/SOL"', '"BTC/ARS"', '"BTC/ORO"'):
            self.assertIn(pair, config)
        self.assertIn("v2NextPair", model)
        self.assertIn("selectedPair", runtime_header)
        self.assertIn("TOCA PARA CAMBIAR", ui)
        for forbidden in ("buyorder", "sellorder", "executetrade", "placeorder"):
            self.assertNotIn(forbidden, runtime.lower())

    def test_selected_pair_is_persisted_and_clamped(self):
        header = (SRC / "nvs_storage.h").read_text(encoding="utf-8")
        source = (SRC / "nvs_storage.cpp").read_text(encoding="utf-8")
        self.assertIn("nvsGetV2Pair", header)
        self.assertIn("nvsSetV2Pair", header)
        self.assertIn('"v2_pair"', source)


class V2RealFlashContractTests(unittest.TestCase):
    def setUp(self):
        self.path = ROOT / "tools" / "flash_v2_real_canary.ps1"
        self.assertTrue(self.path.exists(), "real canary flash script is missing")
        self.source = self.path.read_text(encoding="utf-8")

    def test_flash_requires_identity_confirmation_and_creates_rollback(self):
        for token in (
            "[string]$Port",
            "[string]$ExpectedSerial",
            "[switch]$ConfirmCanary",
            "0x303A",
            "read-flash",
            "rollback",
            "Get-FileHash",
        ):
            self.assertIn(token, self.source)

    def test_flash_is_normal_keep_mode_only(self):
        self.assertIn('"0x10000"', self.source)
        self.assertIn('"--flash-mode", "keep"', self.source)
        self.assertIn('"--flash-size", "keep"', self.source)
        self.assertNotIn("erase-flash", self.source)
        self.assertNotIn("erase_flash", self.source)
        self.assertIn("matouch_esp32s3_40_v2_real", self.source)

    def test_flash_supports_manual_bootloader_without_forcing_another_reset(self):
        self.assertIn("[switch]$ManualBootloader", self.source)
        self.assertIn('$initialReset = if ($ManualBootloader) { "no-reset" }', self.source)
        self.assertIn("--before $initialReset", self.source)

    def test_flash_accepts_only_native_usb_or_known_uart_bridges(self):
        for vid in ("0x303A", "0x10C4", "0x1A86", "0x0403"):
            self.assertIn(vid, self.source)
        self.assertIn("$allowedVids", self.source)


if __name__ == "__main__":
    unittest.main()
