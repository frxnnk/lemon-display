import json
import sys

from .tool_registry import StudioToolRegistry


def handle_message(message, registry=None):
    registry = registry or StudioToolRegistry()
    method = message.get("method")
    req_id = message.get("id")
    params = message.get("params") or {}

    if method == "initialize":
        return _result(req_id, {
            "protocolVersion": "2024-11-05",
            "capabilities": {"tools": {"listChanged": False}},
            "serverInfo": {"name": "lemon-box-studio", "version": "1.0.0"},
        })
    if method == "tools/list":
        return _result(req_id, {"tools": registry.list_tools()})
    if method == "tools/call":
        name = params.get("name")
        arguments = params.get("arguments") or {}
        output = registry.call_tool(name, arguments)
        return _result(req_id, {
            "content": [{"type": "text", "text": json.dumps(output, ensure_ascii=True)}],
            "structuredContent": output,
        })
    if req_id is None:
        return None
    return _error(req_id, -32601, f"method not found: {method}")


def serve(stdin=None, stdout=None, registry=None):
    stdin = stdin or sys.stdin
    stdout = stdout or sys.stdout
    registry = registry or StudioToolRegistry()
    for line in stdin:
        if not line.strip():
            continue
        try:
            response = handle_message(json.loads(line), registry)
        except Exception as exc:
            response = _error(None, -32000, str(exc))
        if response is not None:
            stdout.write(json.dumps(response, ensure_ascii=True) + "\n")
            stdout.flush()


def _result(req_id, result):
    return {"jsonrpc": "2.0", "id": req_id, "result": result}


def _error(req_id, code, message):
    return {"jsonrpc": "2.0", "id": req_id, "error": {"code": code, "message": message}}


def main():
    serve()


if __name__ == "__main__":
    main()
