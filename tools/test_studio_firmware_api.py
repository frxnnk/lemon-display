import os
import unittest


ROOT = os.path.dirname(os.path.dirname(__file__))


class StudioFirmwareApiTest(unittest.TestCase):
    def test_config_server_exposes_json_settings_and_watchlist_routes(self):
        with open(os.path.join(ROOT, "src", "config_server.cpp"), "r", encoding="utf-8") as f:
            config_server = f.read()

        self.assertIn("#include <ArduinoJson.h>", config_server)
        self.assertIn('"/api/settings"', config_server)
        self.assertIn('"/api/watchlist"', config_server)
        self.assertIn("HTTP_GET", config_server)
        self.assertIn("HTTP_POST", config_server)
        self.assertIn("deserializeJson", config_server)
        self.assertIn("nvsSetBrightness", config_server)
        self.assertIn("nvsSetTheme", config_server)
        self.assertIn("nvsSetLayout", config_server)
        self.assertIn("appApplyZ2Mode", config_server)
        self.assertIn("nvsSaveWatchlist", config_server)
        self.assertIn("stocksSetActive(true)", config_server)
        self.assertIn("stocksRequestBurst()", config_server)
        self.assertIn("OTA_GITHUB_REPO", config_server)

    def test_html_watchlist_page_is_preserved(self):
        with open(os.path.join(ROOT, "src", "config_server.cpp"), "r", encoding="utf-8") as f:
            config_server = f.read()

        self.assertIn("WATCHLIST_HTML", config_server)
        self.assertIn('"/watchlist"', config_server)
        self.assertIn("renderWatchlistPage", config_server)

    def test_wireless_pairing_and_health_routes_are_present(self):
        with open(os.path.join(ROOT, "src", "config_server.cpp"), "r", encoding="utf-8") as f:
            config_server = f.read()
        with open(os.path.join(ROOT, "src", "nvs_storage.h"), "r", encoding="utf-8") as f:
            nvs_header = f.read()

        self.assertIn('"/api/device"', config_server)
        self.assertIn('"/api/health"', config_server)
        self.assertIn('"/api/pair/start"', config_server)
        self.assertIn('"/api/pair/confirm"', config_server)
        self.assertIn("Authorization", config_server)
        self.assertIn("Bearer ", config_server)
        self.assertIn("ESP.getEfuseMac", config_server)
        self.assertIn("ESP.getFreeHeap", config_server)
        self.assertIn("nvsGetPairingToken", nvs_header)
        self.assertIn("nvsSetPairingToken", nvs_header)

    def test_web_studio_routes_support_cors_and_local_network_access(self):
        with open(os.path.join(ROOT, "src", "config_server.cpp"), "r", encoding="utf-8") as f:
            config_server = f.read()

        self.assertIn("HTTP_OPTIONS", config_server)
        for route in [
            '"/api/device"',
            '"/api/health"',
            '"/api/pair/start"',
            '"/api/pair/confirm"',
            '"/api/settings"',
            '"/api/watchlist"',
            '"/api/ota/github"',
            '"/api/ota/status"',
        ]:
            self.assertIn(route, config_server)
        self.assertIn("Access-Control-Allow-Origin", config_server)
        self.assertIn("https://lemon-box-landing.vercel.app", config_server)
        self.assertIn("http://127.0.0.1:8765", config_server)
        self.assertIn("Access-Control-Allow-Private-Network", config_server)
        self.assertIn("Private-Network-Access-Name", config_server)
        self.assertIn("Private-Network-Access-ID", config_server)
        self.assertIn("handleCorsOptions", config_server)

    def test_wireless_ota_routes_are_present_and_recovery_stays_usb_only(self):
        with open(os.path.join(ROOT, "src", "config_server.cpp"), "r", encoding="utf-8") as f:
            config_server = f.read()

        self.assertIn("#include <Update.h>", config_server)
        self.assertIn("#include \"ota_manager.h\"", config_server)
        self.assertIn('"/api/ota/arm"', config_server)
        self.assertIn('"/api/ota/upload"', config_server)
        self.assertIn('"/api/ota/github"', config_server)
        self.assertIn('"/api/ota/status"', config_server)
        self.assertIn("handleOtaUploadBody", config_server)
        self.assertIn("Update.begin(expected)", config_server)
        self.assertIn("otaFlash", config_server)
        self.assertNotIn("bootloader.bin", config_server)
        self.assertNotIn("partitions.bin", config_server)


if __name__ == "__main__":
    unittest.main()
