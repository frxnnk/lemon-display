import argparse
import json
import mimetypes
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

from .device import DeviceClient
from .device_registry import DeviceRegistry
from .experiences import ExperienceStore
from .jobs import JobRunner
from .keys import KeyVault
from .manifest import load_manifest, save_manifest
from .services import StudioServices
from .sources import DataSourceRegistry
from .tool_registry import StudioToolRegistry


class StudioApp:
    def __init__(self, services=None, runner=None, manifest_path=None, key_path=None, run_async=True, data_dir=None):
        self.services = services or StudioServices()
        self.runner = runner or JobRunner(run_async=run_async)
        self.manifest_path = manifest_path
        repo_root = Path(__file__).resolve().parents[1]
        data_root = Path(data_dir) if data_dir else repo_root / "studio"
        self.key_vault = KeyVault(key_path or (repo_root / "studio" / "secrets.local.json"))
        self.experience_store = ExperienceStore(data_root, manifest_path)
        self.device_registry = DeviceRegistry(data_root / "devices.local.json")
        self.device_client = DeviceClient(registry=self.device_registry, key_vault=self.key_vault)
        self.tool_registry = StudioToolRegistry(repo_root=repo_root, manifest_path=manifest_path)
        self.static_dir = Path(__file__).resolve().parent / "static"
        self.asset_dir = Path(__file__).resolve().parents[1]
        self._register_jobs()

    def handle(self, method, path, body=None):
        parsed = urlparse(path)
        route = parsed.path.rstrip("/") or "/"
        body = body or {}

        try:
            if method == "GET" and route == "/api/status":
                return 200, self.services.status()
            if method == "GET" and route == "/api/capabilities":
                return 200, self.tool_registry.capabilities()
            if method == "GET" and route == "/api/sources":
                return 200, self._source_registry().list_sources(load_manifest(self.manifest_path))
            if method == "POST" and route == "/api/sources/preview":
                return 200, self._source_registry().preview_source(body.get("source") or body)
            if method == "GET" and route == "/api/ports":
                return 200, {"ports": self.services.scan_ports()}
            if method == "GET" and route == "/api/release":
                return 200, self.services.latest_release()
            if method == "GET" and route == "/api/manifest":
                return 200, load_manifest(self.manifest_path)
            if method == "POST" and route == "/api/manifest":
                return 200, save_manifest(body, self.manifest_path)
            if method == "GET" and route == "/api/keys":
                return 200, {"keys": self.key_vault.list()}
            if method == "POST" and route == "/api/keys":
                return 200, self.key_vault.save(body.get("name"), body.get("value"))
            if method == "POST" and route == "/api/keys/delete":
                return 200, {"ok": self.key_vault.delete(body.get("name"))}
            if method == "GET" and route == "/api/jobs":
                return 200, {"jobs": self.runner.list()}
            if method == "GET" and route == "/api/experiences":
                return 200, {"experiences": self.experience_store.list()}
            if method == "GET" and route == "/api/devices":
                return 200, {"devices": self.device_client.list()}
            if method == "POST" and route == "/api/devices/scan":
                return 200, self.device_client.scan(body)
            if method == "POST" and route == "/api/device/connect":
                return self._device_response(lambda: self.device_client.connect(body.get("host") or body.get("ip")))
            if method == "POST" and route == "/api/device/pair":
                return self._device_response(lambda: self.device_client.pair(body.get("host") or body.get("ip"), body.get("code")))
            if method == "GET" and route == "/api/device/health":
                return self._device_response(lambda: self.device_client.health())
            if route == "/api/device/settings":
                return self._device_settings_response(method, body)

            if route.startswith("/api/experiences/"):
                parts = route.split("/")
                if len(parts) == 4 and method == "GET":
                    return 200, self.experience_store.get(parts[3])
                if len(parts) == 4 and method == "PUT":
                    return 200, self.experience_store.save(parts[3], body)
                if len(parts) == 5 and parts[4] == "activate" and method == "POST":
                    return 200, self.experience_store.activate(parts[3])

            if route.startswith("/api/jobs/"):
                parts = route.split("/")
                if method == "POST" and len(parts) == 4:
                    job = self.runner.submit(parts[3], payload=body)
                    return 202, job
                if method == "GET" and len(parts) == 4:
                    job = self.runner.get(parts[3])
                    return (200, job) if job else (404, {"error": "job not found"})
                if method == "POST" and len(parts) == 5 and parts[4] == "cancel":
                    ok = self.runner.cancel(parts[3])
                    return (200, {"ok": True}) if ok else (404, {"error": "job not cancellable"})

            return 404, {"error": "not found"}
        except Exception as exc:
            return 400, {"error": str(exc)}

    def _register_jobs(self):
        self.runner.register("detect", lambda payload, ctx: self.services.scan_ports())
        self.runner.register("release", lambda payload, ctx: self.services.latest_release())
        self.runner.register("download-release", lambda payload, ctx: self.services.download_latest_firmware(ctx))
        self.runner.register("build", lambda payload, ctx: self.services.build_firmware(ctx))
        self.runner.register("flash", lambda payload, ctx: self.services.flash_firmware(payload, ctx))
        self.runner.register("verify-boot", lambda payload, ctx: self.services.verify_boot(payload, ctx))
        self.runner.register("deploy-draft", lambda payload, ctx: self.services.create_deploy_draft(payload, ctx))
        self.runner.register("agent-draft", lambda payload, ctx: self.services.create_agent_draft(payload, ctx))
        self.runner.register("sync-settings", self._sync_settings_job)
        self.runner.register("ota-upload", self._ota_upload_job)
        self.runner.register("ota-latest", self._ota_latest_job)
        self.runner.register("ota-verify", self._ota_verify_job)

    def _source_registry(self):
        names = [item["name"] for item in self.key_vault.list()]
        return DataSourceRegistry(available_keys=names)

    def _device_settings_response(self, method, body):
        return self._device_response(lambda: self.device_client.settings() if method == "GET" else self.device_client.apply_settings(body)) if method in {"GET", "POST"} else (405, {"error": "method not allowed"})

    def _device_response(self, fn):
        try:
            return 200, fn()
        except RuntimeError as exc:
            return 502, {"error": str(exc)}

    def _sync_settings_job(self, payload, ctx):
        ctx.log("sync settings over WiFi")
        ctx.progress(15)
        result = self.device_client.apply_settings(payload)
        ctx.progress(100)
        return result

    def _ota_upload_job(self, payload, ctx):
        if not payload.get("confirmed"):
            raise ValueError("wireless OTA requires explicit confirmation")
        firmware = payload.get("firmwarePath")
        if not firmware:
            firmware = self.services.local_build()["firmwarePath"]
        ctx.log("wireless OTA upload armed")
        ctx.progress(20)
        result = self.device_client.ota_upload(firmware)
        if not result.get("ok"):
            raise RuntimeError(result.get("error") or "wireless OTA upload failed")
        ctx.progress(100)
        return result

    def _ota_latest_job(self, payload, ctx):
        if not payload.get("confirmed"):
            raise ValueError("wireless OTA requires explicit confirmation")
        ctx.log("wireless OTA latest release requested")
        ctx.progress(20)
        result = self.device_client.ota_latest(payload.get("repo"))
        if not result.get("ok"):
            raise RuntimeError(result.get("error") or "wireless OTA latest failed")
        ctx.progress(100)
        return result

    def _ota_verify_job(self, payload, ctx):
        seconds = int(payload.get("seconds") or 12)
        deadline = time.time() + max(1, seconds)
        last = None
        while time.time() < deadline:
            try:
                last = self.device_client.health()
                if last.get("ok"):
                    return {"ok": True, "health": last}
            except RuntimeError as exc:
                last = {"error": str(exc)}
            time.sleep(0.5)
        raise RuntimeError(f"device health not restored after OTA: {last}")

    def resolve_asset(self, path):
        name = path.lstrip("/")
        allowed = {
            "hero-tilted-screen-final.png",
            "hero-final-latest.png",
            "hero-square-desktop-fixed.png",
            "Lemon-box-with-screen.stl",
        }
        if name not in allowed:
            return None
        target = (self.asset_dir / name).resolve()
        if not str(target).startswith(str(self.asset_dir.resolve())) or not target.exists():
            return None
        return target


def make_handler(app):
    class StudioRequestHandler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path.startswith("/api/"):
                self._send_json(*app.handle("GET", self.path))
            elif self.path.startswith("/assets/"):
                self._send_asset(self.path)
            else:
                self._send_static(self.path)

        def do_POST(self):
            body = self._read_json()
            self._send_json(*app.handle("POST", self.path, body))

        def do_PUT(self):
            body = self._read_json()
            self._send_json(*app.handle("PUT", self.path, body))

        def log_message(self, fmt, *args):
            return

        def _read_json(self):
            length = int(self.headers.get("Content-Length") or 0)
            if length == 0:
                return {}
            raw = self.rfile.read(length).decode("utf-8")
            return json.loads(raw) if raw else {}

        def _send_json(self, code, payload):
            raw = json.dumps(payload).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)

        def _send_static(self, path):
            parsed = urlparse(path)
            name = "index.html" if parsed.path in {"", "/"} else parsed.path.lstrip("/")
            target = (app.static_dir / name).resolve()
            if not str(target).startswith(str(app.static_dir.resolve())) or not target.exists():
                self.send_error(404)
                return
            data = target.read_bytes()
            ctype = mimetypes.guess_type(str(target))[0] or "application/octet-stream"
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def _send_asset(self, path):
            parsed = urlparse(path)
            target = app.resolve_asset(parsed.path[len("/assets/"):])
            if not target:
                self.send_error(404)
                return
            data = target.read_bytes()
            ctype = mimetypes.guess_type(str(target))[0] or "application/octet-stream"
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

    return StudioRequestHandler


def run(host="127.0.0.1", port=8765, open_browser=False):
    app = StudioApp()
    server = ThreadingHTTPServer((host, port), make_handler(app))
    url = f"http://{host}:{port}"
    print(f"Lemon Box Studio running at {url}")
    if open_browser:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--open", action="store_true")
    args = parser.parse_args()
    run(args.host, args.port, args.open)


if __name__ == "__main__":
    main()
