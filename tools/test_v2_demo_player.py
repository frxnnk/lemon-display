import configparser
import re
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


class V2DemoBuildContractTests(unittest.TestCase):
    def test_demo_has_a_dedicated_build_environment(self):
        parser = configparser.ConfigParser(interpolation=None)
        parser.read(ROOT / "platformio.ini", encoding="utf-8")

        demo_section = "env:matouch_esp32s3_40_v2_demo"
        self.assertIn(demo_section, parser.sections())
        self.assertEqual(
            parser[demo_section].get("extends"),
            "env:matouch_esp32s3_40",
        )
        self.assertIn("-DLEMON_V2_DEMO_MODE=1", parser[demo_section]["build_flags"])

    def test_demo_mode_is_off_by_default(self):
        config = (SRC / "config.h").read_text(encoding="utf-8")
        default = re.search(
            r"#ifndef LEMON_V2_DEMO_MODE\s+"
            r"#define LEMON_V2_DEMO_MODE\s+(\d+)\s+"
            r"#endif",
            config,
        )
        self.assertIsNotNone(default)
        self.assertEqual(default.group(1), "0")


class V2DemoTimelineContractTests(unittest.TestCase):
    def test_timeline_has_nine_contiguous_scenes_and_lasts_78_seconds(self):
        path = SRC / "v2_demo_timeline.h"
        self.assertTrue(path.exists(), "v2_demo_timeline.h is missing")
        source = path.read_text(encoding="utf-8")

        table = source[
            source.index("V2_DEMO_TIMELINE[]"):source.index("V2_DEMO_SCENE_COUNT")
        ]
        entries = re.findall(
            r"\{\s*(V2_DEMO_[A-Z_]+),\s*(\d+),\s*(\d+)\s*\}",
            table,
        )
        self.assertEqual(
            [entry[0] for entry in entries],
            [
                "V2_DEMO_HOME",
                "V2_DEMO_PREOPEN",
                "V2_DEMO_OPEN",
                "V2_DEMO_TAPE",
                "V2_DEMO_PACK",
                "V2_DEMO_NEWS",
                "V2_DEMO_CONTEXT",
                "V2_DEMO_QR",
                "V2_DEMO_RETURN",
            ],
        )

        ranges = [(int(start), int(end)) for _, start, end in entries]
        self.assertEqual(ranges[0][0], 0)
        self.assertTrue(all(left[1] == right[0] for left, right in zip(ranges, ranges[1:])))
        self.assertEqual(ranges[-1][1], 78000)
        self.assertIn("V2_DEMO_DURATION_MS = 78000", source)

    def test_timeline_exposes_scene_lookup_and_scene_start(self):
        path = SRC / "v2_demo_timeline.h"
        self.assertTrue(path.exists(), "v2_demo_timeline.h is missing")
        source = path.read_text(encoding="utf-8")

        self.assertIn("v2DemoFrameAt", source)
        self.assertIn("v2DemoSceneStartMs", source)
        self.assertIn("elapsedMs % V2_DEMO_DURATION_MS", source)


class V2DemoAssetContractTests(unittest.TestCase):
    def test_primary_font_is_generated_from_the_official_pp_neue_machina_file(self):
        path = SRC / "data" / "PPNeueMachinaBold24.h"
        self.assertTrue(path.exists(), "PP Neue Machina firmware font is missing")
        source = path.read_text(encoding="utf-8")

        self.assertIn("Converted from: PPNeueMachina-PlainBold.ttf", source)
        self.assertIn("Characters: 0x20-0x7E", source)
        self.assertIn("const GFXfont PPNeueMachinaBold24 PROGMEM", source)

    def test_v2_logos_are_exactly_120_by_28_rgb565_assets(self):
        assets = {
            "lemon_v2_logo_light_120.h": "lemon_v2_logo_light_120",
            "lemon_v2_logo_black_120.h": "lemon_v2_logo_black_120",
        }
        for filename, array_name in assets.items():
            with self.subTest(filename=filename):
                path = SRC / "data" / filename
                self.assertTrue(path.exists(), f"{filename} is missing")
                source = path.read_text(encoding="utf-8")
                self.assertIn("Size: 120x28, Format: RGB565", source)
                self.assertRegex(
                    source,
                    rf"static const uint16_t {array_name}\[3360\] PROGMEM",
                )


class V2DemoUiContractTests(unittest.TestCase):
    def test_ui_uses_verified_tokens_safe_area_and_frozen_fixtures(self):
        path = SRC / "ui_v2_demo.cpp"
        self.assertTrue(path.exists(), "ui_v2_demo.cpp is missing")
        source = path.read_text(encoding="utf-8")

        for expected in (
            "V2_BLACK = 0x1082",
            "V2_MAIN_GREEN = 0x06E3",
            "V2_LIME_YELLOW = 0xCFE6",
            "V2_SAFE_INSET = 48",
            '"$ 118.420"',
            '"S&P 500"',
            '"NASDAQ"',
            '"AAPL"',
            '"NVDA"',
            '"PACK IA"',
            '"NVIDIA"',
            "DATOS DE DEMO",
        ):
            self.assertIn(expected, source)

    def test_ui_is_offline_and_has_no_trading_language(self):
        path = SRC / "ui_v2_demo.cpp"
        self.assertTrue(path.exists(), "ui_v2_demo.cpp is missing")
        source = path.read_text(encoding="utf-8")
        lowered = source.lower()

        self.assertNotIn("wifi.h", lowered)
        self.assertNotIn("api_client", lowered)
        self.assertNotIn("httpclient", lowered)
        self.assertNotIn("comprar", lowered)
        self.assertNotIn("vender", lowered)
        self.assertIn("V2_DEMO_DEEP_LINK", source)

    def test_ui_exposes_setup_tick_and_touch_with_local_qr(self):
        header_path = SRC / "ui_v2_demo.h"
        source_path = SRC / "ui_v2_demo.cpp"
        self.assertTrue(header_path.exists(), "ui_v2_demo.h is missing")
        self.assertTrue(source_path.exists(), "ui_v2_demo.cpp is missing")
        header = header_path.read_text(encoding="utf-8")
        source = source_path.read_text(encoding="utf-8")

        self.assertIn("void v2DemoSetup();", header)
        self.assertIn("void v2DemoTick(uint32_t nowMs);", header)
        self.assertIn("void v2DemoHandleTouch", header)
        self.assertIn("LGFX_Sprite demoSpr(&tft)", source)
        self.assertIn("demoSpr.setPsram(true)", source)
        self.assertIn("qrcode_initText", source)
        self.assertIn("V2_DEMO_NEWS", source)
        self.assertIn("V2_DEMO_CONTEXT", source)
        self.assertIn("V2_DEMO_QR", source)


class V2DemoIntegrationContractTests(unittest.TestCase):
    def test_setup_enters_demo_before_nvs_wifi_or_api_initialization(self):
        main = (SRC / "main.cpp").read_text(encoding="utf-8")
        self.assertIn('#include "ui_v2_demo.h"', main)
        setup = main[main.index("void setup()"):main.index("void loop()")]
        self.assertIn("#if LEMON_V2_DEMO_MODE", setup)
        self.assertIn("v2DemoSetup();", setup)
        self.assertLess(setup.index("v2DemoSetup();"), setup.index("nvsInit();"))
        demo_branch = setup[setup.index("#if LEMON_V2_DEMO_MODE"):setup.index("nvsInit();")]
        self.assertIn("displaySetup();", demo_branch)
        self.assertIn("displaySetupVSync();", demo_branch)
        self.assertIn("touchSetup();", demo_branch)
        self.assertIn("return;", demo_branch)

    def test_loop_ticks_demo_and_dispatches_touch_before_normal_runtime(self):
        main = (SRC / "main.cpp").read_text(encoding="utf-8")
        loop = main[main.index("void loop()"):]
        self.assertIn("#if LEMON_V2_DEMO_MODE", loop)
        demo_branch = loop[loop.index("#if LEMON_V2_DEMO_MODE"):loop.index("esp_task_wdt_reset();")]
        self.assertIn("v2DemoTick(millis());", demo_branch)
        self.assertIn("v2DemoHandleTouch", demo_branch)
        self.assertIn("return;", demo_branch)


class V2DemoCanaryFlashContractTests(unittest.TestCase):
    def test_canary_script_requires_explicit_port_and_confirmation(self):
        path = ROOT / "tools" / "flash_v2_demo_canary.ps1"
        self.assertTrue(path.exists(), "safe canary flash script is missing")
        source = path.read_text(encoding="utf-8")

        self.assertIn("[Parameter(Mandatory = $true)]", source)
        self.assertIn("[string]$Port", source)
        self.assertIn("[switch]$ConfirmCanary", source)
        self.assertIn("0x303A", source)
        self.assertIn("ConfirmCanary", source)

    def test_canary_script_uses_normal_keep_mode_and_never_erases(self):
        path = ROOT / "tools" / "flash_v2_demo_canary.ps1"
        self.assertTrue(path.exists(), "safe canary flash script is missing")
        source = path.read_text(encoding="utf-8")

        self.assertIn("matouch_esp32s3_40_v2_demo", source)
        self.assertIn('"--flash-mode", "keep"', source)
        self.assertIn('"--flash-size", "keep"', source)
        self.assertNotIn("erase-flash", source)
        self.assertNotIn("erase_flash", source)

    def test_canary_script_rejects_an_unknown_port_before_esptool(self):
        script = ROOT / "tools" / "flash_v2_demo_canary.ps1"
        result = subprocess.run(
            [
                "powershell",
                "-NoProfile",
                "-File",
                str(script),
                "-Port",
                "COM999",
                "-ConfirmCanary",
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        output = result.stdout + result.stderr
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no es una Lemon Box ESP32-S3", output)
        self.assertNotIn("SyntaxError", output)
        self.assertNotIn("write-flash", output)

if __name__ == "__main__":
    unittest.main()
