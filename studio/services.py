import hashlib
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

try:
    from tools import lemon_flasher as flasher
except ImportError:
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
    import lemon_flasher as flasher

from .sources import DataSourceRegistry
from .tool_registry import StudioToolRegistry


ENV_NAME = "matouch_esp32s3_40"


class StudioServices:
    def __init__(self, repo_root=None):
        self.repo_root = Path(repo_root or Path(__file__).resolve().parents[1])
        self.build_dir = self.repo_root / ".pio" / "build" / ENV_NAME
        self.artifact_dir = self.repo_root / "studio" / "artifacts"

    def status(self):
        return {
            "appVersion": self.app_version(),
            "ports": self.scan_ports(),
            "localBuild": self.local_build(),
            "recoveryAssets": self.recovery_assets(),
        }

    def app_version(self):
        config = self.repo_root / "src" / "config.h"
        if not config.exists():
            return None
        text = config.read_text(encoding="utf-8", errors="ignore")
        match = re.search(r'#define\s+APP_VERSION\s+"([^"]+)"', text)
        return match.group(1) if match else None

    def scan_ports(self):
        ports, descs = flasher.detect_esp_ports()
        out = []
        for idx, device in enumerate(ports):
            desc = descs[idx] if idx < len(descs) else device
            out.append({
                "device": device,
                "description": desc,
                "isEsp32": "ESP32" in desc.upper(),
            })
        return out

    def latest_release(self):
        version, asset_url, md5 = flasher.fetch_latest_release()
        return {"version": version, "assetUrl": asset_url, "md5": md5}

    def recovery_assets(self):
        bins = flasher._find_recovery_bins()
        if not bins:
            return {"ready": False, "files": {}}
        return {
            "ready": True,
            "files": {name: str(Path(path)) for name, path in bins.items()},
        }

    def local_build(self):
        fw = self.build_dir / "firmware.bin"
        if not fw.exists():
            return {"ready": False, "firmwarePath": str(fw)}
        return {
            "ready": True,
            "firmwarePath": str(fw),
            "size": fw.stat().st_size,
            "md5": _md5_file(fw),
            "updatedAt": fw.stat().st_mtime,
        }

    def download_latest_firmware(self, ctx=None):
        _log(ctx, "Checking latest GitHub release")
        release = self.latest_release()
        if not release["assetUrl"]:
            raise RuntimeError("latest release has no firmware asset")
        version = release["version"] or "latest"
        target_dir = self.artifact_dir / "releases"
        target_dir.mkdir(parents=True, exist_ok=True)
        target = target_dir / f"firmware-v{version}.bin"

        def on_progress(value):
            _progress(ctx, value * 45)

        _log(ctx, f"Downloading firmware v{version}")
        data = flasher.download_firmware(release["assetUrl"], on_progress)
        target.write_bytes(data)
        md5 = hashlib.md5(data).hexdigest()
        if release["md5"] and md5.lower() != release["md5"].lower():
            raise RuntimeError("downloaded firmware MD5 does not match release")
        _progress(ctx, 50)
        return {
            "version": version,
            "firmwarePath": str(target),
            "size": len(data),
            "md5": md5,
        }

    def build_firmware(self, ctx=None):
        cmd = [sys.executable, "-m", "platformio", "run", "-e", ENV_NAME]
        _log(ctx, "Running PlatformIO build")
        proc = subprocess.Popen(
            cmd,
            cwd=str(self.repo_root),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        lines = 0
        assert proc.stdout is not None
        for line in proc.stdout:
            lines += 1
            _log(ctx, line.rstrip())
            _progress(ctx, min(95, 10 + lines))
            if ctx and ctx.cancelled:
                proc.terminate()
                raise RuntimeError("build cancelled")
        code = proc.wait()
        if code != 0:
            raise RuntimeError(f"PlatformIO build failed with exit code {code}")
        return self.local_build()

    def flash_firmware(self, payload, ctx=None):
        port = str(payload.get("port") or "").strip()
        if not port:
            raise ValueError("port is required")
        if not payload.get("confirmed"):
            raise ValueError("firmware actions require explicit confirmation")

        mode = str(payload.get("mode") or "normal").lower()
        if mode not in {"normal", "recovery"}:
            raise ValueError("mode must be normal or recovery")
        if mode == "recovery" and not payload.get("recoveryConfirmed"):
            raise ValueError("recovery flash requires explicit recovery confirmation")

        firmware = self._resolve_firmware(payload, ctx)
        _log(ctx, f"Flashing {mode} on {port}")
        _progress(ctx, 55)
        flasher.flash_firmware(
            port,
            firmware.read_bytes(),
            log_cb=lambda line: _log(ctx, line),
            recovery=(mode == "recovery"),
        )
        _progress(ctx, 95)
        return {
            "ok": True,
            "mode": mode,
            "port": port,
            "firmwarePath": str(firmware),
            "visualConfirmationRequired": True,
            "recoveryRecommendedAfterInterruptedFlash": True,
        }

    def verify_boot(self, payload=None, ctx=None):
        seconds = int((payload or {}).get("seconds") or 12)
        samples = []
        end = time.time() + max(1, seconds)
        previous = None
        while time.time() < end:
            ports = tuple(p["device"] for p in self.scan_ports())
            if ports != previous:
                samples.append({"at": time.time(), "ports": list(ports)})
                _log(ctx, f"ports: {', '.join(ports) if ports else 'NO_PORTS'}")
                previous = ports
            _progress(ctx, int(100 - max(0, end - time.time()) / seconds * 100))
            if ctx and ctx.cancelled:
                raise RuntimeError("verification cancelled")
            time.sleep(0.25)
        stable = len(samples) <= 1
        return {
            "stable": stable,
            "samples": samples,
            "visualConfirmationRequired": True,
            "message": "Stable USB is necessary but screen confirmation is still required.",
        }

    def create_deploy_draft(self, payload, ctx=None):
        prompt = str(payload.get("prompt") or "").strip()
        if not prompt:
            raise ValueError("prompt is required")
        target = str(payload.get("target") or "firmware").lower()
        if target not in {"firmware", "weblive", "static"}:
            raise ValueError("target must be firmware, weblive, or static")

        manifest = payload.get("manifest") or {}
        key_names = []
        for item in payload.get("keys") or []:
            if isinstance(item, dict) and item.get("name"):
                key_names.append(str(item["name"]))

        self.artifact_dir.mkdir(parents=True, exist_ok=True)
        stamp = time.strftime("%Y%m%d-%H%M%S")
        path = self.artifact_dir / f"deploy-draft-{stamp}.json"
        draft = {
            "kind": "lemon-box-deploy-draft",
            "target": target,
            "prompt": prompt,
            "manifest": manifest,
            "keyNames": key_names,
            "steps": [
                "Review generated screen cards",
                "Map required keys to firmware or WebLive runtime",
                "Build firmware or WebLive bundle",
                "Flash or deploy through Control Room",
            ],
        }
        path.write_text(json.dumps(draft, indent=2) + "\n", encoding="utf-8")
        _log(ctx, f"Deploy draft written: {path}")
        return {"path": str(path), "target": target, "cardCount": len(manifest.get("cards") or [])}

    def capabilities(self):
        return StudioToolRegistry(repo_root=self.repo_root).capabilities()

    def list_sources(self, manifest=None):
        return DataSourceRegistry().list_sources(manifest)

    def preview_source(self, payload):
        return DataSourceRegistry().preview_source((payload or {}).get("source") or payload or {})

    def create_agent_draft(self, payload, ctx=None):
        result = StudioToolRegistry(repo_root=self.repo_root).call_tool("save_draft", payload or {})
        _log(ctx, f"Agent draft written: {result['path']}")
        return result

    def _resolve_firmware(self, payload, ctx=None):
        source = str(payload.get("firmwareSource") or "local").lower()
        if source == "latest":
            return Path(self.download_latest_firmware(ctx)["firmwarePath"])
        if source == "path":
            path = Path(str(payload.get("firmwarePath") or ""))
        else:
            path = Path(self.local_build()["firmwarePath"])
        if not path.exists():
            raise ValueError(f"firmware not found: {path}")
        return path


def _md5_file(path):
    h = hashlib.md5()
    with Path(path).open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def _log(ctx, message):
    if ctx:
        ctx.log(message)


def _progress(ctx, value):
    if ctx:
        ctx.progress(value)
