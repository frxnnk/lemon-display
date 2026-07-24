import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"


class OtaPublicReleaseTests(unittest.TestCase):
    def test_ota_requests_do_not_send_embedded_github_pat_for_public_repo(self):
        ota = (SRC / "ota_manager.cpp").read_text(encoding="utf-8")

        self.assertNotIn("GITHUB_PAT", ota)
        self.assertNotIn("Authorization", ota)
        self.assertIn("application/octet-stream", ota)


if __name__ == "__main__":
    unittest.main()
