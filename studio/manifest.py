import json
from pathlib import Path


THEMES = {"dark", "light"}
LAYOUTS = {"btc_focus", "btc_usd"}
CARD_KINDS = {"btc", "dollar", "stocks", "watchlist", "chart", "metric", "text", "list", "sparkline", "weather", "custom"}
SOURCE_KINDS = {"market.crypto", "market.stocks", "weather.current", "custom.http_json", "watchlist"}
SUPPORTED_CARD_KINDS = {"btc", "dollar", "stocks", "watchlist", "chart", "metric", "text", "list", "sparkline"}
Z2_MODES = {"usd", "markets", "stocks"}
SCREEN_SIZE = 480
MIN_CARD_SIZE = 48

DEFAULT_CARDS = [
    {
        "id": "btc-hero",
        "title": "BTC Hero",
        "kind": "btc",
        "source": "market:btc-usd",
        "x": 24,
        "y": 58,
        "w": 432,
        "h": 214,
        "prompt": "",
        "style": {"accent": "solar"},
    },
    {
        "id": "lemon-dollar",
        "title": "Lemon Dollar",
        "kind": "dollar",
        "source": "market:usdc-ars",
        "x": 24,
        "y": 286,
        "w": 210,
        "h": 144,
        "prompt": "",
        "style": {"accent": "greent"},
    },
    {
        "id": "watchlist",
        "title": "Watchlist",
        "kind": "stocks",
        "source": "watchlist",
        "x": 246,
        "y": 286,
        "w": 210,
        "h": 144,
        "prompt": "",
        "style": {"accent": "nebula"},
    },
]

DEFAULT_MANIFEST = {
    "experienceVersion": 1,
    "runtimeSchemaVersion": 1,
    "basePreset": "lemon-default",
    "name": "Lemon Box Studio",
    "theme": "dark",
    "layout": "btc_usd",
    "watchlist": ["AAPL", "TSLA", "NVDA", "SPY", "MSTR"],
    "cards": DEFAULT_CARDS,
    "sources": [
        {
            "id": "btc-usd",
            "title": "BTC / USD",
            "kind": "market.crypto",
            "config": {"symbol": "BTC", "currency": "USD"},
            "keyNames": [],
        },
        {
            "id": "watchlist",
            "title": "Watchlist",
            "kind": "market.stocks",
            "config": {"symbols": ["AAPL", "TSLA", "NVDA"]},
            "keyNames": [],
        },
    ],
    "requiredKeys": [],
    "deviceSettings": {
        "theme": "dark",
        "layout": "btc_usd",
        "brightness": 255,
        "z2Mode": "usd",
        "watchlist": ["AAPL", "TSLA", "NVDA", "SPY", "MSTR"],
    },
    "compatibility": {
        "firmwareSupported": True,
        "mode": "device-supported",
        "deviceSettingsOnly": False,
        "unsupportedCards": [],
    },
    "deviceBundle": {"schemaVersion": 1, "cards": []},
    "syncStatus": "local-draft",
    "lastAppliedDeviceId": "",
    "mode": "control_room",
    "releaseNotes": "",
}


def default_manifest_path(repo_root=None):
    base = Path(repo_root or Path(__file__).resolve().parents[1])
    return base / "studio" / "manifest.json"


def _normalize_watchlist(value):
    if value is None:
        items = DEFAULT_MANIFEST["watchlist"]
    elif isinstance(value, str):
        items = value.replace("\n", ",").replace(" ", ",").split(",")
    elif isinstance(value, list):
        items = value
    else:
        raise ValueError("watchlist must be a list or comma-separated string")

    seen = set()
    out = []
    for raw in items:
        sym = str(raw).strip().upper()
        if not sym or sym in seen:
            continue
        if len(sym) > 18:
            raise ValueError(f"watchlist symbol too long: {sym}")
        seen.add(sym)
        out.append(sym)
        if len(out) >= 12:
            break
    if not out:
        raise ValueError("watchlist cannot be empty")
    return out


def _slug(value, fallback):
    text = str(value or "").strip().lower()
    out = []
    last_dash = False
    for ch in text:
        if ch.isalnum():
            out.append(ch)
            last_dash = False
        elif not last_dash:
            out.append("-")
            last_dash = True
    slug = "".join(out).strip("-")
    return slug or fallback


def _clamp_int(value, fallback, low=0, high=SCREEN_SIZE):
    try:
        parsed = int(value)
    except (TypeError, ValueError):
        parsed = fallback
    return max(low, min(high, parsed))


def _normalize_card(item, idx):
    if not isinstance(item, dict):
        return None
    if "title" in item and not str(item.get("title") or "").strip():
        return None
    title = str(item.get("title") or f"Card {idx + 1}").strip()
    if not title:
        return None
    kind = str(item.get("kind") or item.get("type") or "custom").lower()
    if kind not in CARD_KINDS:
        kind = "custom"

    x = _clamp_int(item.get("x"), 24)
    y = _clamp_int(item.get("y"), 64)
    w = _clamp_int(item.get("w"), 180, MIN_CARD_SIZE)
    h = _clamp_int(item.get("h"), 120, MIN_CARD_SIZE)
    if x + w > SCREEN_SIZE:
        w = SCREEN_SIZE - x
    if y + h > SCREEN_SIZE:
        h = SCREEN_SIZE - y
    if w < MIN_CARD_SIZE or h < MIN_CARD_SIZE:
        return None

    style = item.get("style") if isinstance(item.get("style"), dict) else {}
    return {
        "id": _slug(item.get("id") or title, f"card-{idx + 1}"),
        "title": title,
        "kind": kind,
        "source": str(item.get("source") or ""),
        "x": x,
        "y": y,
        "w": w,
        "h": h,
        "prompt": str(item.get("prompt") or ""),
        "style": dict(style),
    }


def _normalize_key_names(value):
    items = value if isinstance(value, list) else []
    out = []
    seen = set()
    for raw in items:
        text = str(raw or "").strip().upper().replace("-", "_").replace(" ", "_")
        name = "".join(ch for ch in text if ch.isalnum() or ch == "_")
        if not name or name in seen:
            continue
        seen.add(name)
        out.append(name)
    return out


def _source_kind(value):
    kind = str(value or "").strip().lower()
    aliases = {
        "crypto": "market.crypto",
        "stocks": "market.stocks",
        "weather": "weather.current",
        "custom": "custom.http_json",
        "http": "custom.http_json",
    }
    kind = aliases.get(kind, kind)
    return kind if kind in SOURCE_KINDS else "custom.http_json"


def _safe_source_config(value):
    if not isinstance(value, dict):
        return {}
    blocked = ("key", "token", "secret", "password", "bearer", "auth")
    out = {}
    for key, item in value.items():
        name = str(key)
        if any(part in name.lower() for part in blocked):
            continue
        if isinstance(item, (str, int, float, bool)) or item is None:
            out[name] = item
        elif isinstance(item, list):
            out[name] = [str(entry) for entry in item[:20]]
    return out


def _normalize_source(item, idx):
    if not isinstance(item, dict):
        return None
    title = str(item.get("title") or item.get("name") or f"Source {idx + 1}").strip()
    source_id = _slug(item.get("id") or title, f"source-{idx + 1}")
    kind = _source_kind(item.get("kind") or item.get("type"))
    return {
        "id": source_id,
        "title": title,
        "kind": kind,
        "config": _safe_source_config(item.get("config")),
        "keyNames": _normalize_key_names(item.get("keyNames") or item.get("keys")),
    }


def _normalize_sources(value):
    raw_sources = value if isinstance(value, list) else DEFAULT_MANIFEST["sources"]
    sources = []
    seen = set()
    for idx, item in enumerate(raw_sources[:16]):
        source = _normalize_source(item, idx)
        if not source:
            continue
        if source["id"] in seen:
            source["id"] = f"{source['id']}-{idx + 1}"
        seen.add(source["id"])
        sources.append(source)
    if sources:
        return sources
    return [dict(source, config=dict(source["config"]), keyNames=list(source["keyNames"]))
            for source in DEFAULT_MANIFEST["sources"]]


def _normalize_z2_mode(value):
    if isinstance(value, int):
        return {0: "usd", 1: "markets", 2: "stocks"}.get(value, "usd")
    text = str(value or "usd").strip().lower()
    aliases = {"dollar": "usd", "market": "markets", "stock": "stocks"}
    text = aliases.get(text, text)
    return text if text in Z2_MODES else "usd"


def _normalize_device_settings(value, theme, layout, watchlist):
    data = value if isinstance(value, dict) else {}
    brightness = _clamp_int(data.get("brightness"), DEFAULT_MANIFEST["deviceSettings"]["brightness"], 1, 255)
    device_watchlist = _normalize_watchlist(data.get("watchlist") if "watchlist" in data else watchlist)
    return {
        "theme": theme,
        "layout": layout,
        "brightness": brightness,
        "z2Mode": _normalize_z2_mode(data.get("z2Mode")),
        "watchlist": device_watchlist,
    }


def _compatibility(cards):
    unsupported = [card["id"] for card in cards if card["kind"] not in SUPPORTED_CARD_KINDS]
    supported = not unsupported
    return {
        "firmwareSupported": supported,
        "mode": "device-supported" if supported else "requires-firmware-runtime",
        "deviceSettingsOnly": not supported,
        "unsupportedCards": unsupported,
    }


def _device_bundle(cards, schema_version):
    bundle_cards = []
    for card in cards:
        if card["kind"] not in SUPPORTED_CARD_KINDS:
            continue
        bundle_cards.append({
            "id": card["id"],
            "title": card["title"],
            "kind": "watchlist" if card["kind"] == "stocks" else card["kind"],
            "source": card["source"],
            "x": card["x"],
            "y": card["y"],
            "w": card["w"],
            "h": card["h"],
            "style": dict(card.get("style") or {}),
        })
    return {
        "schemaVersion": schema_version,
        "screen": {"w": SCREEN_SIZE, "h": SCREEN_SIZE},
        "cards": bundle_cards,
    }


def _legacy_window_to_card(item, idx):
    zone = str(item.get("zone") or "z2").lower()
    zone_rects = {
        "z1": (24, 64, 432, 202),
        "hero": (24, 64, 432, 202),
        "z2": (24, 276, 432, 156),
        "top-left": (24, 64, 204, 112),
        "top-right": (252, 64, 204, 112),
        "bottom-left": (24, 286, 204, 132),
        "bottom-right": (252, 286, 204, 132),
        "fullscreen": (16, 48, 448, 400),
    }
    x, y, w, h = zone_rects.get(zone, zone_rects["z2"])
    return _normalize_card({
        "id": item.get("id"),
        "title": item.get("title"),
        "kind": item.get("kind") or item.get("type"),
        "source": item.get("source"),
        "x": x,
        "y": y,
        "w": w,
        "h": h,
        "prompt": item.get("prompt"),
    }, idx)


def _normalize_cards(cards_value, windows_value=None):
    raw_cards = cards_value if isinstance(cards_value, list) else None
    if raw_cards is None and isinstance(windows_value, list):
        raw_cards = [_legacy_window_to_card(item, idx) for idx, item in enumerate(windows_value)]
    if raw_cards is None:
        raw_cards = DEFAULT_CARDS

    cards = []
    seen = set()
    for idx, item in enumerate(raw_cards[:10]):
        card = item if isinstance(item, dict) and "kind" in item else item
        normalized = _normalize_card(card, idx) if isinstance(card, dict) else None
        if not normalized:
            continue
        card_id = normalized["id"]
        if card_id in seen:
            normalized["id"] = f"{card_id}-{idx + 1}"
        seen.add(normalized["id"])
        cards.append(normalized)
    if not cards:
        return [dict(card, style=dict(card.get("style") or {})) for card in DEFAULT_CARDS]
    return cards


def normalize_manifest(data=None):
    data = data or {}
    merged = dict(DEFAULT_MANIFEST)
    merged.update(data)

    theme = str(merged.get("theme", "dark")).lower()
    if theme not in THEMES:
        raise ValueError(f"unsupported theme: {theme}")

    layout = str(merged.get("layout", "btc_usd")).lower()
    if layout not in LAYOUTS:
        raise ValueError(f"unsupported layout: {layout}")

    cards_source = data.get("cards") if isinstance(data, dict) and "cards" in data else None
    windows_source = data.get("windows") if isinstance(data, dict) and "windows" in data else None
    device_settings_source = data.get("deviceSettings") if isinstance(data, dict) and "deviceSettings" in data else {}

    watchlist = _normalize_watchlist(merged.get("watchlist"))
    cards = _normalize_cards(cards_source, windows_source)
    sources = _normalize_sources(merged.get("sources"))
    required_keys = _normalize_key_names(merged.get("requiredKeys"))
    for source in sources:
        for key_name in source["keyNames"]:
            if key_name not in required_keys:
                required_keys.append(key_name)

    runtime_schema = int(merged.get("runtimeSchemaVersion") or 1)
    compatibility = _compatibility(cards)
    return {
        "experienceVersion": int(merged.get("experienceVersion") or 1),
        "runtimeSchemaVersion": runtime_schema,
        "basePreset": str(merged.get("basePreset") or DEFAULT_MANIFEST["basePreset"]).strip(),
        "name": str(merged.get("name") or DEFAULT_MANIFEST["name"]).strip(),
        "theme": theme,
        "layout": layout,
        "watchlist": watchlist,
        "cards": cards,
        "sources": sources,
        "requiredKeys": required_keys,
        "deviceSettings": _normalize_device_settings(device_settings_source, theme, layout, watchlist),
        "compatibility": compatibility,
        "deviceBundle": _device_bundle(cards if compatibility["firmwareSupported"] else [], runtime_schema),
        "syncStatus": str(merged.get("syncStatus") or DEFAULT_MANIFEST["syncStatus"]),
        "lastAppliedDeviceId": str(merged.get("lastAppliedDeviceId") or ""),
        "mode": str(merged.get("mode") or DEFAULT_MANIFEST["mode"]).strip(),
        "releaseNotes": str(merged.get("releaseNotes") or ""),
    }


def load_manifest(path=None):
    target = Path(path) if path else default_manifest_path()
    if not target.exists():
        return normalize_manifest()
    with target.open("r", encoding="utf-8") as f:
        return normalize_manifest(json.load(f))


def save_manifest(data, path=None):
    target = Path(path) if path else default_manifest_path()
    manifest = normalize_manifest(data)
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open("w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    return manifest
