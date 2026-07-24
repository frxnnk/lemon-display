import json
from pathlib import Path

from .manifest import load_manifest, normalize_manifest, save_manifest


class ExperienceStore:
    def __init__(self, root_dir=None, manifest_path=None):
        base = Path(root_dir or Path(__file__).resolve().parents[1] / "studio")
        self.root_dir = base / "experiences"
        self.manifest_path = manifest_path

    def list(self):
        self.root_dir.mkdir(parents=True, exist_ok=True)
        items = [self._summary("current", load_manifest(self.manifest_path), active=True)]
        for path in sorted(self.root_dir.glob("*.json")):
            if path.stem == "current":
                continue
            try:
                items.append(self._summary(path.stem, self._read(path), active=False))
            except Exception:
                continue
        return items

    def get(self, experience_id):
        if experience_id == "current":
            return {"id": "current", "experience": load_manifest(self.manifest_path), "active": True}
        path = self._path(experience_id)
        if not path.exists():
            raise KeyError("experience not found")
        return {"id": path.stem, "experience": self._read(path), "active": False}

    def save(self, experience_id, data):
        target_id = _slug(experience_id)
        experience = normalize_manifest(data)
        path = self._path(target_id)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(experience, indent=2) + "\n", encoding="utf-8")
        return {"id": target_id, "experience": experience, "active": False}

    def activate(self, experience_id):
        saved = self.get(experience_id)
        active = save_manifest(saved["experience"], self.manifest_path)
        return {"active": {"id": saved["id"], "experience": active}}

    def _read(self, path):
        with path.open("r", encoding="utf-8") as f:
            return normalize_manifest(json.load(f))

    def _path(self, experience_id):
        return self.root_dir / f"{_slug(experience_id)}.json"

    def _summary(self, experience_id, experience, active):
        return {
            "id": experience_id,
            "name": experience.get("name") or experience_id,
            "basePreset": experience.get("basePreset"),
            "cardCount": len(experience.get("cards") or []),
            "sourceCount": len(experience.get("sources") or []),
            "firmwareSupported": bool((experience.get("compatibility") or {}).get("firmwareSupported")),
            "active": active,
        }


def _slug(value):
    text = str(value or "experience").strip().lower()
    out = []
    last_dash = False
    for ch in text:
        if ch.isalnum():
            out.append(ch)
            last_dash = False
        elif not last_dash:
            out.append("-")
            last_dash = True
    return "".join(out).strip("-") or "experience"
