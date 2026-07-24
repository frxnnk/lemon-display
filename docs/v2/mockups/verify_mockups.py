from pathlib import Path
import re

from PIL import Image


ROOT = Path(__file__).resolve().parent
RENDERED = ROOT / "rendered"


def verify_png(name: str, background: tuple[int, int, int]) -> None:
    path = RENDERED / name
    with Image.open(path) as image:
        assert image.size == (480, 480), f"{name}: tamaño {image.size}"
        pixel = image.convert("RGB").getpixel((0, 0))
        assert pixel == background, f"{name}: fondo {pixel}"


def verify_source_rules() -> None:
    css = (ROOT / "styles.css").read_text(encoding="utf-8")
    html = (ROOT / "index.html").read_text(encoding="utf-8")

    assert re.search(r"\.safe\s*\{[^}]*inset:\s*48px", css, re.S)
    assert re.search(r"\.logo\s*\{[^}]*width:\s*120px", css, re.S)
    assert "PPNeueMachina-PlainBold.ttf" in css
    assert "Satoshi-Regular.otf" in css
    assert "Satoshi-Bold.ttf" in css
    assert html.count("data-scene=") == 3
    assert "DATOS DE DEMO" in html
    assert html.count(">DEMO<") == 2


def main() -> None:
    verify_png("home-480x480.png", (18, 18, 18))
    verify_png("tape-480x480.png", (18, 18, 18))
    verify_png("news-480x480.png", (207, 255, 46))
    verify_source_rules()
    print("OK: 3 mockups de 480x480, fondos y reglas fuente verificados")


if __name__ == "__main__":
    main()
