"""Check stdio host reads and path confinement in an isolated fixture; no Editor operations."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    requests = [json.loads(line) for line in (root / "Tests/Host/stdio-smoke.jsonl").read_text().splitlines() if line]
    requests = [request for request in requests if request["id"] <= 9]
    with tempfile.TemporaryDirectory(prefix="unreal-agent-host-") as directory:
        project = Path(directory) / "TestProject.uproject"
        project.write_text(json.dumps({"FileVersion": 3, "EngineAssociation": "5.8"}), encoding="utf-8")
        result = subprocess.run([str(args.host.resolve()), "--project=" + str(project)],
                                input="\n".join(json.dumps(request) for request in requests) + "\n",
                                capture_output=True, text=True, encoding="utf-8", timeout=20, cwd=directory, check=True)
        responses = {item["id"]: item for line in result.stdout.splitlines() if (item := json.loads(line))}
        assert set(responses) == set(range(1, 10))
        assert responses[1]["result"]["serverInfo"]["name"] == "WorldDataMCPHost"
        assert "get_project_info" in {tool["name"] for tool in responses[2]["result"]["tools"]}
        for request_id in (3, 4, 5, 8):
            assert not responses[request_id]["result"].get("isError"), f"Read request {request_id} failed"
        assert responses[7]["result"]["isError"], "Traversal was not rejected"
        project_info = json.loads(responses[4]["result"]["content"][0]["text"])
        assert project_info["projectName"] == "TestProject"
        assert responses[6]["result"]["resources"]
        assert responses[9]["result"]["contents"]
        print(json.dumps({"success": True, "responses": len(responses), "scope": "stdio reads and traversal; no editor start/build"}))


if __name__ == "__main__":
    main()
