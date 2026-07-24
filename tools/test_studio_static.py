import os
import unittest


ROOT = os.path.dirname(os.path.dirname(__file__))
STATIC = os.path.join(ROOT, "studio", "static")


class StudioStaticTest(unittest.TestCase):
    def test_static_assets_exist_and_use_local_api(self):
        required = ["index.html", "styles.css", "app.js", "workbench3d.js", "screen_renderer.js"]
        for name in required:
            self.assertTrue(os.path.exists(os.path.join(STATIC, name)), name)

        with open(os.path.join(STATIC, "app.js"), "r", encoding="utf-8") as f:
            app_js = f.read()
        self.assertIn("/api/status", app_js)
        self.assertIn("/api/capabilities", app_js)
        self.assertIn("/api/sources", app_js)
        self.assertIn("/api/sources/preview", app_js)
        self.assertIn("/api/experiences", app_js)
        self.assertIn("/api/devices", app_js)
        self.assertIn("/api/device/settings", app_js)
        self.assertIn("/api/device/connect", app_js)
        self.assertIn("/api/device/pair", app_js)
        self.assertIn("/api/device/health", app_js)
        self.assertIn("/api/jobs/sync-settings", app_js)
        self.assertIn("/api/jobs/ota-upload", app_js)
        self.assertIn("/api/jobs/ota-latest", app_js)
        self.assertIn("/api/jobs/ota-verify", app_js)
        self.assertIn("/api/jobs/flash", app_js)
        self.assertIn("/api/jobs/agent-draft", app_js)
        self.assertIn("/api/jobs/deploy-draft", app_js)
        self.assertIn("/api/manifest", app_js)
        self.assertIn("/api/keys", app_js)
        self.assertIn("cards", app_js)
        self.assertIn("requires-firmware-runtime", app_js)
        self.assertIn("WiFi device", app_js)
        self.assertIn("partial", app_js)
        self.assertIn("OTA health verified", app_js)
        self.assertIn("advanced-toggle", app_js)
        self.assertIn("Modo tecnico", app_js)
        self.assertIn("visibleCardKinds", app_js)
        self.assertIn("renderDeviceSummary", app_js)
        self.assertIn("wifiSignal", app_js)
        self.assertIn("formatDuration", app_js)
        self.assertIn("saveSourceConfig", app_js)
        self.assertIn("sourceEditorKind", app_js)
        self.assertIn("item.firmwareSupported", app_js)
        self.assertIn("targetAddressSpace", app_js)
        self.assertIn("lemon-studio-web-device", app_js)
        self.assertIn("deviceFromQuery", app_js)
        self.assertIn("webDeviceFetch", app_js)
        self.assertIn("scanNetworkForDevice", app_js)
        self.assertIn("STOCK_OPTIONS", app_js)
        self.assertIn("renderStockPicker", app_js)
        self.assertIn("autoPairDevice", app_js)
        self.assertIn("syncWatchlistSource", app_js)
        self.assertIn("/api/ota/github", app_js)
        self.assertIn("sourceKindLabel", app_js)
        self.assertIn("Datos listos", app_js)
        self.assertIn("Sincronizada", app_js)
        self.assertIn("Previsualizando", app_js)
        self.assertNotIn("confirm(", app_js)

    def test_index_does_not_pull_external_dependencies(self):
        with open(os.path.join(STATIC, "index.html"), "r", encoding="utf-8") as f:
            html = f.read().lower()

        self.assertNotIn("https://", html)
        self.assertIn("lemon box studio", html)
        self.assertIn("workbench-stage", html)
        self.assertIn("studio-nav", html)
        self.assertIn("source-catalog", html)
        self.assertIn("device-readiness", html)
        self.assertIn("agent-timeline", html)
        self.assertIn("device-viewer", html)
        self.assertIn("screen-editor", html)
        self.assertIn("advanced-toggle", html)
        self.assertIn(">control<", html)
        self.assertIn("modo secundario", html)
        self.assertIn("watchlist", html)
        self.assertIn("aplicar", html)
        self.assertIn("stock-picker", html)
        self.assertIn("buscar cajita", html)
        self.assertIn("buscar actualizacion", html)
        self.assertNotIn("que cambia la cajita", html)
        self.assertNotIn("si funciona", html)
        self.assertNotIn("limitado", html)
        self.assertNotIn("brillo solo cambia", html)
        self.assertIn("data-advanced", html)
        self.assertIn("data-labs", html)
        self.assertIn("herramientas tecnicas", html)
        self.assertIn("ota local", html)
        self.assertIn("flash usb normal", html)
        self.assertIn("data-labs", html)
        self.assertIn("device-summary", html)
        self.assertIn("conectar", html)
        self.assertNotIn("pair-device", html)
        self.assertNotIn(">vincular<", html)
        self.assertNotIn("<span>pantalla</span>", html)
        self.assertIn(">aplicar<", html)
        self.assertIn("senal wifi", html)

        with open(os.path.join(STATIC, "styles.css"), "r", encoding="utf-8") as f:
            css = f.read()
        self.assertIn('body:not([data-advanced="true"]) [data-advanced]', css)
        self.assertIn("[data-labs]", css)
        self.assertIn(".device-summary", css)
        self.assertIn('url("./fonts/Satoshi-Regular.woff2")', css)
        self.assertIn("grid-template-areas:", css)
        self.assertIn('"stage right"', css)
        self.assertIn('"right"', css)

    def test_three_vendor_and_models_are_local(self):
        required = [
            os.path.join(STATIC, "vendor", "three", "three.module.js"),
            os.path.join(STATIC, "vendor", "three", "examples", "jsm", "loaders", "STLLoader.js"),
            os.path.join(STATIC, "vendor", "three", "examples", "jsm", "controls", "OrbitControls.js"),
            os.path.join(STATIC, "vendor", "three", "examples", "jsm", "environments", "RoomEnvironment.js"),
            os.path.join(STATIC, "models", "Lemon-box-with-screen.stl"),
            os.path.join(STATIC, "models", "Lemon-box-lid-new.stl"),
            os.path.join(STATIC, "models", "Lemon-box-lid-logo-new.stl"),
        ]
        for path in required:
            self.assertTrue(os.path.exists(path), path)

    def test_brand_and_fonts_are_local(self):
        required = [
            os.path.join(STATIC, "brand", "lemon_imagotipo_load.png"),
            os.path.join(STATIC, "fonts", "Satoshi-Regular.woff2"),
            os.path.join(STATIC, "fonts", "Satoshi-Medium.woff2"),
            os.path.join(STATIC, "fonts", "Satoshi-Bold.woff2"),
        ]
        for path in required:
            self.assertTrue(os.path.exists(path), path)

    def test_workbench_screen_uses_sloped_stl_plane(self):
        with open(os.path.join(STATIC, "workbench3d.js"), "r", encoding="utf-8") as f:
            workbench = f.read()

        self.assertIn("SCREEN_PLANE_SLOPE_Z", workbench)
        self.assertIn("SCREEN_PLANE_INTERCEPT", workbench)
        self.assertIn("addScaledVector(frontNormal, SCREEN_PLANE_LIFT)", workbench)
        self.assertNotIn("box.max.y + 0.16", workbench)

    def test_workbench_camera_presets_are_wired(self):
        with open(os.path.join(STATIC, "index.html"), "r", encoding="utf-8") as f:
            html = f.read()
        with open(os.path.join(STATIC, "app.js"), "r", encoding="utf-8") as f:
            app_js = f.read()
        with open(os.path.join(STATIC, "workbench3d.js"), "r", encoding="utf-8") as f:
            workbench = f.read()

        self.assertIn('data-view="front"', html)
        self.assertIn('data-view="screen"', html)
        self.assertIn('data-view="side"', html)
        self.assertIn('id="screen-focus"', html)
        self.assertIn("setCameraView", app_js)
        self.assertIn("setCameraView", workbench)
        self.assertIn("screenBasis", workbench)

    def test_screen_preview_matches_firmware_zones(self):
        with open(os.path.join(STATIC, "screen_renderer.js"), "r", encoding="utf-8") as f:
            renderer = f.read()

        self.assertIn("drawFirmwareScreen", renderer)
        self.assertIn("firmwareZones", renderer)
        self.assertIn('x: 16, y: 267, w: 448, h: 213', renderer)
        self.assertNotIn('x: 246, y: 286, w: 210, h: 144', renderer)

    def test_simplification_decisions_are_documented(self):
        doc = os.path.join(ROOT, "docs", "studio-v1-simplification.md")
        self.assertTrue(os.path.exists(doc), doc)
        with open(doc, "r", encoding="utf-8") as f:
            text = f.read().lower()

        self.assertIn("visible by default", text)
        self.assertIn("visible in technical mode", text)
        self.assertIn("hidden from ui but documented as labs", text)
        self.assertIn("functional status", text)
        self.assertIn("recovery flashing", text)
        self.assertIn("arbitrary custom cards", text)


if __name__ == "__main__":
    unittest.main()
