import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


class RuntimeStabilityTests(unittest.TestCase):
    def test_live_ws_sparkline_uses_stable_range_before_render(self):
        main = (SRC / "main.cpp").read_text(encoding="utf-8")

        self.assertIn("stabilizeLiveSparklineRange", main)
        ws_sync = main[main.index("wsBinanceGetSparkline(tmp);"):]
        self.assertLess(
            ws_sync.index("stabilizeLiveSparklineRange(tmp, state.spark);"),
            ws_sync.index("state.spark = tmp;"),
        )

    def test_stocks_burst_releases_shared_tls_on_all_entry_paths(self):
        main = (SRC / "main.cpp").read_text(encoding="utf-8")

        self.assertIn("requestStocksBurst", main)
        helper = re.search(
            r"static void requestStocksBurst\(\) \{(?P<body>.*?)\n\}",
            main,
            re.S,
        )
        self.assertIsNotNone(helper)
        body = helper.group("body")
        self.assertLess(body.index("apiStop();"), body.index("stocksRequestBurst();"))

        apply_mode = main[main.index("static void applyZ2Mode"):]
        self.assertIn("if (stocksOn) requestStocksBurst();", apply_mode)

    def test_stocks_z2_renders_from_atomic_snapshot(self):
        header = (SRC / "ui_stocks.h").read_text(encoding="utf-8")
        dashboard = (SRC / "ui_dashboard.cpp").read_text(encoding="utf-8")
        stocks = (SRC / "ui_stocks.cpp").read_text(encoding="utf-8")

        self.assertIn("bool stocksGetFocusedSnapshot", header)
        self.assertIn("stocksGetFocusedSnapshot", dashboard)
        stocks_card = dashboard[dashboard.index("void dashboardDrawStocksZ2()"):]
        self.assertNotIn("stocksGetFocusedQuote()", stocks_card)
        self.assertNotIn("stocksGetFocusedSpark()", stocks_card)
        self.assertIn("SemaphoreHandle_t", stocks)
        self.assertIn("xSemaphoreCreateMutex", stocks)

    def test_dollar_morph_uses_chart_only_redraw(self):
        header = (SRC / "ui_dashboard.h").read_text(encoding="utf-8")
        dashboard = (SRC / "ui_dashboard.cpp").read_text(encoding="utf-8")
        main = (SRC / "main.cpp").read_text(encoding="utf-8")

        self.assertIn("dashboardRedrawDollarChartOnly", header)
        self.assertIn("void dashboardRedrawDollarChartOnly", dashboard)
        dollar_morph = main[main.index("if (dollarMorphActive"):]
        self.assertIn("dashboardRedrawDollarChartOnly(state.lemon, morphed, dollarChartStyle);", dollar_morph)
        self.assertNotIn("dashboardDrawLemonDollar(state.lemon, &morphed", dollar_morph)

    def test_stock_spark_cache_uses_compact_nvs_format(self):
        nvs = (SRC / "nvs_storage.cpp").read_text(encoding="utf-8")

        self.assertIn("STOCK_SPARK_CACHE_POINTS", nvs)
        self.assertIn("struct StockSparkCacheEntry", nvs)
        self.assertNotIn("sizeof(SparklineData) * STOCK_MAX_SYMBOLS", nvs)
        self.assertIn('prefs.remove("sp_syms")', nvs)

    def test_regular_stock_refresh_releases_api_tls_before_worker_fetch(self):
        stocks = (SRC / "ui_stocks.cpp").read_text(encoding="utf-8")

        self.assertIn('#include "api_client.h"', stocks)
        task_body = stocks[stocks.index("void stocksFetchTask()"):]
        self.assertIn("STOCKS_REGULAR_REFRESH_MIN_MS", stocks)
        self.assertIn("regularFresh", task_body)
        self.assertLess(task_body.index("apiStop();"), task_body.index("xTaskNotifyGive(worker);"))

    def test_regular_stock_failures_do_not_start_retry_burst(self):
        stocks = (SRC / "ui_stocks.cpp").read_text(encoding="utf-8")

        failure_block = stocks[stocks.index("Serial.printf(\"[Stocks] fetch failed"):]
        self.assertIn("if (fromBurst && s_burstRetryCount[target] < STOCKS_MAX_BURST_RETRIES)", failure_block)

    def test_stocks_worker_stays_fetching_during_chained_burst(self):
        stocks = (SRC / "ui_stocks.cpp").read_text(encoding="utf-8")

        worker = stocks[stocks.index("static void stocksWorkerTask"):]
        self.assertIn("if (pending == 0) s_fetching = false;", worker)

    def test_main_https_refreshes_yield_while_stocks_fetching(self):
        main = (SRC / "main.cpp").read_text(encoding="utf-8")

        for fn in ("updateBtc", "updateSparkline", "updateLemon", "updateDollarSparkline", "updateCrossRate"):
            body = main[main.index(f"static void {fn}()"):]
            self.assertIn("if (stocksIsFetching()) return;", body[:500])

    def test_pair_selector_remains_visible_while_pair_price_loads(self):
        dashboard = (SRC / "ui_dashboard.cpp").read_text(encoding="utf-8")

        hero = dashboard[dashboard.index("void dashboardDrawBtcHero"):]
        self.assertLess(
            hero.index("sprZ1.drawString(pair.pairLabel"),
            hero.index("if (!btc.valid)"),
        )
        invalid_block = hero[hero.index("if (!btc.valid)"):hero.index("const lgfx::IFont* priceFont")]
        self.assertIn("drawPairDropdown(sprZ1, pairDropdownSelected);", invalid_block)

    def test_candlestick_availability_requires_supported_pair_and_period(self):
        main = (SRC / "main.cpp").read_text(encoding="utf-8")

        self.assertIn("static bool pairSupportsCandles", main)
        self.assertIn("static bool currentSelectionSupportsCandles", main)

        switch_pair = main[main.index("static void switchPair"):main.index("static void onDashboardTouch")]
        self.assertIn("!pairSupportsCandles(pair)", switch_pair)
        self.assertNotIn("pair.source == PAIR_GECKO_ONLY && chartStyle == CHART_CANDLE", switch_pair)

        touch = main[main.index("Tap outside both carousels: cycle chart style"):]
        self.assertIn("findNearestVisibleCandlePeriod", touch)
        self.assertIn("currentSelectionSupportsCandles()", touch)

        update_spark = main[main.index("static void updateSparkline()"):main.index("static void updateLemon()")]
        self.assertIn("currentSelectionSupportsCandles()", update_spark)

    def test_scheduler_request_run_is_due_on_next_tick(self):
        scheduler = (SRC / "scheduler.cpp").read_text(encoding="utf-8")

        request_run = scheduler[scheduler.index("void Scheduler::requestRun"):]
        self.assertIn("millis() - tasks[id].intervalMs", request_run)
        self.assertNotIn("tasks[id].lastRun = 0;", request_run)

    def test_polymarket_entry_requests_immediate_scheduler_run(self):
        main = (SRC / "main.cpp").read_text(encoding="utf-8")

        start = main.index("static void enterPredictionMode() {")
        end = main.index("static void exitPredictionMode", start)
        enter_prediction = main[start:end]
        self.assertIn("scheduler.requestRun(taskPolymarket);", enter_prediction)

    def test_wifi_dns_override_is_reapplied_after_reconnects(self):
        wifi = (SRC / "wifi_manager.cpp").read_text(encoding="utf-8")

        self.assertIn("applyPublicDns", wifi)
        self.assertIn("static bool dnsApplied", wifi)
        self.assertIn("DNS override", wifi)

        setup = wifi[wifi.index("void wifiSetup"):wifi.index("void wifiLoop")]
        self.assertIn("applyPublicDns();", setup)

        loop = wifi[wifi.index("void wifiLoop"):wifi.index("bool wifiConnected")]
        self.assertIn("if (!dnsApplied)", loop)
        self.assertIn("applyPublicDns();", loop)
        self.assertIn("dnsApplied = false;", loop)

        async_connect = wifi[wifi.index("void wifiConnectAsync"):wifi.index("bool wifiConnecting")]
        self.assertIn("dnsApplied = false;", async_connect)

        connecting = wifi[wifi.index("bool wifiConnecting"):wifi.index("bool wifiConnectSucceeded")]
        self.assertIn("applyPublicDns();", connecting)

    def test_saved_wifi_retries_even_when_initial_boot_connection_failed(self):
        wifi = (SRC / "wifi_manager.cpp").read_text(encoding="utf-8")
        setup = wifi[wifi.index("void wifiSetup"):wifi.index("void wifiLoop")]
        loop = wifi[wifi.index("void wifiLoop"):wifi.index("bool wifiConnected")]

        self.assertNotIn("if (!everConnected) return", loop)
        self.assertNotIn("WiFi.disconnect(true)", setup)
        self.assertIn("WiFi.begin(storedSSID, storedPass);", loop)
        self.assertIn("RECONNECT_MAX", loop)

    def test_stocks_deactivation_cancels_pending_burst_without_killing_inflight_fetch(self):
        header = (SRC / "ui_stocks.h").read_text(encoding="utf-8")
        stocks = (SRC / "ui_stocks.cpp").read_text(encoding="utf-8")
        main = (SRC / "main.cpp").read_text(encoding="utf-8")

        self.assertIn("void stocksSetActive(bool active);", header)
        self.assertIn("static volatile bool  s_active", stocks)

        set_active = stocks[stocks.index("void stocksSetActive"):stocks.index("// Kick a burst refresh", stocks.index("void stocksSetActive"))]
        self.assertIn("s_active = active;", set_active)
        self.assertIn("s_burstPending = 0;", set_active)
        self.assertIn("s_priorityIdx = 0xFF;", set_active)
        self.assertNotIn("vTaskDelete", set_active)

        fetch_task = stocks[stocks.index("void stocksFetchTask"):]
        self.assertIn("if (!active)", fetch_task)

        worker = stocks[stocks.index("static void stocksWorkerTask"):]
        self.assertIn("pending != 0 && active", worker)

        apply_mode = main[main.index("static void applyZ2Mode"):]
        self.assertLess(apply_mode.index("stocksSetActive(stocksOn);"), apply_mode.index("if (stocksOn) requestStocksBurst();"))

    def test_polymarket_waits_for_stocks_worker_and_retries_when_idle(self):
        main = (SRC / "main.cpp").read_text(encoding="utf-8")

        self.assertIn("static bool  polyDeferredByStocks", main)

        update_poly = main[main.index("static void updatePolymarket()"):main.index("// ── Enter/exit prediction mode")]
        self.assertIn("if (stocksIsFetching())", update_poly)
        self.assertIn("polyDeferredByStocks = true;", update_poly)
        self.assertIn("drawPredictionUI(true);", update_poly)
        self.assertLess(update_poly.index("if (stocksIsFetching())"), update_poly.index("resolveActivePredictionIfClosed"))

        loop = main[main.index("void loop()"):]
        self.assertIn("polyDeferredByStocks && !stocksIsFetching()", loop)
        self.assertIn("scheduler.requestRun(taskPolymarket);", loop)

    def test_yahoo_chart_parser_uses_filtered_meta_and_close_array(self):
        stocks = (SRC / "stocks_client.cpp").read_text(encoding="utf-8")

        parser = stocks[stocks.index("static DeserializationError parseYahooChartJson"):stocks.index("ApiResult fetchStockChart")]
        self.assertIn('filter["chart"]["result"][0]["meta"]["symbol"] = true;', parser)
        self.assertIn('filter["chart"]["result"][0]["meta"]["regularMarketPrice"] = true;', parser)
        self.assertIn('filter["chart"]["result"][0]["indicators"]["quote"][0]["close"][0] = true;', parser)
        self.assertIn("DeserializationOption::Filter(filter)", parser)
        self.assertNotIn("just let it all through", parser)

    def test_stocks_recreates_worker_after_settings_update_check_stops_it(self):
        stocks = (SRC / "ui_stocks.cpp").read_text(encoding="utf-8")

        self.assertIn("static void ensureStocksWorkerRunning()", stocks)
        set_active = stocks[stocks.index("void stocksSetActive"):stocks.index("// Actual fetch body")]
        self.assertIn("if (active) ensureStocksWorkerRunning();", set_active)

        request_burst = stocks[stocks.index("void stocksRequestBurst"):]
        self.assertIn("ensureStocksWorkerRunning();", request_burst)
        self.assertLess(request_burst.index("ensureStocksWorkerRunning();"), request_burst.index("worker = s_workerHandle;"))

    def test_yahoo_filtered_parse_has_full_parse_fallback_when_close_array_is_empty(self):
        stocks = (SRC / "stocks_client.cpp").read_text(encoding="utf-8")

        chart = stocks[stocks.index("ApiResult fetchStockChart"):]
        self.assertIn("parseYahooChartJson", stocks)
        self.assertIn("filtered parse missing close array", chart)
        self.assertIn("parseYahooChartJson(json, doc, false)", chart)
        self.assertLess(
            chart.index("parseYahooChartJson(json, doc, true)"),
            chart.index("parseYahooChartJson(json, doc, false)"),
        )

    def test_settings_actions_fit_without_cutting_bottom_buttons(self):
        settings = (SRC / "ui_settings.cpp").read_text(encoding="utf-8")

        self.assertIn("#define ROW_H           42", settings)
        self.assertIn("#define SCARD_Y         48", settings)
        self.assertIn("#define SECONDARY_BTN_Y", settings)
        self.assertIn("#define RESET_BTN_W", settings)
        self.assertIn("#define TUTORIAL_BTN_X", settings)

        self.assertNotIn("#define TUTORIAL_BTN_Y (BTN_START_Y + 2 * (BTN_H + BTN_GAP))", settings)
        self.assertIn("touchInRect(tx, cy, RESET_BTN_X, RESET_BTN_Y, RESET_BTN_W, RESET_BTN_H)", settings)
        self.assertIn("touchInRect(tx, cy, TUTORIAL_BTN_X, TUTORIAL_BTN_Y, TUTORIAL_BTN_W, TUTORIAL_BTN_H)", settings)

        self.assertIn("static_assert(CONTENT_TOTAL <= SCREEN_H", settings)


if __name__ == "__main__":
    unittest.main()
