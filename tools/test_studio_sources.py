import os
import sys
import unittest


sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))


class StudioSourcesTest(unittest.TestCase):
    def test_registry_lists_real_source_capabilities(self):
        from studio.sources import DataSourceRegistry

        catalog = DataSourceRegistry().list_catalog()
        kinds = {item["kind"] for item in catalog}

        self.assertIn("market.crypto", kinds)
        self.assertIn("market.stocks", kinds)
        self.assertIn("weather.current", kinds)
        self.assertIn("custom.http_json", kinds)

    def test_builtin_preview_returns_normalized_payload(self):
        from studio.sources import DataSourceRegistry

        def fake_fetch(url, timeout, headers=None):
            return {"bitcoin": {"usd": 104235.42, "usd_24h_change": 2.14}}

        source = {
            "id": "btc",
            "kind": "market.crypto",
            "config": {"symbol": "BTC", "currency": "USD", "coinId": "bitcoin"},
        }
        preview = DataSourceRegistry(fetcher=fake_fetch).preview_source(source)

        self.assertTrue(preview["ok"])
        self.assertEqual(preview["sourceId"], "btc")
        self.assertEqual(preview["kind"], "market.crypto")
        self.assertIn("price", preview["sample"])
        self.assertEqual(preview["missingKeys"], [])

    def test_missing_required_key_does_not_expose_secret_values(self):
        from studio.sources import DataSourceRegistry

        source = {
            "id": "weather",
            "kind": "weather.current",
            "keyNames": ["WEATHER_API_KEY"],
            "config": {"city": "Buenos Aires", "apiKey": "secret-value"},
        }
        preview = DataSourceRegistry(available_keys=[]).preview_source(source)

        self.assertFalse(preview["ok"])
        self.assertEqual(preview["missingKeys"], ["WEATHER_API_KEY"])
        self.assertNotIn("secret-value", repr(preview))

    def test_custom_http_preview_uses_injected_fetcher_and_json_path(self):
        from studio.sources import DataSourceRegistry

        def fake_fetch(url, timeout, headers=None):
            self.assertEqual(url, "http://127.0.0.1/data.json")
            self.assertLessEqual(timeout, 5)
            return {"data": {"value": 42, "label": "ok"}}

        source = {
            "id": "custom",
            "kind": "custom.http_json",
            "config": {
                "url": "http://127.0.0.1/data.json",
                "jsonPath": "data.value",
            },
        }
        preview = DataSourceRegistry(fetcher=fake_fetch).preview_source(source)

        self.assertTrue(preview["ok"])
        self.assertEqual(preview["sample"], {"value": 42})

    def test_crypto_preview_fetches_coingecko_payload_and_uses_cache(self):
        from studio.sources import DataSourceRegistry

        calls = []

        def fake_fetch(url, timeout, headers=None):
            calls.append(url)
            self.assertIn("api.coingecko.com", url)
            self.assertIn("ids=bitcoin", url)
            self.assertIn("vs_currencies=usd", url)
            return {"bitcoin": {"usd": 109321.5, "usd_24h_change": 1.23}}

        source = {
            "id": "btc",
            "kind": "market.crypto",
            "config": {"provider": "coingecko", "coinId": "bitcoin", "currency": "usd"},
        }
        registry = DataSourceRegistry(fetcher=fake_fetch)

        first = registry.preview_source(source)
        second = registry.preview_source(source)

        self.assertTrue(first["ok"])
        self.assertEqual(first["sample"]["price"], 109321.5)
        self.assertEqual(first["sample"]["change24h"], 1.23)
        self.assertFalse(first["cached"])
        self.assertTrue(second["cached"])
        self.assertEqual(len(calls), 1)

    def test_stock_preview_fetches_quote_payload(self):
        from studio.sources import DataSourceRegistry

        def fake_fetch(url, timeout, headers=None):
            self.assertIn("query1.finance.yahoo.com", url)
            self.assertIn("AAPL%2CMSFT", url)
            return {
                "quoteResponse": {
                    "result": [
                        {"symbol": "AAPL", "regularMarketPrice": 200.5, "regularMarketChangePercent": 0.8},
                        {"symbol": "MSFT", "regularMarketPrice": 510.1, "regularMarketChangePercent": -0.2},
                    ]
                }
            }

        preview = DataSourceRegistry(fetcher=fake_fetch).preview_source({
            "id": "watch",
            "kind": "market.stocks",
            "config": {"symbols": ["aapl", "msft"]},
        })

        self.assertTrue(preview["ok"])
        self.assertEqual(preview["sample"]["symbols"], ["AAPL", "MSFT"])
        self.assertEqual(preview["sample"]["rows"][0]["price"], 200.5)

    def test_stock_preview_falls_back_to_twelve_data_partial_rows(self):
        from studio.sources import DataSourceRegistry

        calls = []

        def fake_fetch(url, timeout, headers=None):
            calls.append(url)
            if "query1.finance.yahoo.com" in url:
                raise RuntimeError("HTTP Error 429: Too Many Requests")
            self.assertIn("api.twelvedata.com", url)
            return {
                "symbol": "AAPL",
                "close": "315.70",
                "percent_change": "-0.50",
            }

        preview = DataSourceRegistry(fetcher=fake_fetch).preview_source({
            "id": "watch",
            "kind": "market.stocks",
            "config": {"symbols": ["AAPL"]},
        })

        self.assertTrue(preview["ok"])
        self.assertEqual(preview["sample"]["rows"][0]["symbol"], "AAPL")
        self.assertEqual(preview["sample"]["rows"][0]["price"], 315.7)
        self.assertEqual(preview["sample"]["provider"], "twelvedata-demo")
        self.assertEqual(len(calls), 2)

    def test_weather_preview_uses_open_meteo_without_default_key(self):
        from studio.sources import DataSourceRegistry

        def fake_fetch(url, timeout, headers=None):
            if "geocoding-api.open-meteo.com" in url:
                return {"results": [{"name": "Buenos Aires", "latitude": -34.6, "longitude": -58.38}]}
            self.assertIn("api.open-meteo.com", url)
            self.assertIn("latitude=-34.6", url)
            return {
                "current": {
                    "temperature_2m": 21.4,
                    "relative_humidity_2m": 54,
                    "weather_code": 1,
                }
            }

        preview = DataSourceRegistry(available_keys=[], fetcher=fake_fetch).preview_source({
            "id": "weather",
            "kind": "weather.current",
            "config": {"city": "Buenos Aires"},
        })

        self.assertTrue(preview["ok"])
        self.assertEqual(preview["missingKeys"], [])
        self.assertEqual(preview["sample"]["city"], "Buenos Aires")
        self.assertEqual(preview["sample"]["tempC"], 21.4)

    def test_invalid_custom_json_path_returns_error_state(self):
        from studio.sources import DataSourceRegistry

        preview = DataSourceRegistry(fetcher=lambda url, timeout, headers=None: {"data": {}}).preview_source({
            "id": "custom",
            "kind": "custom.http_json",
            "config": {"url": "http://127.0.0.1/data.json", "jsonPath": "data.value"},
        })

        self.assertFalse(preview["ok"])
        self.assertIn("json path not found", preview["error"])


if __name__ == "__main__":
    unittest.main()
