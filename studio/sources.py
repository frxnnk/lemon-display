import json
import time
import urllib.request
from urllib.parse import quote, urlencode

from .manifest import _normalize_key_names, _normalize_source, normalize_manifest


CATALOG = [
    {
        "kind": "market.crypto",
        "title": "Crypto market",
        "description": "BTC and crypto price cards for local preview.",
        "firmwareSupported": True,
        "requiredKeys": [],
    },
    {
        "kind": "market.stocks",
        "title": "Stocks watchlist",
        "description": "Ticker rows compatible with the current watchlist runtime.",
        "firmwareSupported": True,
        "requiredKeys": [],
    },
    {
        "kind": "weather.current",
        "title": "Weather",
        "description": "Open-Meteo current weather previews without a key by default.",
        "firmwareSupported": False,
        "requiredKeys": [],
    },
    {
        "kind": "custom.http_json",
        "title": "Custom HTTP JSON",
        "description": "Local HTTP JSON source for drafts and WebLive experiments.",
        "firmwareSupported": False,
        "requiredKeys": [],
    },
]


class DataSourceRegistry:
    def __init__(self, available_keys=None, fetcher=None, ttl_seconds=30):
        self.available_keys = None if available_keys is None else set(_normalize_key_names(list(available_keys)))
        self.fetcher = fetcher or _fetch_json
        self.ttl_seconds = ttl_seconds
        self._cache = {}

    def list_catalog(self):
        return [dict(item, requiredKeys=list(item["requiredKeys"])) for item in CATALOG]

    def list_sources(self, manifest=None):
        normalized = normalize_manifest(manifest or {})
        return {
            "catalog": self.list_catalog(),
            "configured": [
                self._source_summary(source)
                for source in normalized.get("sources", [])
            ],
        }

    def preview_source(self, source, timeout=5):
        normalized = _normalize_source(source, 0)
        if not normalized:
            return _error("source", "custom.http_json", "invalid source")

        missing = self._missing_keys(normalized)
        if missing:
            return _preview(normalized, ok=False, missing=missing, error="missing required key")

        try:
            cache_key = json.dumps(normalized, sort_keys=True)
            cached = self._cached(cache_key)
            if cached is not None:
                return _preview(normalized, ok=True, sample=cached, cached=True)

            if normalized["kind"] == "market.crypto":
                sample = self._market_crypto(normalized, timeout)
            elif normalized["kind"] == "market.stocks":
                sample = self._market_stocks(normalized, timeout)
            elif normalized["kind"] == "weather.current":
                sample = self._weather(normalized, timeout)
            else:
                sample = self._custom_http(normalized, timeout)
            self._cache[cache_key] = {"at": time.time(), "sample": sample}
            return _preview(normalized, ok=True, sample=sample, cached=False)
        except Exception as exc:
            return _preview(normalized, ok=False, error=str(exc))

    def _source_summary(self, source):
        preview = self.preview_source(source)
        return {
            "id": source["id"],
            "title": source["title"],
            "kind": source["kind"],
            "requiredKeys": list(source.get("keyNames") or []),
            "firmwareSupported": _firmware_supported(source["kind"]),
            "health": "ok" if preview["ok"] else "needs-attention",
            "lastPreview": preview["sample"] if preview["ok"] else None,
            "error": preview["error"],
            "missingKeys": preview["missingKeys"],
        }

    def _missing_keys(self, source):
        required = set(source.get("keyNames") or [])
        if self.available_keys is None:
            return []
        return sorted(required - self.available_keys)

    def _cached(self, key):
        item = self._cache.get(key)
        if not item:
            return None
        if time.time() - item["at"] > self.ttl_seconds:
            self._cache.pop(key, None)
            return None
        return item["sample"]

    def _market_crypto(self, source, timeout):
        config = source["config"]
        symbol = str(config.get("symbol") or "BTC").upper()
        currency = str(config.get("currency") or "USD").lower()
        coin_id = str(config.get("coinId") or _coin_id_for_symbol(symbol))
        payload = self.fetcher(
            "https://api.coingecko.com/api/v3/simple/price?"
            + urlencode({
                "ids": coin_id,
                "vs_currencies": currency,
                "include_24hr_change": "true",
            }),
            min(int(timeout or 5), 5),
            None,
        )
        coin = payload.get(coin_id) or {}
        price = coin.get(currency)
        if price is None:
            raise ValueError("crypto payload missing price")
        return {
            "symbol": symbol,
            "currency": currency.upper(),
            "price": price,
            "change24h": coin.get(f"{currency}_24h_change"),
            "asOf": int(time.time()),
        }

    def _market_stocks(self, source, timeout):
        symbols = [str(symbol).upper() for symbol in (source["config"].get("symbols") or ["AAPL", "TSLA", "NVDA"])[:8]]
        try:
            payload = self.fetcher(
                "https://query1.finance.yahoo.com/v7/finance/quote?"
                + urlencode({"symbols": ",".join(symbols)}),
                min(int(timeout or 5), 5),
                None,
            )
            results = ((payload.get("quoteResponse") or {}).get("result") or [])
            provider = "yahoo"
        except Exception:
            results = self._twelve_data_demo_quotes(symbols, timeout)
            provider = "twelvedata-demo"
        rows = []
        for item in results[:5]:
            if not item.get("symbol"):
                continue
            rows.append({
                "symbol": str(item["symbol"]).upper(),
                "price": item.get("regularMarketPrice") if "regularMarketPrice" in item else item.get("price"),
                "change": item.get("regularMarketChangePercent") if "regularMarketChangePercent" in item else item.get("change"),
            })
        if not rows:
            raise ValueError("stocks payload missing quotes")
        return {
            "symbols": symbols,
            "rows": rows,
            "provider": provider,
            "partial": len(rows) < len(symbols),
        }

    def _twelve_data_demo_quotes(self, symbols, timeout):
        rows = []
        for symbol in symbols[:5]:
            try:
                payload = self.fetcher(
                    "https://api.twelvedata.com/quote?"
                    + urlencode({"symbol": symbol, "apikey": "demo"}),
                    min(int(timeout or 5), 5),
                    None,
                )
            except Exception:
                continue
            if payload.get("symbol") and payload.get("close") is not None:
                rows.append({
                    "symbol": payload.get("symbol"),
                    "price": _float_or_none(payload.get("close")),
                    "change": _float_or_none(payload.get("percent_change")),
                })
        return rows

    def _weather(self, source, timeout):
        config = source["config"]
        city = str(config.get("city") or config.get("label") or "Local")
        lat = config.get("latitude")
        lon = config.get("longitude")
        if lat is None or lon is None:
            geo = self.fetcher(
                "https://geocoding-api.open-meteo.com/v1/search?"
                + urlencode({"name": city, "count": 1, "language": "en", "format": "json"}),
                min(int(timeout or 5), 5),
                None,
            )
            results = geo.get("results") or []
            if not results:
                raise ValueError("weather location not found")
            city = results[0].get("name") or city
            lat = results[0].get("latitude")
            lon = results[0].get("longitude")
        payload = self.fetcher(
            "https://api.open-meteo.com/v1/forecast?"
            + urlencode({
                "latitude": lat,
                "longitude": lon,
                "current": "temperature_2m,relative_humidity_2m,weather_code",
            }),
            min(int(timeout or 5), 5),
            None,
        )
        current = payload.get("current") or {}
        if "temperature_2m" not in current:
            raise ValueError("weather payload missing current temperature")
        return {
            "city": city,
            "tempC": current.get("temperature_2m"),
            "condition": _weather_code_name(current.get("weather_code")),
            "humidity": current.get("relative_humidity_2m"),
        }

    def _custom_http(self, source, timeout):
        url = str(source["config"].get("url") or "").strip()
        if not url:
            raise ValueError("custom source requires config.url")
        payload = self.fetcher(url, min(int(timeout or 5), 5), None)
        value = _json_path(payload, str(source["config"].get("jsonPath") or ""))
        return {"value": value}


def _preview(source, ok, sample=None, missing=None, error=None, cached=False):
    return {
        "ok": ok,
        "sourceId": source["id"],
        "kind": source["kind"],
        "sample": sample or {},
        "fields": sorted((sample or {}).keys()),
        "requiredKeys": list(source.get("keyNames") or []),
        "missingKeys": missing or [],
        "firmwareSupported": _firmware_supported(source["kind"]),
        "error": error,
        "cached": cached,
        "fetchedAt": int(time.time()) if ok else None,
    }


def _error(source_id, kind, message):
    return {
        "ok": False,
        "sourceId": source_id,
        "kind": kind,
        "sample": {},
        "fields": [],
        "requiredKeys": [],
        "missingKeys": [],
        "firmwareSupported": False,
        "error": message,
    }


def _firmware_supported(kind):
    return kind in {"market.crypto", "market.stocks", "watchlist"}


def _fetch_json(url, timeout, headers=None):
    req = urllib.request.Request(url, headers={"Accept": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as res:
        raw = res.read(1024 * 128)
    return json.loads(raw.decode("utf-8"))


def _json_path(payload, path):
    value = payload
    for part in [p for p in path.split(".") if p]:
        if isinstance(value, dict) and part in value:
            value = value[part]
        else:
            raise ValueError(f"json path not found: {path}")
    return value


def _coin_id_for_symbol(symbol):
    return {"BTC": "bitcoin", "ETH": "ethereum", "SOL": "solana"}.get(symbol.upper(), symbol.lower())


def _weather_code_name(code):
    try:
        code = int(code)
    except (TypeError, ValueError):
        return "unknown"
    if code == 0:
        return "clear"
    if code in {1, 2, 3}:
        return "partly_cloudy"
    if code in {45, 48}:
        return "fog"
    if 51 <= code <= 67 or 80 <= code <= 82:
        return "rain"
    if 71 <= code <= 77:
        return "snow"
    if 95 <= code <= 99:
        return "storm"
    return "unknown"


def _float_or_none(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return None
