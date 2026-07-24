import json
import time
import urllib.error
import urllib.request
from pathlib import Path

from .device_registry import DeviceRegistry, token_ref_for_device


class DeviceClient:
    def __init__(self, path=None, opener=None, registry=None, key_vault=None):
        repo_root = Path(__file__).resolve().parents[1]
        self.path = Path(path or repo_root / "studio" / "device.local.json")
        self.opener = opener or urllib.request.urlopen
        self.registry = registry or DeviceRegistry(self.path)
        self.key_vault = key_vault

    def connect(self, host):
        clean = str(host or "").strip().replace("http://", "").replace("https://", "").strip("/")
        if not clean:
            raise ValueError("device host is required")
        base_url = f"http://{clean}"
        identity = self._request("GET", f"{base_url}/api/device")
        device_id = identity.get("id") or clean
        profile = {
            "id": device_id,
            "name": identity.get("name") or "Lemon Box",
            "host": clean,
            "baseUrl": base_url,
            "version": identity.get("version"),
            "hardware": identity.get("hardware"),
            "mac": identity.get("mac"),
            "lastSeenAt": int(time.time()),
        }
        device = self.registry.upsert(profile)
        return {"device": device, "settingsUrl": f"{base_url}/api/settings"}

    def list(self):
        return self.registry.list()

    def scan(self, payload=None):
        return {"devices": self.registry.list(), "scan": "manual-ip-first"}

    def pair(self, host=None, code=None):
        if host:
            clean = str(host or "").strip().replace("http://", "").replace("https://", "").strip("/")
            base_url = f"http://{clean}"
        else:
            device = self._device()
            clean = device["host"]
            base_url = device["baseUrl"]

        if not code:
            started = self._request("POST", f"{base_url}/api/pair/start", {})
            return {
                "ok": bool(started.get("ok", True)),
                "host": clean,
                "pairingRequired": True,
                "code": started.get("code"),
            }

        confirmed = self._request("POST", f"{base_url}/api/pair/confirm", {"code": code})
        device_id = confirmed.get("id") or clean
        token = confirmed.get("token")
        token_ref = token_ref_for_device(device_id)
        if token and self.key_vault:
            self.key_vault.save(token_ref, token)
        profile = {
            "id": device_id,
            "name": confirmed.get("name") or "Lemon Box",
            "host": clean,
            "baseUrl": base_url,
            "version": confirmed.get("version"),
            "tokenRef": token_ref if token else None,
            "lastSeenAt": int(time.time()),
        }
        return {"ok": True, "device": self.registry.upsert(profile)}

    def health(self):
        state = self._device()
        return self._request("GET", f"{state['baseUrl']}/api/health", headers=self._auth_headers(state))

    def ota_status(self):
        state = self._device()
        return self._request("GET", f"{state['baseUrl']}/api/ota/status", headers=self._auth_headers(state))

    def ota_upload(self, firmware_path):
        state = self._device()
        path = Path(firmware_path)
        if not path.exists():
            raise ValueError(f"firmware not found: {path}")
        self._request("POST", f"{state['baseUrl']}/api/ota/arm", {}, headers=self._auth_headers(state), timeout=10)
        data = path.read_bytes()
        headers = self._auth_headers(state)
        headers["Content-Type"] = "application/octet-stream"
        return self._request("POST", f"{state['baseUrl']}/api/ota/upload", raw=data, headers=headers, timeout=90)

    def ota_latest(self, repo=None):
        state = self._device()
        payload = {"repo": repo} if repo else {}
        return self._request("POST", f"{state['baseUrl']}/api/ota/github", payload, headers=self._auth_headers(state), timeout=120)

    def settings(self):
        state = self._device()
        return self._request("GET", f"{state['baseUrl']}/api/settings", headers=self._auth_headers(state))

    def apply_settings(self, payload):
        state = self._device()
        return self._request("POST", f"{state['baseUrl']}/api/settings", payload, headers=self._auth_headers(state))

    def _device(self):
        device = self.registry.current()
        if not device:
            raise RuntimeError("device not connected")
        if not device.get("baseUrl"):
            raise RuntimeError("device not connected")
        return device

    def _auth_headers(self, device):
        token_ref = device.get("tokenRef")
        token = self._secret(token_ref)
        return {"Authorization": f"Bearer {token}"} if token else {}

    def _secret(self, name):
        if not name or not self.key_vault:
            return None
        data = self.key_vault._read()
        return data.get(name)

    def _request(self, method, url, payload=None, headers=None, raw=None, timeout=2):
        data = None
        headers = dict(headers or {})
        headers["Accept"] = "application/json"
        if raw is not None:
            data = raw
        elif payload is not None:
            data = json.dumps(payload).encode("utf-8")
            headers["Content-Type"] = "application/json"
        req = urllib.request.Request(url, data=data, headers=headers, method=method)
        try:
            with self.opener(req, timeout=timeout) as res:
                raw = res.read(1024 * 64).decode("utf-8")
        except (OSError, urllib.error.URLError) as exc:
            raise RuntimeError(f"device not reachable: {exc}") from exc
        return json.loads(raw) if raw else {"ok": True}
