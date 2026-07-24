import json
from pathlib import Path


class KeyVault:
    def __init__(self, path):
        self.path = Path(path)

    def list(self):
        return [self._public(name, value) for name, value in self._read().items()]

    def save(self, name, value):
        key_name = _normalize_name(name)
        key_value = str(value or "").strip()
        if not key_value:
            raise ValueError("key value cannot be empty")
        data = self._read()
        data[key_name] = key_value
        self._write(data)
        return self._public(key_name, key_value)

    def delete(self, name):
        key_name = _normalize_name(name)
        data = self._read()
        existed = key_name in data
        data.pop(key_name, None)
        self._write(data)
        return existed

    def _read(self):
        if not self.path.exists():
            return {}
        with self.path.open("r", encoding="utf-8") as f:
            payload = json.load(f)
        return payload if isinstance(payload, dict) else {}

    def _write(self, data):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        with self.path.open("w", encoding="utf-8") as f:
            json.dump(data, f, indent=2)
            f.write("\n")

    def _public(self, name, value):
        return {
            "name": name,
            "masked": mask_secret(value),
            "length": len(str(value)),
        }


def mask_secret(value):
    text = str(value or "")
    if len(text) <= 4:
        return "*" * len(text)
    if len(text) <= 10:
        return text[:2] + "*" * (len(text) - 4) + text[-2:]
    return text[:2] + "*" * 8 + text[-2:]


def _normalize_name(name):
    text = str(name or "").strip().upper().replace("-", "_").replace(" ", "_")
    text = "".join(ch for ch in text if ch.isalnum() or ch == "_")
    if not text:
        raise ValueError("key name is required")
    return text
