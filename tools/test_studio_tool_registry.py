import os
import sys
import tempfile
import unittest


sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))


class StudioToolRegistryTest(unittest.TestCase):
    def test_tool_list_is_draft_only(self):
        from studio.tool_registry import StudioToolRegistry

        names = {tool["name"] for tool in StudioToolRegistry().list_tools()}

        self.assertIn("get_current_experience", names)
        self.assertIn("create_experience_draft", names)
        self.assertIn("validate_experience", names)
        self.assertNotIn("flash", names)
        self.assertNotIn("recovery_flash", names)
        self.assertNotIn("build_firmware", names)

    def test_validate_experience_reports_preview_only_cards(self):
        from studio.tool_registry import StudioToolRegistry

        result = StudioToolRegistry().call_tool("validate_experience", {
            "experience": {
                "cards": [{"title": "Custom", "kind": "custom"}],
            }
        })

        self.assertFalse(result["ok"])
        self.assertIn("custom", " ".join(result["warnings"]).lower())

    def test_save_draft_writes_without_secret_values(self):
        from studio.tool_registry import StudioToolRegistry

        with tempfile.TemporaryDirectory() as tmp:
            registry = StudioToolRegistry(repo_root=tmp)
            result = registry.call_tool("save_draft", {
                "prompt": "Create a private weather screen",
                "experience": {
                    "sources": [{
                        "id": "weather",
                        "kind": "weather.current",
                        "keyNames": ["WEATHER_API_KEY"],
                        "config": {"apiKey": "secret-value"},
                    }],
                },
            })
            with open(result["path"], "r", encoding="utf-8") as f:
                raw = f.read()

        self.assertIn("WEATHER_API_KEY", raw)
        self.assertNotIn("secret-value", raw)
        self.assertEqual(result["kind"], "lemon-box-agent-draft")

    def test_destructive_tool_names_are_rejected(self):
        from studio.tool_registry import StudioToolRegistry

        with self.assertRaises(ValueError):
            StudioToolRegistry().call_tool("flash", {})


if __name__ == "__main__":
    unittest.main()
