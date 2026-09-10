"""Check a real ACP adapter initialize response without account access or prompts."""
import argparse
import json
import os
from pathlib import Path
import queue
import subprocess
import tempfile
import threading


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--entry", type=Path, required=True, help="Installed codex-acp dist/index.js")
    parser.add_argument("--node", default="node")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="unreal-agent-acp-") as isolated:
        environment = os.environ.copy()
        environment["CODEX_HOME"] = isolated
        for key in ("OPENAI_API_KEY", "CODEX_API_KEY", "CHATGPT_AUTH_TOKEN"):
            environment.pop(key, None)
        process = subprocess.Popen([args.node, str(args.entry.resolve())], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                   text=True, encoding="utf-8", env=environment, cwd=isolated)
        messages = queue.Queue()

        def read():
            for line in process.stdout:
                try:
                    messages.put(json.loads(line))
                except json.JSONDecodeError:
                    pass

        threading.Thread(target=read, daemon=True).start()
        try:
            request = {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
                "protocolVersion": 1,
                "clientInfo": {"name": "unreal-agent-smoke", "version": "0.3.0"},
                "clientCapabilities": {"fs": {"readTextFile": True, "writeTextFile": False},
                                       "terminal": False, "session": {"configOptions": {}}}}}
            process.stdin.write(json.dumps(request) + "\n")
            process.stdin.flush()
            response = messages.get(timeout=30)
            assert response.get("id") == 1 and "error" not in response, "ACP initialize failed"
            result = response["result"]
            assert result["protocolVersion"] == 1
            assert result["agentInfo"]["version"] == "1.11.0"
            assert isinstance(result["agentCapabilities"], dict)
            print(json.dumps({"success": True, "protocol_version": result["protocolVersion"],
                              "adapter": result["agentInfo"], "capabilities": result["agentCapabilities"],
                              "scope": "initialize only; no account login, prompt or UE session"}))
        finally:
            process.stdin.close()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=5)


if __name__ == "__main__":
    main()
