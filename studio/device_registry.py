import json
from pathlib import Path


class DeviceRegistry:
    def __init__(self, path):
        self.path = Path(path)

    def list(self):
        devices = self._read().get("devices", [])
        return [_public_device(device) for device in devices if isinstance(device, dict)]

    def current(self):
        data = self._read()
        current_id = data.get("currentDeviceId")
        for device in data.get("devices", []):
            if device.get("id") == current_id:
                return _public_device(device)
        devices = data.get("devices", [])
        return _public_device(devices[0]) if devices else None

    def upsert(self, device):
        clean = _public_device(device)
        clean["host"] = str(device.get("host") or "").strip()
        if device.get("baseUrl"):
            clean["baseUrl"] = device["baseUrl"]
        elif clean["host"]:
            clean["baseUrl"] = f"http://{clean['host']}"
        if device.get("tokenRef"):
            clean["tokenRef"] = str(device["tokenRef"])
        data = self._read()
        existing = next((item for item in data.get("devices", []) if item.get("id") == clean["id"]), None)
        if existing and existing.get("tokenRef") and not clean.get("tokenRef"):
            clean["tokenRef"] = existing["tokenRef"]
        devices = [item for item in data.get("devices", []) if item.get("id") != clean["id"]]
        devices.insert(0, clean)
        data["devices"] = devices
        data["currentDeviceId"] = clean["id"]
        self._write(data)
        return _public_device(clean)

    def _read(self):
        if not self.path.exists():
            return {"devices": [], "currentDeviceId": None}
        with self.path.open("r", encoding="utf-8") as f:
            data = json.load(f)
        if not isinstance(data, dict):
            return {"devices": [], "currentDeviceId": None}
        data.setdefault("devices", [])
        return data

    def _write(self, data):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with self.path.open("w", encoding="utf-8") as f:
            json.dump(data, f, indent=2)
            f.write("\n")


def token_ref_for_device(device_id):
    text = str(device_id or "device").strip().upper().replace("-", "_").replace(" ", "_")
    text = "".join(ch for ch in text if ch.isalnum() or ch == "_")
    return f"DEVICE_TOKEN_{text or 'DEVICE'}"


def _public_device(device):
    clean = {
        "id": str(device.get("id") or device.get("host") or "lemon-box"),
        "name": str(device.get("name") or "Lemon Box"),
        "host": str(device.get("host") or ""),
        "baseUrl": str(device.get("baseUrl") or ""),
    }
    for key in ("version", "hardware", "ip", "mac", "tokenRef", "lastSeenAt"):
        if device.get(key) is not None:
            clean[key] = device[key]
    return clean
