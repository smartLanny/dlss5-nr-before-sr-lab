#!/usr/bin/env python3
"""Stage the locked Vulkan-to-D3D12 NRExternalProviderV1 transport source."""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    lock = json.loads((here / "source-lock.json").read_text(encoding="utf-8"))
    upstream = args.upstream.resolve()
    output = args.output.resolve()
    if output.exists():
        raise SystemExit(f"output already exists: {output}")
    commit = subprocess.check_output(
        ["git", "-C", str(upstream), "rev-parse", "HEAD"], text=True
    ).strip()
    if commit != lock["upstream"]["commit"]:
        raise SystemExit(f"upstream mismatch: expected {lock['upstream']['commit']}, got {commit}")
    if subprocess.check_output(
        ["git", "-C", str(upstream), "status", "--porcelain"], text=True
    ).strip():
        raise SystemExit("upstream checkout is dirty")
    for name, expected in lock["upstream"]["sourceSha256"].items():
        if sha256(upstream / name) != expected:
            raise SystemExit(f"locked source changed: {name}")
    shutil.copytree(
        upstream, output,
        ignore=shutil.ignore_patterns(".git", "build", "bin", "obj"),
    )
    patch = here / lock["patch"]["file"]
    if sha256(patch) != lock["patch"]["sha256"]:
        raise SystemExit("transport patch hash mismatch")
    subprocess.run(
        ["git", "apply", "--whitespace=nowarn", str(patch)],
        cwd=output, check=True,
    )
    manifest = {
        "schema": "dlss5-vulkan-provider-source-stage-v1",
        "upstream": lock["upstream"],
        "patch": lock["patch"],
        "outputFiles": {
            name: sha256(output / name) for name in lock["outputSha256"]
        },
    }
    if manifest["outputFiles"] != lock["outputSha256"]:
        raise SystemExit("staged output does not match the locked transport")
    (output / "vulkan-provider-source-manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
