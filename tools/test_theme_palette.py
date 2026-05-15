import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


class ThemePaletteTests(unittest.TestCase):
    def test_colors_exposes_runtime_theme_api_and_light_palette(self):
        header = (SRC / "colors.h").read_text(encoding="utf-8")
        source_path = SRC / "colors.cpp"

        self.assertIn("enum UiTheme", header)
        self.assertIn("THEME_DARK", header)
        self.assertIn("THEME_LIGHT", header)
        self.assertIn("void setTheme(UiTheme theme);", header)
        self.assertIn("UiTheme getTheme();", header)
        self.assertTrue(source_path.exists(), "runtime theme implementation is missing")

        source = source_path.read_text(encoding="utf-8")
        self.assertIn("lightPalette", source)
        self.assertRegex(source, r"lightPalette\s*=\s*\{[^}]*0xF[0-9A-Fa-f]{3}")
        self.assertIn("BG_BASE = p.bgBase;", source)
        self.assertIn("TEXT_PRIMARY = p.textPrimary;", source)

    def test_theme_is_persisted_in_nvs(self):
        header = (SRC / "nvs_storage.h").read_text(encoding="utf-8")
        source = (SRC / "nvs_storage.cpp").read_text(encoding="utf-8")

        self.assertIn("uint8_t  nvsGetTheme();", header)
        self.assertIn("void     nvsSetTheme(uint8_t theme);", header)
        self.assertIn('"ui_theme"', source)
        self.assertIn("nvsGetTheme", source)
        self.assertIn("nvsSetTheme", source)

    def test_settings_screen_exposes_theme_toggle(self):
        settings = (SRC / "ui_settings.cpp").read_text(encoding="utf-8")

        self.assertIn("Tema", settings)
        self.assertIn("Claro", settings)
        self.assertIn("Oscuro", settings)
        self.assertIn("nvsSetTheme", settings)
        self.assertIn("Colors::setTheme", settings)

    def test_boot_applies_saved_theme_before_display_setup(self):
        main = (SRC / "main.cpp").read_text(encoding="utf-8")
        nvs_pos = main.index("nvsInit();")
        theme_pos = main.index("Colors::setTheme", nvs_pos)
        display_pos = main.index("displaySetup();", nvs_pos)

        self.assertLess(nvs_pos, theme_pos)
        self.assertLess(theme_pos, display_pos)


if __name__ == "__main__":
    unittest.main()
