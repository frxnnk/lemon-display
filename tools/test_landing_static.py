import json
import os
import unittest
from html.parser import HTMLParser


ROOT = os.path.dirname(os.path.dirname(__file__))
LANDING = os.path.join(ROOT, "landing")


class LinkCollector(HTMLParser):
    def __init__(self):
        super().__init__()
        self.hrefs = []
        self.ids = set()

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if attrs.get("id"):
            self.ids.add(attrs["id"])
        if tag == "a" and attrs.get("href"):
            self.hrefs.append(attrs["href"])


class LandingStaticTest(unittest.TestCase):
    def test_landing_assets_and_studio_entry_exist(self):
        required = [
            "index.html",
            "studio/index.html",
            "studio/app.js",
            "studio/workbench3d.js",
            "studio/models/Lemon-box-with-screen.stl",
            "assets/lemon-iso.svg",
            "assets/lemonlogo.png",
            "modelo3d/Lemon-box-with-screen.stl",
            "modelo3d/Lemon-box-lid-new.stl",
            "modelo3d/Lemon-box-lid-logo-new.stl",
            "modelo3d/Lemon-box-feet.stl",
            "assets/v2/ambient-home.png",
            "assets/v2/market-tape.png",
            "assets/v2/news-takeover.png",
            "assets/v2/ambient-home-product.png",
            "assets/v2/ambient-home-product.webp",
            "assets/v2/market-tape-product.png",
            "assets/v2/market-tape-product.webp",
            "assets/v2/news-takeover-product.png",
            "assets/v2/news-takeover-product.webp",
            "assets/v2/home-screen-demo-480.png",
            "assets/v2/PPNeueMachina-PlainBold.ttf",
            "assets/v2/Satoshi-Bold.ttf",
            "assets/v2/Satoshi-Regular.otf",
            "assets/v2/lemon-horizontal-light.svg",
            "assets/v2/lemon-horizontal-black.svg",
            "assets/v1/previous-landing-hardware.png",
        ]
        for name in required:
            self.assertTrue(os.path.exists(os.path.join(LANDING, *name.split("/"))), name)

        with open(os.path.join(LANDING, "index.html"), "r", encoding="utf-8") as f:
            html = f.read()
        self.assertIn("Lemon Market Desk · Lemon en tu escritorio", html)
        self.assertNotIn("~30 cajas físicas", html)
        self.assertNotIn("V2 validada en hardware", html)
        self.assertNotIn("Lemon Market Desk · V2", html)
        self.assertIn("Bitcoin, dólar y mercado en tiempo real. Siempre a la vista.", html)
        self.assertIn("Dirección visual · datos ilustrativos", html)
        self.assertIn("Siempre presente. Relevante cuando importa.", html)
        self.assertIn("Tres momentos. Una Lemon Box.", html)
        self.assertIn("Ver demo", html)
        self.assertIn("./studio/?mode=concept", html)
        self.assertNotIn('src="./studio/?mode=embed', html)
        self.assertNotIn("07 · Ver el producto", html)
        self.assertNotIn('id="explorer"', html)
        self.assertIn('id="product"', html)
        self.assertIn('id="concepts"', html)
        self.assertNotIn('id="origin"', html)
        self.assertNotIn('id="status"', html)
        self.assertNotIn('id="roadmap"', html)
        self.assertNotIn('id="evidence"', html)
        self.assertNotIn('aria-label="Archivo visual de Lemon Box V1"', html)
        self.assertNotIn('id="executive-summary"', html)
        self.assertIn('id="features" hidden aria-hidden="true"', html)
        self.assertIn("if (!legacyFeatures || legacyFeatures.hidden) return;", html)
        self.assertNotIn("Powered by", html)
        self.assertNotIn("ferced.com", html)
        self.assertNotIn("#00F068", html)
        self.assertNotIn("document.title = s", html)

    def test_scroll_motion_keeps_the_assembly_choreography(self):
        with open(os.path.join(LANDING, "index.html"), "r", encoding="utf-8") as f:
            html = f.read()

        self.assertIn("nav:not(.scrolled)", html)
        self.assertIn("nav.scrolled", html)
        self.assertIn("function getScrollProgress()", html)
        self.assertIn("const HOLD_END = 0.52", html)
        self.assertIn("const exitOpacity = 1 - clamp((raw - 0.94) / 0.04, 0, 1)", html)
        self.assertIn("const logoVisible = Math.max(0, Math.min(1, (raw - 0.42) / 0.04))", html)
        self.assertIn("const matrixVisible = Math.max(0, Math.min(1, (raw - 0.44) / 0.04))", html)
        self.assertIn("const productVisible = Math.max(0, Math.min(1, (raw - 0.52) / 0.06))", html)
        self.assertIn("height: 380vh;", html)
        self.assertIn(".hero-scroll { height: 340svh; }", html)
        self.assertIn("phaseProgress(t, 0.82, 1.0)", html)
        self.assertIn("progressBar.setAttribute('aria-valuenow', String(pct))", html)
        self.assertIn("heroVisibilityObserver.observe(heroSection)", html)
        self.assertIn("conceptStageObserver", html)
        self.assertIn("is-expanded", html)
        self.assertIn("classList.toggle('visible', e.isIntersecting)", html)
        self.assertNotIn("revealObserver.unobserve(e.target)", html)
        self.assertIn("hero-intro-ready", html)
        self.assertIn("--hero-exit-opacity", html)
        self.assertIn("border: 1px solid transparent;", html)
        self.assertIn("nav.scrolled {\n            border-color:", html)
        self.assertIn("nav:not(.scrolled) .nav-preview {\n            opacity: 0;", html)
        self.assertIn("nav.scrolled .nav-preview {\n            opacity: 1;", html)

    def test_mobile_hero_gives_the_physical_box_a_closer_camera(self):
        with open(os.path.join(LANDING, "index.html"), "r", encoding="utf-8") as f:
            html = f.read()

        self.assertIn("CAM_PATH.startRadius = narrow ? 205 : 280", html)
        self.assertIn("CAM_PATH.endRadius   = narrow ? 105 : 130", html)
        self.assertIn("width: 108vw !important", html)
        self.assertIn("height: 44dvh", html)
        self.assertIn(
            "#concepts .concept-figure:nth-child(1),\n"
            "            #concepts .concept-figure:nth-child(2),\n"
            "            #concepts .concept-figure:nth-child(3)",
            html,
        )

    def test_landing_links_have_real_internal_destinations(self):
        with open(os.path.join(LANDING, "index.html"), "r", encoding="utf-8") as f:
            html = f.read()
        parser = LinkCollector()
        parser.feed(html)
        self.assertNotIn("#", parser.hrefs)
        for href in parser.hrefs:
            if href.startswith("#"):
                self.assertIn(href[1:], parser.ids, href)
        self.assertIn("./studio/?mode=concept", parser.hrefs)

    def test_studio_route_is_hosted_web_preview(self):
        with open(os.path.join(LANDING, "studio", "index.html"), "r", encoding="utf-8") as f:
            html = f.read()
        with open(os.path.join(LANDING, "studio", "app.js"), "r", encoding="utf-8") as f:
            app_js = f.read()
        self.assertIn("./app.js", html)
        self.assertIn("./styles.css", html)
        self.assertIn("./vendor/three/three.module.js", html)
        self.assertIn("STATIC_STUDIO", app_js)
        self.assertIn("Studio web preview", app_js)
        self.assertIn('STUDIO_MODE === "concept"', app_js)
        self.assertIn('STUDIO_MODE === "embed"', app_js)
        self.assertIn('initialView: EMBED_MODE || REVIEW_MODE ? "overview" : "front"', app_js)
        self.assertIn('location.pathname.includes("/studio/")', app_js)
        self.assertIn("drawMarketDeskScreen", app_js)
        self.assertIn("targetAddressSpace", app_js)
        self.assertIn("lemon-studio-web-device", app_js)
        self.assertNotIn("127.0.0.1:8765", html)
        self.assertIn(">Control<", html)
        self.assertIn("Demo de producto", html)
        self.assertIn("Market Desk V2", html)
        self.assertIn('data-demo-screen="home"', html)
        self.assertIn("No aplica cambios ni ejecuta OTA.", html)
        self.assertNotIn("Controlar la experiencia, no diseñar slides.", html)
        self.assertNotIn('class="view-controls"', html)
        self.assertNotIn("pair-device", html)
        self.assertNotIn(">Vincular<", html)
        self.assertNotIn("<span>Pantalla</span>", html)
        self.assertNotIn("Que cambia la cajita", html)
        self.assertNotIn("Si funciona", html)

        with open(os.path.join(LANDING, "studio", "styles.css"), "r", encoding="utf-8") as f:
            css = f.read()
        self.assertIn('url("./fonts/Satoshi-Regular.woff2")', css)
        self.assertIn('body[data-review-mode="true"] { overflow: hidden; background: var(--bg); }', css)
        self.assertIn('body[data-embed-mode="true"] { overflow: hidden; }', css)
        self.assertIn("height: 100dvh", css)

        with open(os.path.join(LANDING, "studio", "workbench3d.js"), "r", encoding="utf-8") as f:
            workbench_js = f.read()
        self.assertIn('overview: { normal: 285', workbench_js)

    def test_vercel_route_to_studio_redirect_page(self):
        with open(os.path.join(LANDING, "vercel.json"), "r", encoding="utf-8") as f:
            config = json.load(f)
        redirects = config.get("redirects") or []
        self.assertIn(
            {"source": "/studio", "destination": "/studio/?mode=concept", "permanent": False},
            redirects,
        )


if __name__ == "__main__":
    unittest.main()
