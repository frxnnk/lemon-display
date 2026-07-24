import os
import sys
import unittest


sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))


class StudioMcpServerTest(unittest.TestCase):
    def test_initialize_and_tools_list(self):
        from studio.mcp_server import handle_message

        init = handle_message({"jsonrpc": "2.0", "id": 1, "method": "initialize"})
        listed = handle_message({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})

        self.assertEqual(init["result"]["serverInfo"]["name"], "lemon-box-studio")
        names = {tool["name"] for tool in listed["result"]["tools"]}
        self.assertIn("save_draft", names)
        self.assertNotIn("flash", names)

    def test_tools_call_returns_structured_content(self):
        from studio.mcp_server import handle_message

        response = handle_message({
            "jsonrpc": "2.0",
            "id": 3,
            "method": "tools/call",
            "params": {"name": "list_capabilities", "arguments": {}},
        })

        self.assertEqual(response["result"]["content"][0]["type"], "text")
        self.assertEqual(response["result"]["structuredContent"]["mcp"]["permissions"], "draft-only")


if __name__ == "__main__":
    unittest.main()
