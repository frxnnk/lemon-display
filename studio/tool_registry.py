import json
import time
from pathlib import Path

from .manifest import load_manifest, normalize_manifest
from .sources import DataSourceRegistry


TOOL_NAMES = [
    "get_current_experience",
    "list_capabilities",
    "list_sources",
    "preview_source",
    "create_experience_draft",
    "validate_experience",
    "save_draft",
]


class StudioToolRegistry:
    def __init__(self, repo_root=None, manifest_path=None, source_registry=None):
        self.repo_root = Path(repo_root or Path(__file__).resolve().parents[1])
        self.manifest_path = manifest_path
        self.source_registry = source_registry or DataSourceRegistry()
        self.artifact_dir = self.repo_root / "studio" / "artifacts"

    def list_tools(self):
        return [
            _tool("get_current_experience", "Read the active Lemon Box experience package."),
            _tool("list_capabilities", "List safe local Studio capabilities and firmware support."),
            _tool("list_sources", "List source catalog and configured sources."),
            _tool("preview_source", "Preview one data source without exposing secret values."),
            _tool("create_experience_draft", "Create an in-memory draft from a prompt and experience."),
            _tool("validate_experience", "Validate an experience for firmware and preview compatibility."),
            _tool("save_draft", "Persist a draft artifact for human review."),
        ]

    def capabilities(self):
        return {
            "studio": {
                "mode": "local-first",
                "experienceVersion": 1,
                "surface": "Workbench Pro",
            },
            "firmware": {
                "deviceSettings": ["theme", "layout", "brightness", "z2Mode", "watchlist"],
                "brightnessControl": "hardware-mod-required",
                "cards": "preview-only except built-in BTC/USD/watchlist presets",
                "destructiveActions": "human-confirmed-control-room-only",
            },
            "mcp": {
                "transport": "stdio",
                "permissions": "draft-only",
                "tools": [tool["name"] for tool in self.list_tools()],
            },
            "sources": [item["kind"] for item in self.source_registry.list_catalog()],
        }

    def call_tool(self, name, args=None):
        if name not in TOOL_NAMES:
            raise ValueError(f"tool not allowed: {name}")
        args = args or {}
        if name == "get_current_experience":
            return load_manifest(self.manifest_path)
        if name == "list_capabilities":
            return self.capabilities()
        if name == "list_sources":
            return self.source_registry.list_sources(args.get("experience"))
        if name == "preview_source":
            return self._preview_source(args)
        if name == "validate_experience":
            return self.validate_experience(args.get("experience") or args)
        if name == "create_experience_draft":
            return self.create_draft(args, persist=False)
        if name == "save_draft":
            return self.create_draft(args, persist=True)
        raise ValueError(f"tool not implemented: {name}")

    def validate_experience(self, experience):
        manifest = normalize_manifest(experience or {})
        compatibility = manifest.get("compatibility") or {}
        warnings = []
        if not compatibility.get("firmwareSupported"):
            names = ", ".join(compatibility.get("unsupportedCards") or [])
            warnings.append(f"Preview-only cards are not firmware-supported yet: {names}")
        if manifest.get("requiredKeys"):
            warnings.append("Draft references key names only; values stay in the local key vault.")
        return {
            "ok": bool(compatibility.get("firmwareSupported")),
            "experience": manifest,
            "warnings": warnings,
            "applyToDeviceSupported": ["theme", "layout", "brightness", "z2Mode", "watchlist"],
            "hardwareNotes": {
                "brightness": "Stored and sent to firmware, but visible backlight changes require the MaTouch R28/R29 hardware mod."
            },
        }

    def create_draft(self, args, persist=False):
        prompt = str(args.get("prompt") or "").strip()
        experience = normalize_manifest(args.get("experience") or {})
        validation = self.validate_experience(experience)
        draft = {
            "kind": "lemon-box-agent-draft",
            "createdAt": int(time.time()),
            "prompt": prompt,
            "experience": validation["experience"],
            "capabilities": self.capabilities(),
            "sourceCatalog": self.source_registry.list_catalog(),
            "keyNames": validation["experience"].get("requiredKeys") or [],
            "warnings": validation["warnings"],
            "nextHumanSteps": [
                "Review the 480x480 preview",
                "Apply supported settings over local WiFi if desired",
                "Build or flash only through Control Room confirmations",
            ],
        }
        if not persist:
            return draft
        self.artifact_dir.mkdir(parents=True, exist_ok=True)
        stamp = time.strftime("%Y%m%d-%H%M%S")
        path = self.artifact_dir / f"agent-draft-{stamp}.json"
        path.write_text(json.dumps(draft, indent=2) + "\n", encoding="utf-8")
        return {
            "kind": draft["kind"],
            "path": str(path),
            "cardCount": len(draft["experience"].get("cards") or []),
            "warnings": draft["warnings"],
        }

    def _preview_source(self, args):
        source = args.get("source")
        if not source and args.get("sourceId"):
            current = normalize_manifest(args.get("experience") or load_manifest(self.manifest_path))
            source = next((item for item in current.get("sources", []) if item["id"] == args["sourceId"]), None)
        return self.source_registry.preview_source(source or {})


def _tool(name, description):
    return {
        "name": name,
        "description": description,
        "inputSchema": {
            "type": "object",
            "additionalProperties": True,
        },
    }
