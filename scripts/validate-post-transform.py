"""Verify the two actual generated Core sources used by post GPU comparison.

The GPU fixture owns the pixel comparison. This check proves that its two
shaders came from the source bytes named by their separate build receipts.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(source: Path, manifest: Path, label: str) -> dict[str, str]:
    source = source.resolve(strict=True)
    manifest = manifest.resolve(strict=True)
    data = json.loads(manifest.read_text(encoding="utf-8-sig"))
    actual = sha(source)
    recorded = data["files"]["core/src/nr_before_sr.cpp"]
    if actual != recorded:
        raise ValueError(f"{label} generated Core does not match its source manifest")
    content = source.read_text(encoding="utf-8-sig")
    shaders = re.findall(r'const char kCodecShader\[\] = R"HLSL\(([\s\S]*?)\)HLSL";', content)
    if len(shaders) != 1:
        raise ValueError(f"{label} production shader extraction is not unique")
    shader = shaders[0]
    for marker in ("SampleProxyStoredV2", "LoadPostArea", "LoadFinalNeuralStoredV2"):
        if marker not in shader:
            raise ValueError(f"{label} shader lacks post sampling: {marker}")
    return {"source": str(source), "source_sha256": actual,
            "manifest": str(manifest), "manifest_sha256": sha(manifest),
            "shader_sha256": hashlib.sha256(shader.encode("utf-8")).hexdigest()}


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--baseline-source", type=Path, required=True)
    p.add_argument("--baseline-manifest", type=Path, required=True)
    p.add_argument("--candidate-source", type=Path, required=True)
    p.add_argument("--candidate-manifest", type=Path, required=True)
    a = p.parse_args()
    baseline = verify(a.baseline_source, a.baseline_manifest, "baseline")
    candidate = verify(a.candidate_source, a.candidate_manifest, "candidate")
    if baseline["source_sha256"] == candidate["source_sha256"]:
        raise ValueError("Post comparison requires two distinct generated Core sources")
    print(json.dumps({"baseline": baseline, "candidate": candidate}, indent=2))


if __name__ == "__main__":
    main()
