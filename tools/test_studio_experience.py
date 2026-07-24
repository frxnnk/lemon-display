import os
import sys
import unittest


sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))


class StudioExperienceTest(unittest.TestCase):
    def test_extends_legacy_manifest_into_experience_package(self):
        from studio.manifest import normalize_manifest

        manifest = normalize_manifest({
            "name": "Desk build",
            "theme": "light",
            "layout": "btc_focus",
            "watchlist": "spy,mstr",
            "cards": [{
                "title": "Weather",
                "kind": "weather",
                "source": "local-weather",
                "x": 420,
                "y": 430,
                "w": 180,
                "h": 140,
            }],
            "sources": [{
                "id": "local-weather",
                "kind": "weather.current",
                "title": "Casa",
                "keyNames": ["weather_api_key", "WEATHER_API_KEY"],
                "config": {"city": "Buenos Aires", "apiKey": "must-not-survive"},
            }],
        })

        self.assertEqual(manifest["experienceVersion"], 1)
        self.assertEqual(manifest["basePreset"], "lemon-default")
        self.assertEqual(manifest["cards"][0]["kind"], "weather")
        self.assertLessEqual(manifest["cards"][0]["x"] + manifest["cards"][0]["w"], 480)
        self.assertLessEqual(manifest["cards"][0]["y"] + manifest["cards"][0]["h"], 480)
        self.assertEqual(manifest["requiredKeys"], ["WEATHER_API_KEY"])
        self.assertEqual(manifest["sources"][0]["keyNames"], ["WEATHER_API_KEY"])
        self.assertNotIn("apiKey", manifest["sources"][0]["config"])
        self.assertEqual(manifest["deviceSettings"]["theme"], "light")
        self.assertEqual(manifest["deviceSettings"]["layout"], "btc_focus")
        self.assertEqual(manifest["deviceSettings"]["watchlist"], ["SPY", "MSTR"])

    def test_marks_freeform_cards_as_requires_firmware_runtime(self):
        from studio.manifest import normalize_manifest

        manifest = normalize_manifest({
            "cards": [{
                "title": "Prompt App",
                "kind": "custom",
                "source": "my-api",
                "prompt": "Invent a card runtime",
            }]
        })

        self.assertFalse(manifest["compatibility"]["firmwareSupported"])
        self.assertEqual(manifest["compatibility"]["mode"], "requires-firmware-runtime")
        self.assertEqual(manifest["compatibility"]["unsupportedCards"], ["prompt-app"])

    def test_device_settings_are_clamped_and_z2_mode_normalized(self):
        from studio.manifest import normalize_manifest

        manifest = normalize_manifest({
            "deviceSettings": {
                "brightness": 999,
                "z2Mode": "stocks",
                "watchlist": ["aapl", "aapl", "nvda"],
            }
        })

        self.assertEqual(manifest["deviceSettings"]["brightness"], 255)
        self.assertEqual(manifest["deviceSettings"]["z2Mode"], "stocks")
        self.assertEqual(manifest["deviceSettings"]["watchlist"], ["AAPL", "NVDA"])


if __name__ == "__main__":
    unittest.main()
