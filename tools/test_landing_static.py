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
    def test_customer_proposal_is_integrated_with_readable_attachments(self):
        with open(os.path.join(LANDING, "index.html"), "r", encoding="utf-8") as f:
            html = f.read()

        for text in [
            "Propuesta para ampliar Lemon Box",
            "Opciones de fabricación",
            "Control de flota",
            "Developer Kit",
            "100 unidades",
            "150 unidades",
            "200 unidades",
            "USDC 4.500",
            "USDC 120 por unidad",
            "34,2% menos",
            "37,5% menos",
            "41,7% menos",
            "Programa de firmware · 60 días",
            "60 días corridos",
            "firmware Stocks",
            "No incluye cables ni fuentes de alimentación.",
            "Recall incluido",
            "70% al confirmar la producción y 30% contra entrega del lote completo",
            "SDK",
            "Skill",
            "20 días hábiles",
            "90 días de soporte inicial",
        ]:
            self.assertIn(text, html)

        self.assertNotIn("Una plataforma física para Lemon", html)
        self.assertNotIn("Tres escalas. Un precio claro por unidad.", html)
        self.assertIn("Control de flota + Developer Kit", html)
        self.assertIn("Skill de desarrollo", html)
        self.assertNotIn("USDC 3.500", html)
        self.assertNotIn("USDC 7.000", html)
        self.assertNotIn("Plataforma Partners", html)
        self.assertNotIn("MCP", html)
        self.assertNotIn("20% con el lote ensamblado", html)
        self.assertIn('id="proposal"', html)
        self.assertIn('id="documents"', html)

        required_documents = [
            "docs/propuesta-comercial.html",
            "docs/propuesta-comercial.txt",
            "docs/propuesta-comercial.pdf",
            "docs/alcance-plataforma.html",
            "docs/alcance-plataforma.txt",
            "docs/alcance-plataforma.pdf",
        ]
        for name in required_documents:
            self.assertTrue(os.path.exists(os.path.join(LANDING, *name.split("/"))), name)
            self.assertIn(f'./{name}', html)

        for name in ["docs/propuesta-comercial.html", "docs/propuesta-comercial.txt", "docs/alcance-plataforma.html", "docs/alcance-plataforma.txt"]:
            with open(os.path.join(LANDING, *name.split("/")), "r", encoding="utf-8") as f:
                document_copy = f.read()
            self.assertIn("Control de flota + Developer Kit", document_copy)
            self.assertIn("60 d", document_copy)
            self.assertIn("Stocks", document_copy)
            self.assertNotIn("Plataforma Partners", document_copy)
            self.assertNotIn("MCP", document_copy)

        for name in ["docs/propuesta-comercial.html", "docs/propuesta-comercial.txt"]:
            with open(os.path.join(LANDING, *name.split("/")), "r", encoding="utf-8") as f:
                commercial_copy = f.read()
            self.assertIn("No incluye cables ni fuentes", commercial_copy)
            self.assertIn("Recall incluido", commercial_copy)

        with open(os.path.join(LANDING, "proposal.css"), "r", encoding="utf-8") as f:
            proposal_css = f.read()
        self.assertIn(".document-list h3 { margin: .6rem 0 .4rem; color: var(--starlight);", proposal_css)
        self.assertIn(".implementation-list h3 { margin: 2.5rem 0 .6rem; color: var(--starlight);", proposal_css)
        self.assertIn(".proposal-table tbody td::before { content: attr(data-label);", proposal_css)
        self.assertIn(".fleet-app aside { display: none; }", proposal_css)

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
        self.assertIn("<title>Lemon Box</title>", html)
        self.assertNotIn("<h1>Lemon Box</h1>", html)
        self.assertIn("Scroll para explorar", html)
        self.assertIn("heroScrollCta?.addEventListener", html)
        self.assertIn("heroScrollCta?.classList.toggle('is-hidden', raw > 0);", html)
        self.assertIn(".hero-scroll-cta.is-hidden", html)
        self.assertIn(".nav-links a::after", html)
        self.assertIn(".nav-links a:hover::after", html)
        self.assertNotIn("~30 cajas físicas", html)
        self.assertNotIn("V2 validada en hardware", html)
        self.assertNotIn("Lemon Market Desk · V2", html)
        self.assertNotIn("Bitcoin, dólar y mercado en tiempo real. Siempre a la vista.", html)
        self.assertNotIn("El mercado de Lemon, en tu escritorio.", html)
        self.assertNotIn("Siempre presente. Relevante cuando importa.", html)
        self.assertNotIn("02 · La idea", html)
        self.assertNotIn("Dirección visual · datos ilustrativos", html)
        self.assertNotIn("Tres momentos. Una Lemon Box.", html)
        self.assertNotIn("USDT CONTROL ROOM", html)
        self.assertNotIn("tether-control-room", html)
        self.assertNotIn("ambient-home-product", html)
        self.assertNotIn("market-tape-product", html)
        self.assertNotIn("news-takeover-product", html)
        self.assertNotIn('<div class="hero-actions"', html)
        self.assertNotIn("Ver demo", html)
        self.assertNotIn('src="./studio/?mode=embed', html)
        self.assertNotIn("07 · Ver el producto", html)
        self.assertNotIn('id="explorer"', html)
        self.assertNotIn('id="idea"', html)
        self.assertNotIn('id="product"', html)
        self.assertNotIn('id="concepts"', html)
        self.assertNotIn('id="origin"', html)
        self.assertNotIn('id="status"', html)
        self.assertNotIn('id="roadmap"', html)
        self.assertNotIn('id="evidence"', html)
        self.assertNotIn('aria-label="Archivo visual de Lemon Box V1"', html)
        self.assertNotIn('id="executive-summary"', html)
        self.assertIn('id="features" hidden aria-hidden="true"', html)
        self.assertIn("if (!legacyFeatures || legacyFeatures.hidden) return;", html)
        self.assertIn('href="https://ferced.com"', html)
        self.assertIn("by ferced", html)
        self.assertNotIn("#00F068", html)
        self.assertNotIn("document.title = s", html)

        with open(os.path.join(LANDING, "proposal.css"), "r", encoding="utf-8") as f:
            proposal_css = f.read()
        self.assertIn("min-height: calc(100dvh + 1px);", proposal_css)
        self.assertIn("display: flex;", proposal_css)
        self.assertIn("align-items: center;", proposal_css)

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
        self.assertIn("ctx.globalAlpha = matrixVisible;", html)
        self.assertIn("drawLogoOn(logoVisible);", html)
        self.assertNotIn("productScreenImg", html)
        self.assertNotIn("home-screen-demo-480.png", html)
        self.assertIn("height: 380vh;", html)
        self.assertIn(".hero-scroll { height: 340svh; }", html)
        self.assertIn("phaseProgress(t, 0.82, 1.0)", html)
        self.assertIn("progressBar.setAttribute('aria-valuenow', String(pct))", html)
        self.assertIn("heroVisibilityObserver.observe(heroSection)", html)
        self.assertNotIn("conceptStageObserver", html)
        self.assertIn("classList.toggle('visible', e.isIntersecting)", html)
        self.assertNotIn("revealObserver.unobserve(e.target)", html)
        self.assertIn("hero-nav-ready", html)
        self.assertIn("hero-product-ready", html)
        self.assertIn("hero-scroll-ready", html)
        self.assertIn("const productDelay = prefersReducedMotion ? 0 : 360", html)
        self.assertIn("@media (prefers-reduced-motion: reduce)", html)
        self.assertIn("--hero-exit-opacity", html)
        self.assertIn("border: 1px solid transparent;", html)
        self.assertIn("nav.scrolled {\n            border-color:", html)
        self.assertIn("nav:not(.scrolled) .nav-preview {\n            opacity: 0;", html)
        self.assertIn("nav.scrolled .nav-preview {\n            opacity: 1;", html)
        self.assertIn("const heroHasReleased = heroBounds.bottom <= window.innerHeight;", html)
        self.assertIn("nav.classList.toggle('scrolled', heroHasReleased);", html)
        self.assertIn("const footerForNav = document.querySelector('footer');", html)
        self.assertIn("const footerIsVisible = footerBounds.top < window.innerHeight", html)
        self.assertIn("nav.classList.toggle('at-footer', footerIsVisible);", html)
        self.assertIn("window.addEventListener('resize', updateNav);", html)
        self.assertIn("nav.at-footer", html)
        self.assertNotIn("window.scrollY > 60", html)

    def test_mobile_hero_gives_the_physical_box_a_closer_camera(self):
        with open(os.path.join(LANDING, "index.html"), "r", encoding="utf-8") as f:
            html = f.read()

        self.assertIn("CAM_PATH.startRadius = narrow ? 175 : 225", html)
        self.assertIn("CAM_PATH.endRadius   = narrow ? 92 : 110", html)
        self.assertIn("CAM_PATH.startTargetY = narrow ? -14 : -18", html)
        self.assertIn("const verticalTargetT = easeInOutCubic(phaseProgress(t, 0.52, 0.78));", html)
        self.assertIn("const screenTargetT = easeInOutCubic(phaseProgress(t, 0.80, 1.0));", html)
        self.assertIn("const targetY = CAM_PATH.startTargetY * (1 - verticalTargetT);", html)
        self.assertIn("heroCamera.lookAt(0, targetY, screenWorldZ * screenTargetT);", html)
        self.assertIn("width: 114vw !important", html)
        self.assertIn("height: 44dvh", html)
        self.assertNotIn("#concepts", html)

    def test_landing_links_have_real_internal_destinations(self):
        with open(os.path.join(LANDING, "index.html"), "r", encoding="utf-8") as f:
            html = f.read()
        parser = LinkCollector()
        parser.feed(html)
        self.assertNotIn("#", parser.hrefs)
        for href in parser.hrefs:
            if href.startswith("#"):
                self.assertIn(href[1:], parser.ids, href)
        self.assertNotIn("./studio/?mode=concept", parser.hrefs)

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
