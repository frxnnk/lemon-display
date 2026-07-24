import os
import sys
import tempfile
import unittest


sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))


class StudioKeysTest(unittest.TestCase):
    def test_key_vault_masks_values_when_listing(self):
        from studio.keys import KeyVault

        with tempfile.TemporaryDirectory() as tmp:
            vault = KeyVault(os.path.join(tmp, "keys.json"))
            saved = vault.save("coingecko", "abc123456789")
            listed = vault.list()

        self.assertEqual(saved["name"], "COINGECKO")
        self.assertEqual(saved["masked"], "ab********89")
        self.assertNotIn("value", saved)
        self.assertNotIn("abc123456789", repr(listed))

    def test_empty_key_value_is_rejected(self):
        from studio.keys import KeyVault

        with tempfile.TemporaryDirectory() as tmp:
            vault = KeyVault(os.path.join(tmp, "keys.json"))
            with self.assertRaises(ValueError):
                vault.save("github_pat", "")


if __name__ == "__main__":
    unittest.main()
