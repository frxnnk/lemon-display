import json
import os
import sys
import tempfile
import unittest


sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))


class FakeResponse:
    def __init__(self, payload):
        self.payload = payload

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        return False

    def read(self, limit=-1):
        return json.dumps(self.payload).encode("utf-8")


class StudioDeviceRegistryTest(unittest.TestCase):
    def test_registry_masks_token_values(self):
        from studio.device_registry import DeviceRegistry

        with tempfile.TemporaryDirectory() as tmp:
            registry = DeviceRegistry(os.path.join(tmp, "devices.json"))
            registry.upsert({
                "id": "lemon-abc",
                "host": "192.168.1.50",
                "name": "Lemon Box",
                "token": "secret-token",
                "tokenRef": "DEVICE_TOKEN_LEMON_ABC",
            })
            listed = registry.list()

        self.assertEqual(listed[0]["tokenRef"], "DEVICE_TOKEN_LEMON_ABC")
        self.assertNotIn("secret-token", repr(listed))

    def test_upsert_preserves_existing_token_ref_when_reconnecting(self):
        from studio.device_registry import DeviceRegistry

        with tempfile.TemporaryDirectory() as tmp:
            registry = DeviceRegistry(os.path.join(tmp, "devices.json"))
            registry.upsert({
                "id": "lemon-abc",
                "host": "192.168.1.50",
                "name": "Lemon Box",
                "tokenRef": "DEVICE_TOKEN_LEMON_ABC",
            })
            reconnected = registry.upsert({
                "id": "lemon-abc",
                "host": "192.168.1.51",
                "name": "Lemon Box",
                "version": "5.1.2",
            })

        self.assertEqual(reconnected["host"], "192.168.1.51")
        self.assertEqual(reconnected["tokenRef"], "DEVICE_TOKEN_LEMON_ABC")

    def test_connect_reads_device_identity_and_stores_profile(self):
        from studio.device import DeviceClient
        from studio.device_registry import DeviceRegistry

        requests = []

        def opener(req, timeout=2):
            requests.append(req)
            return FakeResponse({
                "ok": True,
                "id": "lemon-001",
                "name": "Desk Lemon",
                "version": "5.1.2",
            })

        with tempfile.TemporaryDirectory() as tmp:
            registry = DeviceRegistry(os.path.join(tmp, "devices.json"))
            client = DeviceClient(registry=registry, opener=opener)
            connected = client.connect("192.168.1.50")

            self.assertEqual(connected["device"]["id"], "lemon-001")
            self.assertEqual(connected["device"]["host"], "192.168.1.50")
            self.assertEqual(registry.list()[0]["version"], "5.1.2")

        self.assertEqual(requests[0].full_url, "http://192.168.1.50/api/device")

    def test_pair_stores_token_in_vault_and_never_returns_it(self):
        from studio.device import DeviceClient
        from studio.device_registry import DeviceRegistry
        from studio.keys import KeyVault

        calls = []

        def opener(req, timeout=2):
            calls.append((req.full_url, req.get_method(), req.data))
            if req.full_url.endswith("/api/pair/start"):
                return FakeResponse({"ok": True, "pairingRequired": True, "code": "123456"})
            if req.full_url.endswith("/api/pair/confirm"):
                return FakeResponse({
                    "ok": True,
                    "id": "lemon-001",
                    "name": "Desk Lemon",
                    "token": "pair-token-secret",
                })
            return FakeResponse({"ok": True})

        with tempfile.TemporaryDirectory() as tmp:
            registry = DeviceRegistry(os.path.join(tmp, "devices.json"))
            vault = KeyVault(os.path.join(tmp, "keys.json"))
            client = DeviceClient(registry=registry, key_vault=vault, opener=opener)

            started = client.pair("192.168.1.50")
            confirmed = client.pair("192.168.1.50", code="123456")

            self.assertEqual(started["code"], "123456")
            self.assertEqual(confirmed["device"]["tokenRef"], "DEVICE_TOKEN_LEMON_001")
            self.assertNotIn("pair-token-secret", repr(confirmed))
            self.assertIn("DEVICE_TOKEN_LEMON_001", {item["name"] for item in vault.list()})

        self.assertTrue(calls[0][0].endswith("/api/pair/start"))
        self.assertTrue(calls[1][0].endswith("/api/pair/confirm"))

    def test_ota_upload_uses_long_timeout_for_firmware_body(self):
        from studio.device import DeviceClient
        from studio.device_registry import DeviceRegistry
        from studio.keys import KeyVault

        calls = []

        def opener(req, timeout=2):
            calls.append((req.full_url, timeout, req.data))
            return FakeResponse({"ok": True})

        with tempfile.TemporaryDirectory() as tmp:
            firmware = os.path.join(tmp, "firmware.bin")
            with open(firmware, "wb") as f:
                f.write(b"firmware")
            registry = DeviceRegistry(os.path.join(tmp, "devices.json"))
            vault = KeyVault(os.path.join(tmp, "keys.json"))
            vault.save("DEVICE_TOKEN_LEMON_ABC", "token")
            registry.upsert({
                "id": "lemon-abc",
                "host": "192.168.1.50",
                "baseUrl": "http://192.168.1.50",
                "tokenRef": "DEVICE_TOKEN_LEMON_ABC",
            })
            client = DeviceClient(registry=registry, key_vault=vault, opener=opener)
            client.ota_upload(firmware)

        self.assertTrue(calls[0][0].endswith("/api/ota/arm"))
        self.assertEqual(calls[0][1], 10)
        self.assertTrue(calls[1][0].endswith("/api/ota/upload"))
        self.assertEqual(calls[1][1], 90)


if __name__ == "__main__":
    unittest.main()
