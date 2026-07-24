import os
import sys
import tempfile
import unittest


sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))


class StudioManifestTest(unittest.TestCase):
    def test_normalizes_watchlist_and_defaults(self):
        from studio.manifest import normalize_manifest

        manifest = normalize_manifest({
            "name": "Desk build",
            "watchlist": "aapl, tsla\nnvda, AAPL",
        })

        self.assertEqual(manifest["name"], "Desk build")
        self.assertEqual(manifest["theme"], "dark")
        self.assertEqual(manifest["layout"], "btc_usd")
        self.assertEqual(manifest["watchlist"], ["AAPL", "TSLA", "NVDA"])

    def test_save_and_load_manifest_roundtrip(self):
        from studio.manifest import load_manifest, save_manifest

        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "manifest.json")

            saved = save_manifest({
                "name": "Counter build",
                "theme": "light",
                "layout": "btc_focus",
                "watchlist": ["spy", "mstr"],
                "releaseNotes": "first pass",
            }, path)
            loaded = load_manifest(path)

        self.assertEqual(saved, loaded)
        self.assertEqual(loaded["watchlist"], ["SPY", "MSTR"])
        self.assertEqual(loaded["releaseNotes"], "first pass")

    def test_invalid_theme_is_rejected(self):
        from studio.manifest import normalize_manifest

        with self.assertRaises(ValueError):
            normalize_manifest({"theme": "purple"})

    def test_normalizes_cards_for_workbench(self):
        from studio.manifest import normalize_manifest

        manifest = normalize_manifest({
            "cards": [
                {
                    "title": "Prompt",
                    "kind": "custom",
                    "source": "https://api.test",
                    "x": -20,
                    "y": 16,
                    "w": 900,
                    "h": 140,
                    "prompt": "Show weather",
                    "style": {"accent": "solar"},
                },
                {"title": "BTC", "kind": "btc", "x": 22, "y": 72, "w": 260, "h": 180},
            ]
        })

        self.assertEqual(manifest["cards"][0]["id"], "prompt")
        self.assertEqual(manifest["cards"][0]["kind"], "custom")
        self.assertEqual(manifest["cards"][0]["x"], 0)
        self.assertEqual(manifest["cards"][0]["w"], 480)
        self.assertEqual(manifest["cards"][0]["prompt"], "Show weather")
        self.assertEqual(manifest["cards"][0]["style"], {"accent": "solar"})
        self.assertEqual(manifest["cards"][1]["id"], "btc")
        self.assertEqual(manifest["cards"][1]["source"], "")

    def test_migrates_legacy_windows_to_cards(self):
        from studio.manifest import normalize_manifest

        manifest = normalize_manifest({
            "windows": [
                {"title": "Prompt", "type": "custom", "zone": "z2", "source": "https://api.test"},
                {"title": "BTC", "type": "btc", "zone": "z1"},
            ]
        })

        self.assertNotIn("windows", manifest)
        self.assertEqual(manifest["cards"][0]["id"], "prompt")
        self.assertEqual(manifest["cards"][0]["kind"], "custom")
        self.assertEqual(manifest["cards"][0]["x"], 24)
        self.assertEqual(manifest["cards"][0]["y"], 276)
        self.assertEqual(manifest["cards"][1]["kind"], "btc")

    def test_rejects_invalid_cards_without_breaking_defaults(self):
        from studio.manifest import normalize_manifest

        manifest = normalize_manifest({
            "cards": [
                "bad",
                {"title": "", "kind": "", "w": 0, "h": 0},
            ]
        })

        self.assertGreaterEqual(len(manifest["cards"]), 2)
        self.assertEqual(manifest["cards"][0]["kind"], "btc")

    def test_builds_declarative_device_bundle_for_supported_cards(self):
        from studio.manifest import normalize_manifest

        manifest = normalize_manifest({
            "runtimeSchemaVersion": 1,
            "cards": [
                {"title": "BTC", "kind": "btc", "source": "btc-usd"},
                {"title": "Note", "kind": "text", "prompt": "Hold steady"},
                {"title": "Spark", "kind": "sparkline", "source": "watchlist"},
                {"title": "Metric", "kind": "metric", "source": "weather-temp"},
            ],
        })

        self.assertEqual(manifest["runtimeSchemaVersion"], 1)
        self.assertTrue(manifest["compatibility"]["firmwareSupported"])
        self.assertEqual(manifest["compatibility"]["mode"], "device-supported")
        self.assertEqual(len(manifest["deviceBundle"]["cards"]), 4)
        self.assertNotIn("prompt", manifest["deviceBundle"]["cards"][0])
        self.assertEqual(manifest["syncStatus"], "local-draft")

    def test_custom_cards_require_firmware_runtime_not_device_apply(self):
        from studio.manifest import normalize_manifest

        manifest = normalize_manifest({
            "cards": [{"title": "Prompt app", "kind": "custom", "prompt": "Make a game"}],
        })

        self.assertFalse(manifest["compatibility"]["firmwareSupported"])
        self.assertEqual(manifest["compatibility"]["mode"], "requires-firmware-runtime")
        self.assertEqual(manifest["compatibility"]["unsupportedCards"], ["prompt-app"])
        self.assertEqual(manifest["deviceBundle"]["cards"], [])


if __name__ == "__main__":
    unittest.main()
