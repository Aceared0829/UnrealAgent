"""Validate publishable Git files, lock consistency and basic secret hygiene; not a security audit."""
import hashlib
import json
from pathlib import Path
import re
import subprocess


def main():
    root = Path(__file__).resolve().parents[1]
    paths = subprocess.check_output(["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"], cwd=root)
    files = sorted(set(path.decode("utf-8") for path in paths.split(b"\0") if path))
    assert files, "No publishable files found"
    forbidden = {"Binaries", "Intermediate", "Saved", "node_modules", "__pycache__", ".vs", ".idea"}
    failures = []
    total = 0
    for name in files:
        path = root / name
        if not path.is_file():
            failures.append(name + ": missing file")
            continue
        size = path.stat().st_size
        total += size
        if forbidden.intersection(path.relative_to(root).parts) or size > 10 * 1024 * 1024:
            failures.append(name + ": generated or oversized file")
        if path.suffix.lower() in {".exe", ".dll", ".pdb", ".obj", ".lib", ".mp4", ".uasset", ".umap", ".zip"}:
            failures.append(name + ": binary artifact")
        try:
            text = path.read_text(encoding="utf-8-sig")
        except UnicodeDecodeError:
            if path.suffix.lower() not in {".png", ".ico", ".jpg", ".svg"}:
                failures.append(name + ": unexpected binary")
            continue
        for pattern in (r"gh[pousr]_[A-Za-z0-9]{30,}", r"sk-(?:proj-)?[A-Za-z0-9_-]{32,}",
                        r"-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----", r"AKIA[A-Z0-9]{16}"):
            if re.search(pattern, text):
                failures.append(name + ": potential secret (value suppressed)")
    manifest = json.loads((root / "Scripts/CodexACP.package.json").read_text())
    lock_path = root / "Scripts/CodexACP.package-lock.json"
    lock = json.loads(lock_path.read_text())
    version = manifest["dependencies"]["@agentclientprotocol/codex-acp"]
    assert lock["packages"]["node_modules/@agentclientprotocol/codex-acp"]["version"] == version
    digest = hashlib.sha256(lock_path.read_bytes()).hexdigest()
    installer = (root / "Scripts/Install-AgentCli.ps1").read_text(encoding="utf-8-sig")
    assert digest in installer, "Lock SHA-256 is stale"
    assert f'$lockedVersion = "{version}"' in installer
    assert f'$Version = "{version}"' in (root / "Scripts/Install-CodexACP.ps1").read_text()
    for path in (root / "Config").glob("*.json"):
        json.loads(path.read_text(encoding="utf-8-sig"))
    json.loads((root / "UnrealAgent.uplugin").read_text(encoding="utf-8-sig"))
    assert not failures, "\n".join(failures)
    print(json.dumps({"success": True, "files": len(files), "bytes": total,
                      "codex_acp": version, "lock_sha256": digest}))


if __name__ == "__main__":
    main()
