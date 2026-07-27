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


class V2NewsRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.runtime = (SRC / "v2_runtime.cpp").read_text(encoding="utf-8")

    def test_news_is_queued_at_boot_and_only_fetched_after_stocks_are_idle(self):
        start = self.runtime.split("static void startNetworkServices() {", 1)[1].split(
            "static void startProvisioning", 1
        )[0]
        loop = self.runtime.split("void v2RuntimeLoop()", 1)[1]

        self.assertIn("requestNewsFetch();", start)
        self.assertRegex(
            loop,
            r"s_newsFetchPending\s*&&\s*online\s*&&\s*!stocksIsFetching\(\)",
        )

    def test_news_request_does_not_open_tls_inside_touch_or_rotation_handlers(self):
        touch = self.runtime.split("static void handleTouch()", 1)[1].split(
            "void v2RuntimeSetup", 1
        )[0]
        rotation = self.runtime.split("if (rotation > 0", 1)[1].split(
            "if (v2ApplyTimeout", 1
        )[0]

        self.assertIn("requestNewsFetch();", touch)
        self.assertNotIn("fetchNewsNow();", touch)
        self.assertIn("requestNewsFetch();", rotation)
        self.assertNotIn("fetchNewsNow();", rotation)

    def test_news_fetch_releases_shared_tls_before_opening_its_connection(self):
        fetch = self.runtime.split("static void fetchNewsNow()", 1)[1].split(
            "static void fetchPairAuxNow", 1
        )[0]

        self.assertIn("apiStop();", fetch)
        self.assertLess(fetch.index("apiStop();"), fetch.index("newsFetch("))

    def test_news_client_uses_the_live_google_rss_instead_of_retired_yahoo_feed(self):
        client = (SRC / "news_client.cpp").read_text(encoding="utf-8")

        self.assertIn("https://news.google.com/rss/search?q=%s+stock", client)
        self.assertNotIn("feeds.finance.yahoo.com/rss", client)

    def test_news_reuses_the_hardened_chunked_http_pipeline(self):
        client = (SRC / "news_client.cpp").read_text(encoding="utf-8")
        api = (SRC / "api_client.cpp").read_text(encoding="utf-8")
        fetch = client.split("bool newsFetch", 1)[1]

        self.assertIn("apiHttpGet(url, false, result", fetch)
        self.assertIn("result != API_OK", fetch)
        self.assertIn("writeToStream(&bufStream)", api)
        self.assertNotIn("getStreamPtr", client)
        self.assertNotIn("WiFiClientSecure", client)

    def test_switching_stock_reuses_fresh_symbol_cache_without_loading_or_tls(self):
        header = (SRC / "news_client.h").read_text(encoding="utf-8")
        client = (SRC / "news_client.cpp").read_text(encoding="utf-8")
        request = self.runtime.split("static void requestNewsFetch()", 1)[1].split(
            "static void fetchNewsNow", 1
        )[0]

        self.assertIn("newsCacheIsFresh", header)
        self.assertIn("NewsCacheEntry", client)
        self.assertIn("_newsCache[STOCK_MAX_SYMBOLS]", client)
        self.assertNotIn("static char  _cachedSymbol", client)
        self.assertIn("newsGetCached", request)
        self.assertIn("newsCacheIsFresh", request)
        self.assertIn("if (cachedCount == 0)", request)
        cache_flow = request.split("uint8_t cachedCount = 0;", 1)[1]
        self.assertNotIn(
            "s_snapshot.newsCount = 0;",
            cache_flow.split("if (cachedCount == 0)", 1)[0],
        )

    def test_news_reader_cycles_cached_items_without_requesting_network(self):
        marker = "if (event.gesture == TOUCH_TAP && s_model.scene == V2_NEWS_READER)"
        self.assertIn(marker, self.runtime)
        reader_touch = self.runtime.split(marker, 1)[1].split(
            "if (event.gesture == TOUCH_TAP && s_model.scene == V2_HOME)", 1
        )[0]

        self.assertIn("v2NextNews", reader_touch)
        self.assertNotIn("requestNewsFetch", reader_touch)


class V2NewsRenderingTests(unittest.TestCase):
    def test_home_and_context_use_two_pixel_fitted_headline_lines(self):
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        home = ui.split("static void drawNewsHeadline", 1)[1].split(
            "static void drawBtcCard", 1
        )[0]
        context = ui.split("static void drawContext", 1)[1].split(
            "static void drawWifiRecovery", 1
        )[0]

        self.assertIn("layoutHeadline", ui)
        self.assertIn("textWidth", ui)
        self.assertNotIn("maxChars = 42", home)
        headline_block = ui.split("static void drawHeadlineBlock", 1)[1].split(
            "static void drawNewsHeadline", 1
        )[0]
        self.assertIn("drawHeadlineBlock", home)
        self.assertGreaterEqual(headline_block.count("SatoshiMedium18"), 2)
        self.assertIn("drawHeadlineBlock", context)
        self.assertNotIn("for (uint8_t i = 0; i < snapshot.newsCount", context)

    def test_news_client_normalizes_punctuation_missing_from_ascii_only_font(self):
        client = (SRC / "news_client.cpp").read_text(encoding="utf-8")
        font = (SRC / "data" / "SatoshiMedium18.h").read_text(encoding="utf-8")

        self.assertIn("0x20, 0x7E", font)
        self.assertIn("normalizeNewsPunctuation", client)
        normalization = client.split("normalizeNewsPunctuation", 1)[1].split(
            "static void stripHtml", 1
        )[0]
        for replacement in (
            "*dst++ = '\\''",
            "*dst++ = '\"'",
            "*dst++ = '-'",
            'memcpy(dst, "...", 3)',
        ):
            self.assertIn(replacement, normalization)

    def test_news_tap_opens_full_reader_with_multi_line_cached_navigation(self):
        model = (SRC / "v2_runtime_model.h").read_text(encoding="utf-8")
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")

        self.assertIn("V2_NEWS_READER", model)
        self.assertIn("selectedNews", model)
        self.assertIn("v2NextNews", model)
        self.assertRegex(
            model,
            r"y >= 228 && y < 300 \? V2_NEWS_READER",
        )
        reader = ui.split("static void drawNewsReader", 1)[1].split(
            "static void drawWifiRecovery", 1
        )[0]
        self.assertIn("wrapHeadline", reader)
        self.assertIn("NEWS_READER_MAX_LINES", reader)
        self.assertIn("model.selectedNews", reader)
        self.assertIn("TOCA PARA SIGUIENTE", reader)


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
        self.assertIn("stocksGetSnapshotAt", (SRC / "v2_runtime.cpp").read_text(encoding="utf-8"))

    def test_ui_does_not_contain_demo_quotes_or_fake_news(self):
        source = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        settings = (SRC / "ui_v2_settings.cpp").read_text(encoding="utf-8")
        forbidden = (
            "$ 118.420",
            "6.309,62",
            "NVIDIA CAE",
            "DATOS DE DEMO",
            "PACK IA",
        )
        for fixture in forbidden:
            self.assertNotIn(fixture, source)
        self.assertIn("PROVIDER PENDIENTE", settings)

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
            "static void drawSparkline", 1
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
        self.assertIn("V2_HERO_GRAPH_Y = 98", ui)
        self.assertIn("V2_HERO_CENTER_Y = 132", ui)
        self.assertIn("V2_HERO_CHANGE_Y = 168", ui)
        self.assertIn("V2_HOME_CARD_Y = 328", ui)
        self.assertNotIn("V2_HOME_TAPE_Y", ui)
        pair = ui.split("static void drawStockHero", 1)[1].split("static void drawBtcCard", 1)[0]
        self.assertIn("setTextDatum(lgfx::middle_left)", pair)
        self.assertIn("V2_HERO_CENTER_Y", pair)
        self.assertIn("V2_HERO_LABEL_Y", pair)

    def test_stock_main_prices_drop_cents_but_keep_percentage_precision(self):
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        hero = ui.split("static void drawStockHero", 1)[1].split(
            "static constexpr uint8_t NEWS_READER_MAX_LINES", 1
        )[0]
        context = ui.split("static void drawContext", 1)[1].split(
            "static void drawNewsReader", 1
        )[0]
        live_update = ui.split("void v2UiUpdateStockPrice", 1)[1].split(
            "void v2UiUpdateBtcCard", 1
        )[0]

        self.assertIn('snprintf(price, sizeof(price), "$%.0f"', hero)
        self.assertIn('snprintf(price, sizeof(price), "$ %.0f"', context)
        self.assertIn('snprintf(price, sizeof(price), "$%.0f"', live_update)
        self.assertNotIn('snprintf(price, sizeof(price), "$%.2f"', hero)
        self.assertNotIn('snprintf(price, sizeof(price), "$ %.2f"', context)
        self.assertNotIn('snprintf(price, sizeof(price), "$%.2f"', live_update)
        for section in (hero, context, live_update):
            self.assertIn("%+.2f%% HOY", section)

    def test_home_settings_lives_in_the_header_with_a_drawn_controls_icon(self):
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        header = ui.split("static void drawHeader", 1)[1].split("static void drawStockHero", 1)[0]
        self.assertIn("drawSettingsIcon", header)
        self.assertNotIn("drawSettingsButton", ui)
        self.assertNotIn("drawHomeActions", ui)
        self.assertNotIn("static void drawTape", ui)
        self.assertNotIn("V2_SETTINGS_BUTTON_X", ui)
        self.assertNotIn("V2_HOME_TAPE_W", ui)

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

    def test_clock_is_prominent_in_full_and_clipped_header_draws(self):
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        header = ui.split("static void drawHeader", 1)[1].split(
            "static void drawStockHero", 1
        )[0]
        update = ui.split("void v2UiUpdateClock", 1)[1].split(
            "void v2UiUpdateHeader", 1
        )[0]

        self.assertIn(
            "drawString(snapshot.time, SCREEN_W / 2, 32, &SatoshiMedium18)",
            header,
        )
        self.assertIn(
            "drawString(time, SCREEN_W / 2, 32, &SatoshiMedium18)",
            update,
        )
        self.assertIn("constexpr int x = 170, y = 24, w = 140, h = 38", update)

    def test_clock_skips_identical_text_before_touching_the_rgb_framebuffer(self):
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        clock = ui.split("void v2UiUpdateClock", 1)[1].split("void v2UiUpdateStatus", 1)[0]
        self.assertIn("s_lastDrawnTime", ui)
        self.assertIn("strcmp", clock)
        self.assertLess(clock.index("strcmp"), clock.index("fillRect"))

    def test_deferred_redraws_keep_distant_regions_in_separate_clips(self):
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        deferred = ui.split("struct DeferredClip", 1)[1].split(
            "static void drawChevron", 1
        )[0]
        flush = deferred.split("void v2UiFlushDeferred()", 1)[1]

        self.assertIn("s_defClips[V2_MAX_DEFERRED_CLIPS]", deferred)
        self.assertIn("clipsOverlap", deferred)
        self.assertNotIn("unionDefClip", deferred)
        self.assertIn("nextDeferredClip", flush)
        self.assertEqual(flush.count("displayWaitVSync()"), 1)
        self.assertEqual(flush.count("pushSprite(0, 0)"), 1)

    def test_deferred_flush_sends_only_one_clip_per_vsync_and_preserves_pending(self):
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        deferred = ui.split("struct DeferredClip", 1)[1].split(
            "static void drawChevron", 1
        )[0]
        set_deferred = deferred.split("void v2UiSetDeferred", 1)[1].split(
            "void v2UiFlushDeferred", 1
        )[0]
        flush = deferred.split("void v2UiFlushDeferred()", 1)[1]

        self.assertNotIn("clip.active = false", set_deferred)
        self.assertIn("DeferredClip* clip = nextDeferredClip();", flush)
        self.assertIn("clip->active = false;", flush)
        self.assertNotIn("for (auto& clip : s_defClips)", flush)
        self.assertIn("clearDeferredClips();", ui.split("void v2UiDraw(", 1)[1])

    def test_display_push_diagnostics_are_exposed_on_health(self):
        display_h = (SRC / "display_manager.h").read_text(encoding="utf-8")
        display = (SRC / "display_manager.cpp").read_text(encoding="utf-8")
        server = (SRC / "config_server.cpp").read_text(encoding="utf-8")

        self.assertIn("struct DisplayDiagnostics", display_h)
        self.assertIn("displayRecordPush", display_h)
        self.assertIn("displayGetDiagnostics", display_h)
        self.assertIn("waitTimeouts", display_h)
        self.assertIn("maxPushUs", display_h)
        self.assertIn("displayRecordPush", display)
        health = server.split("static void sendHealthJson", 1)[1].split(
            "static void", 1
        )[0]
        self.assertIn("displayGetDiagnostics", health)
        for field in (
            '"vsyncCount"',
            '"waitCalls"',
            '"waitTimeouts"',
            '"pushCount"',
            '"pushedBytes"',
            '"lastPushUs"',
            '"maxPushUs"',
            '"lastPushBytes"',
            '"maxPushBytes"',
        ):
            self.assertIn(field, health)

    def test_rgb_pixel_clock_keeps_psram_bandwidth_headroom(self):
        panel = (SRC / "lgfx_matouch_40.h").read_text(encoding="utf-8")
        self.assertIn("cfg.freq_write = 12000000;", panel)

    def test_stock_rotation_splits_the_large_hero_across_vsync_frames(self):
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        runtime = (SRC / "v2_runtime.cpp").read_text(encoding="utf-8")
        update = ui.split("void v2UiUpdateStockHero", 1)[1].split(
            "void v2UiUpdateStockPrice", 1
        )[0]

        for clip in ("V2_HERO_TEXT_CLIP", "V2_HERO_GRAPH_CLIP"):
            self.assertIn(f"pushClip({clip}_X", update)
        self.assertNotIn("pushClip(V2_HERO_CLIP_X", update)

        rotation = runtime.split("if (rotation > 0", 1)[1].split(
            "if (v2ApplyTimeout", 1
        )[0]
        self.assertLess(
            rotation.index("v2UiUpdateStockHero"),
            rotation.index("requestNewsFetch"),
        )

    def test_live_price_updates_have_their_own_clipped_render_path(self):
        header = (SRC / "ui_v2_runtime.h").read_text(encoding="utf-8")
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        self.assertIn("v2UiUpdateBtcCard", header)
        self.assertIn("v2UiUpdateBtcCard", ui)
        self.assertIn("V2_HERO_CLIP", ui)

    def test_periodic_data_updates_never_invalidate_the_full_scene(self):
        runtime = (SRC / "v2_runtime.cpp").read_text(encoding="utf-8")
        btc_body = runtime.split("static void fetchBtcNow()", 1)[1].split("static void", 1)[0]
        self.assertNotIn("s_dirty = true", btc_body)
        stocks_block = runtime.split("if (stocksConsumeDirty())", 1)[1].split("uint32_t nowMs", 1)[0]
        self.assertNotIn("s_dirty = true", stocks_block)
        self.assertIn("v2UiUpdateData", stocks_block)
        rotation_block = runtime.split("if (rotation > 0", 1)[1].split("if (v2ApplyTimeout", 1)[0]
        self.assertNotIn("s_dirty = true", rotation_block)
        self.assertIn("v2UiUpdateStockHero", rotation_block)

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
        self.assertIn("V2_CARDS_CLIP", ui)


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
        self.assertIn('#include "data/lemon_v2_logo_black_120.h"', ui)
        self.assertIn("drawLoadingLogoProgress", ui)
        self.assertIn("lemon_v2_logo_black_120", ui)
        self.assertIn("V2_LOADING_LOGO_W", ui)
        self.assertIn("progress * 240", ui)
        self.assertNotIn("fillSmoothRoundRect(64, 286", loading)

    def test_loading_only_shows_logo_and_phase_text(self):
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        loading = ui.split("void v2UiDrawLoading", 1)[1].split("void v2UiDraw(", 1)[0]
        self.assertNotIn('drawString("LEMON BOX V2"', loading)
        self.assertNotIn('" / CANARY"', loading)

    def test_home_removes_technical_labels_and_sparkline_baseline(self):
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        pair = ui.split("static void drawStockHero", 1)[1].split("static void drawBtcCard", 1)[0]
        spark = ui.split("static void drawSparkline", 1)[1].split("static void drawChevron", 1)[0]
        self.assertNotIn("COTIZACION CRUZADA", ui)
        self.assertNotIn("CRIPTOYA", ui)
        self.assertNotIn("pairSource", pair)
        self.assertNotIn("drawFastHLine", spark)

    def test_prices_use_positive_and_negative_market_colors(self):
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        self.assertIn("pal->neg", ui)
        self.assertIn("trendColor", ui)
        self.assertIn("pairTrend", ui)
        self.assertIn("stock->quote.changePct", ui)

    def test_dollar_lemon_restores_buy_and_sell_with_freshness(self):
        runtime_header = (SRC / "v2_runtime.h").read_text(encoding="utf-8")
        runtime = (SRC / "v2_runtime.cpp").read_text(encoding="utf-8")
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        self.assertIn("lemonFreshness", runtime_header)
        self.assertIn("fetchLemonNow", runtime)
        for label in ("usdc_token_32", "COMPRA", "VENTA"):
            self.assertIn(label, ui)
        self.assertNotIn('"Dolar Digital"', ui)
        self.assertIn("CRIPTOYA_LEMON_EP", (SRC / "config.h").read_text(encoding="utf-8"))

    def test_pair_hero_has_real_sparkline_and_compact_large_numbers(self):
        runtime_header = (SRC / "v2_runtime.h").read_text(encoding="utf-8")
        runtime = (SRC / "v2_runtime.cpp").read_text(encoding="utf-8")
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        self.assertIn("SparklineData pairSpark", runtime_header)
        self.assertIn("wsBinanceGetSparkline", runtime)
        self.assertIn("drawSparkline", ui)
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


class V2MarketCardTests(unittest.TestCase):
    def setUp(self):
        self.ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        self.runtime = (SRC / "v2_runtime.cpp").read_text(encoding="utf-8")

    def test_cards_expand_to_the_bottom_safe_area_and_keep_clipped_updates(self):
        for token in (
            "V2_HOME_CARD_Y = 328",
            "V2_CARD_H = 120",
            "V2_CARDS_CLIP_Y = 324",
            "V2_CARDS_CLIP_H = 128",
        ):
            self.assertIn(token, self.ui)

        btc_update = self.ui.split("void v2UiUpdateBtcCard", 1)[1].split(
            "void v2UiUpdateBtcPrice", 1
        )[0]
        dollar_update = self.ui.split("void v2UiUpdateDollarCard", 1)[1].split(
            "void v2UiUpdateNews", 1
        )[0]
        for update in (btc_update, dollar_update):
            self.assertIn("V2_CARD_H", update)
            self.assertIn("pushClip", update)

        touch = self.runtime.split(
            "if (event.gesture == TOUCH_TAP && s_model.scene == V2_HOME)", 1
        )[1].split("handleSettingsAction", 1)[0]
        self.assertIn("event.y >= 328", touch)
        self.assertIn("event.y < 448", touch)

    def test_btc_card_uses_large_price_live_status_and_compact_sparkline(self):
        btc = self.ui.split("static void drawBtcCard", 1)[1].split(
            "static void drawDollarCard", 1
        )[0]

        self.assertIn("PPNeueMachinaBold24", btc)
        self.assertIn("cardPriceFont", btc)
        self.assertIn("pairFreshness", btc)
        self.assertIn("24H", btc)
        self.assertIn("drawCompactSparkline", btc)
        self.assertNotIn('"SIN SERIE"', btc)

    def test_cards_use_real_token_icons_instead_of_long_titles(self):
        btc = self.ui.split("static void drawBtcCard", 1)[1].split(
            "static void drawDollarCard", 1
        )[0]
        dollar = self.ui.split("static void drawDollarCard", 1)[1].split(
            "static void drawHome", 1
        )[0]

        self.assertIn('#include "data/btc_token_32.h"', self.ui)
        self.assertIn('#include "data/usdc_token_32.h"', self.ui)
        self.assertIn("btc_token_32", btc)
        self.assertIn("usdc_token_32", dollar)
        self.assertNotIn("pair.pairLabel", btc)
        self.assertNotIn('"Dolar Digital"', dollar)

        for asset in ("btc_token_32.h", "usdc_token_32.h"):
            source = (SRC / "data" / asset).read_text(encoding="utf-8")
            self.assertIn("[1024]", source)

    def test_btc_card_labels_only_alternative_pairs_next_to_the_icon(self):
        btc = self.ui.split("static void drawBtcCard", 1)[1].split(
            "static void drawDollarCard", 1
        )[0]

        self.assertIn("if (pairIndex > 0)", btc)
        self.assertIn('"VS %s"', btc)
        self.assertIn("pair.label", btc)
        self.assertNotIn("pair.pairLabel", btc)

    def test_btc_and_dollar_cards_make_24h_variation_prominent(self):
        btc = self.ui.split("static void drawBtcCard", 1)[1].split(
            "static void drawDollarCard", 1
        )[0]
        dollar = self.ui.split("static void drawDollarCard", 1)[1].split(
            "static void drawHome", 1
        )[0]

        self.assertIn("SatoshiMedium18", btc)
        self.assertIn('"24H"', btc)
        self.assertGreaterEqual(dollar.count("SatoshiMedium18"), 2)
        self.assertIn("formatArsPrice", dollar)
        self.assertIn("lemonChange24h", dollar)
        self.assertIn('"24H"', dollar)
        self.assertIn("V2_CARD_Y + 3", dollar)
        self.assertIn("V2_CARD_Y + 27", dollar)
        self.assertNotIn('"SPREAD', dollar)
        self.assertNotIn("spread", dollar.lower())
        self.assertNotIn("lemonFreshness", dollar)

    def test_dollar_variation_reuses_existing_one_day_history_at_slow_cadence(self):
        header = (SRC / "v2_runtime.h").read_text(encoding="utf-8")
        self.assertIn("SparklineData lemonSpark", header)
        self.assertIn("float lemonChange24h", header)
        self.assertIn("bool lemonChange24hValid", header)

        self.assertIn("fetchLemonSparkline(s_snapshot.lemonSpark, 1)", self.runtime)
        self.assertIn("UPDATE_SPARKLINE_MS", self.runtime)
        self.assertIn("v2UiUpdateDollarCard", self.runtime)

    def test_dollar_buy_and_sell_rows_are_centered_below_the_header(self):
        dollar = self.ui.split("static void drawDollarCard", 1)[1].split(
            "static void drawHome", 1
        )[0]
        for y in (45, 53, 74, 82):
            self.assertIn(f"V2_CARD_Y + {y}", dollar)
        for old_y in (35, 43, 64, 72):
            self.assertNotIn(f"V2_CARD_Y + {old_y}", dollar)

    def test_card_price_fonts_fit_representative_values(self):
        def advances(path):
            source = path.read_text(encoding="utf-8")
            return {
                chr(int(code, 16)): int(advance)
                for advance, code in re.findall(
                    r"\{\s*\d+,\s*\d+,\s*\d+,\s*(\d+),\s*-?\d+,\s*-?\d+\s*\},"
                    r"\s*// 0x([0-9A-F]+)",
                    source,
                )
            }

        pp24 = advances(SRC / "data" / "PPNeueMachinaBold24.h")
        sat18 = advances(SRC / "data" / "SatoshiMedium18.h")
        width = lambda value, font: sum(font[char] for char in value)

        for price in ("$999K",):
            self.assertLessEqual(width(price, pp24), 180)
        for price in ("$99,9M", "999,99", "999,9K"):
            self.assertLessEqual(width(price, sat18), 180)
        for price in ("$1.302", "$9.999"):
            self.assertLessEqual(width(price, sat18), 92)

    def test_btc_card_updates_at_most_once_per_second_for_real_data_changes(self):
        self.assertIn("V2_BTC_CARD_REFRESH_MS = 1000", self.runtime)
        ws = self.runtime.split("uint32_t wsPriceMs", 1)[1].split(
            "uint8_t rotation", 1
        )[0]
        self.assertIn("sparkChanged", ws)
        self.assertIn("displayChanged", ws)
        self.assertIn("s_lastBtcCardDrawMs", ws)
        self.assertIn("refreshSnapshot(true)", ws)
        self.assertIn("v2UiUpdateBtcCard", ws)
        self.assertIn("#define UPDATE_LEMON_MS         30000", (SRC / "config.h").read_text(encoding="utf-8"))

    def test_pair_sparkline_survives_price_snapshots_between_one_second_syncs(self):
        refresh = self.runtime.split("static void refreshPairSnapshot", 1)[1].split(
            "static void refreshSnapshot", 1
        )[0]
        self.assertIn("const bool pairChanged", refresh)
        self.assertIn("if (pairChanged) s_snapshot.pairSpark.valid = false;", refresh)
        self.assertIn("pairChanged ||", refresh)

    def test_freshness_labels_cover_loading_cache_offline_and_errors(self):
        marker = "static const char* freshnessLabel"
        self.assertIn(marker, self.ui)
        helper = self.ui.split(marker, 1)[1].split(
            "static void", 1
        )[0]
        for label in ('"CARGA"', '"LIVE"', '"CACHE"', '"OFFLINE"', '"ERROR"'):
            self.assertIn(label, helper)


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
        self.assertIn('#define APP_VERSION "5.1.1-beta.72"', config)

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

    def test_periodic_full_check_detects_updates_without_a_reboot(self):
        runtime = (SRC / "v2_runtime.cpp").read_text(encoding="utf-8")
        loop = runtime.split("void v2RuntimeLoop()", 1)[1]
        check = runtime.split("static void checkV2OtaNow", 1)[1].split(
            "static void installV2OtaNow", 1
        )[0]

        self.assertIn("V2_OTA_CHECK_MS = 5UL * 60UL * 1000UL", runtime)
        self.assertIn("checkV2OtaNow(false)", loop)
        self.assertNotIn("otaLatestTagChanged", loop)
        self.assertNotIn("stocksRequestBurst", check)

    def test_v2_update_notice_is_explicit_and_requires_confirmation(self):
        runtime_h = (SRC / "v2_runtime.h").read_text(encoding="utf-8")
        runtime = (SRC / "v2_runtime.cpp").read_text(encoding="utf-8")
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        settings = (SRC / "ui_v2_settings.cpp").read_text(encoding="utf-8")
        for field in ("otaChecked", "otaAvailable", "otaChecking", "otaArmed", "otaVersion"):
            self.assertIn(field, runtime_h)
        header = ui.split("static void drawHeader", 1)[1].split(
            "static void drawStockHero", 1
        )[0]
        self.assertIn("drawUpdateBadge", header)
        self.assertIn("snapshot.otaAvailable", header)
        self.assertIn('"UPDATE"', ui)
        self.assertIn("ACTUALIZACION", settings)
        self.assertIn("V%s", settings)
        self.assertIn("CONFIRMAR UPDATE", settings)
        self.assertIn("s_snapshot.otaArmed", runtime)
        self.assertIn("otaFlash", runtime)

    def test_home_update_badge_routes_to_firmware_settings_safely(self):
        runtime = (SRC / "v2_runtime.cpp").read_text(encoding="utf-8")
        handler = runtime.split("static void handleTouch()", 1)[1].split("void v2RuntimeSetup()", 1)[0]
        badge = runtime.split("static bool handleHomeUpdateTap", 1)[1].split(
            "static void handleTouch", 1
        )[0]

        self.assertIn("s_model.scene = V2_SETTINGS", badge)
        self.assertIn("s_model.settingsPage = 2", badge)
        self.assertNotIn("installV2OtaNow", badge)
        self.assertNotIn("otaArmed", badge)
        self.assertIn("handleHomeUpdateTap", handler)


class V2DirectTouchNavigationTests(unittest.TestCase):
    def test_home_uses_a_drawn_settings_icon_instead_of_hidden_instruction(self):
        ui = (SRC / "ui_v2_runtime.cpp").read_text(encoding="utf-8")
        self.assertIn("drawSettingsIcon", ui)
        self.assertNotIn("drawSettingsButton", ui)
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
        self.assertIn("v2UiUpdateHeader", check_body)
        self.assertNotIn("else s_dirty = true", check_body)


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
        self.assertNotIn("TOCA PARA CAMBIAR", ui)
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
