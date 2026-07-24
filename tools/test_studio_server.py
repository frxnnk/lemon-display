import json
import os
import sys
import tempfile
import unittest


sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))


class FakeServices:
    def status(self):
        return {"appVersion": "5.1.2", "ports": []}

    def latest_release(self):
        return {"version": "5.1.3", "assetUrl": "https://example.test/fw.bin", "md5": None}

    def scan_ports(self):
        return [{"device": "COM7", "description": "USB JTAG [ESP32]", "isEsp32": True}]

    def flash_firmware(self, payload, ctx=None):
        ctx.log("flash called")
        return {"ok": True, "mode": payload["mode"]}

    def create_deploy_draft(self, payload, ctx=None):
        ctx.log("draft created")
        return {"path": "draft.json", "target": payload["target"]}

    def create_agent_draft(self, payload, ctx=None):
        ctx.log("agent draft created")
        return {"path": "agent-draft.json", "kind": "lemon-box-agent-draft"}


class StudioServerTest(unittest.TestCase):
    def test_get_status_returns_services_payload(self):
        from studio.server import StudioApp

        app = StudioApp(services=FakeServices())
        code, payload = app.handle("GET", "/api/status")

        self.assertEqual(code, 200)
        self.assertEqual(payload["appVersion"], "5.1.2")

    def test_manifest_roundtrip_via_api(self):
        from studio.server import StudioApp

        with tempfile.TemporaryDirectory() as tmp:
            manifest_path = os.path.join(tmp, "manifest.json")
            app = StudioApp(services=FakeServices(), manifest_path=manifest_path)

            code, saved = app.handle("POST", "/api/manifest", {
                "theme": "light",
                "layout": "btc_focus",
                "watchlist": "spy,mstr",
            })
            code2, loaded = app.handle("GET", "/api/manifest")

        self.assertEqual(code, 200)
        self.assertEqual(code2, 200)
        self.assertEqual(saved, loaded)
        self.assertEqual(loaded["watchlist"], ["SPY", "MSTR"])

    def test_flash_job_endpoint_runs_registered_handler(self):
        from studio.server import StudioApp

        app = StudioApp(services=FakeServices(), run_async=False)
        code, job = app.handle("POST", "/api/jobs/flash", {
            "port": "COM7",
            "mode": "normal",
            "confirmed": True,
        })

        self.assertEqual(code, 202)
        self.assertEqual(job["state"], "complete")
        self.assertEqual(job["result"], {"ok": True, "mode": "normal"})
        self.assertEqual(job["logs"], ["flash called"])

    def test_unknown_api_route_returns_404(self):
        from studio.server import StudioApp

        app = StudioApp(services=FakeServices())
        code, payload = app.handle("GET", "/api/nope")

        self.assertEqual(code, 404)
        self.assertEqual(payload["error"], "not found")

    def test_key_routes_mask_saved_values(self):
        from studio.server import StudioApp

        with tempfile.TemporaryDirectory() as tmp:
            app = StudioApp(
                services=FakeServices(),
                key_path=os.path.join(tmp, "keys.json"),
                run_async=False,
            )
            code, saved = app.handle("POST", "/api/keys", {
                "name": "openai_api_key",
                "value": "sk-test-123456",
            })
            code2, listed = app.handle("GET", "/api/keys")

        self.assertEqual(code, 200)
        self.assertEqual(code2, 200)
        self.assertEqual(saved["name"], "OPENAI_API_KEY")
        self.assertNotIn("value", saved)
        self.assertNotIn("sk-test-123456", repr(listed))

    def test_deploy_draft_job_endpoint(self):
        from studio.server import StudioApp

        app = StudioApp(services=FakeServices(), run_async=False)
        code, job = app.handle("POST", "/api/jobs/deploy-draft", {
            "prompt": "Make it show weather",
            "target": "weblive",
        })

        self.assertEqual(code, 202)
        self.assertEqual(job["state"], "complete")
        self.assertEqual(job["result"]["target"], "weblive")

    def test_creator_platform_api_contracts(self):
        from studio.server import StudioApp

        with tempfile.TemporaryDirectory() as tmp:
            app = StudioApp(
                services=FakeServices(),
                manifest_path=os.path.join(tmp, "manifest.json"),
                key_path=os.path.join(tmp, "keys.json"),
                run_async=False,
                data_dir=tmp,
            )

            code, capabilities = app.handle("GET", "/api/capabilities")
            code2, sources = app.handle("GET", "/api/sources")
            code3, preview = app.handle("POST", "/api/sources/preview", {
                "source": {
                    "id": "btc",
                    "kind": "market.crypto",
                    "config": {"symbol": "BTC"},
                }
            })
            code4, experiences = app.handle("GET", "/api/experiences")

        self.assertEqual(code, 200)
        self.assertEqual(code2, 200)
        self.assertEqual(code3, 200)
        self.assertEqual(code4, 200)
        self.assertIn("deviceSettings", capabilities["firmware"])
        self.assertIn("tools", capabilities["mcp"])
        self.assertIn("market.crypto", {item["kind"] for item in sources["catalog"]})
        self.assertIn("ok", preview)
        self.assertEqual(preview["kind"], "market.crypto")
        self.assertGreaterEqual(len(experiences["experiences"]), 1)

    def test_experience_routes_save_and_activate(self):
        from studio.server import StudioApp

        with tempfile.TemporaryDirectory() as tmp:
            manifest_path = os.path.join(tmp, "manifest.json")
            app = StudioApp(
                services=FakeServices(),
                manifest_path=manifest_path,
                run_async=False,
                data_dir=tmp,
            )

            code, saved = app.handle("PUT", "/api/experiences/weather-desk", {
                "name": "Weather desk",
                "cards": [{"title": "Weather", "kind": "weather"}],
            })
            code2, fetched = app.handle("GET", "/api/experiences/weather-desk")
            code3, activated = app.handle("POST", "/api/experiences/weather-desk/activate")
            code4, active = app.handle("GET", "/api/manifest")

        self.assertEqual(code, 200)
        self.assertEqual(code2, 200)
        self.assertEqual(code3, 200)
        self.assertEqual(saved["id"], "weather-desk")
        self.assertEqual(fetched["experience"]["name"], "Weather desk")
        self.assertEqual(activated["active"]["id"], "weather-desk")
        self.assertEqual(active["name"], "Weather desk")

    def test_agent_draft_job_and_device_routes(self):
        from studio.server import StudioApp

        with tempfile.TemporaryDirectory() as tmp:
            app = StudioApp(
                services=FakeServices(),
                run_async=False,
                data_dir=tmp,
                key_path=os.path.join(tmp, "keys.json"),
            )

            code, job = app.handle("POST", "/api/jobs/agent-draft", {
                "prompt": "Make a market screen",
                "experience": {},
            })
            code2, connected = app.handle("POST", "/api/device/connect", {"host": "192.168.4.10"})
            code3, settings = app.handle("GET", "/api/device/settings")

        self.assertEqual(code, 202)
        self.assertEqual(job["state"], "complete")
        self.assertEqual(job["result"]["kind"], "lemon-box-agent-draft")
        self.assertEqual(code2, 502)
        self.assertIn("not reachable", connected["error"])
        self.assertEqual(code3, 502)
        self.assertIn("not connected", settings["error"])

    def test_wireless_device_routes_and_sync_job_exist(self):
        from studio.server import StudioApp

        with tempfile.TemporaryDirectory() as tmp:
            app = StudioApp(
                services=FakeServices(),
                run_async=False,
                data_dir=tmp,
                key_path=os.path.join(tmp, "keys.json"),
            )

            code, devices = app.handle("GET", "/api/devices")
            code2, scan = app.handle("POST", "/api/devices/scan", {"subnet": "manual"})
            code3, pair = app.handle("POST", "/api/device/pair", {"host": "192.168.1.44"})
            code4, health = app.handle("GET", "/api/device/health")
            code5, job = app.handle("POST", "/api/jobs/sync-settings", {
                "theme": "light",
                "layout": "btc_usd",
                "brightness": 180,
                "watchlist": ["AAPL", "MSTR"],
            })

        self.assertEqual(code, 200)
        self.assertEqual(devices["devices"], [])
        self.assertEqual(code2, 200)
        self.assertIn("devices", scan)
        self.assertEqual(code3, 502)
        self.assertIn("not reachable", pair["error"])
        self.assertEqual(code4, 502)
        self.assertIn("not connected", health["error"])
        self.assertEqual(code5, 202)
        self.assertEqual(job["state"], "failed")


if __name__ == "__main__":
    unittest.main()
