"""Fetch pinned NVIDIA build-only components outside the source repository."""
from __future__ import annotations
import argparse
import hashlib
import io
import json
from pathlib import Path
import shutil
import urllib.request
import zipfile

CUDA_RELEASE = "13.0.0"
CUTLASS_COMMIT = "cb4247394dd82148787aed73e5dc7cef33cbf862"
CUTLASS_ARCHIVE_SHA256 = "910f9bfeb1e86b51d10ba4ef2c00b39b5004568edab109289c9951d9e49f1f83"
COMPONENTS = ("cuda_nvcc", "cuda_cudart", "cuda_cccl", "cuda_crt", "libnvvm", "cuda_nvdisasm", "cuda_cuobjdump")

def fetch(url: str, target: Path, expected: str | None = None) -> str:
    if not target.exists():
        target.parent.mkdir(parents=True, exist_ok=True)
        partial = target.with_suffix(target.suffix + ".partial")
        with urllib.request.urlopen(url, timeout=90) as response, partial.open("wb") as out:
            shutil.copyfileobj(response, out)
        partial.replace(target)
    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    if expected and digest != expected:
        raise RuntimeError(f"SHA256 mismatch: {target.name}; remove this archive before retrying")
    return digest

def unpack(archive: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as z:
        for info in z.infolist():
            parts = Path(info.filename).parts[1:]
            if not parts or info.is_dir():
                continue
            path = destination.joinpath(*parts)
            if not path.resolve().is_relative_to(destination.resolve()):
                raise RuntimeError("Unsafe archive path")
            path.parent.mkdir(parents=True, exist_ok=True)
            with z.open(info) as source, path.open("wb") as output:
                shutil.copyfileobj(source, output)

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--deps", type=Path, required=True)
    args = parser.parse_args()
    root = args.deps.resolve()
    root.mkdir(parents=True, exist_ok=True)
    base = "https://developer.download.nvidia.com/compute/cuda/redist/"
    manifest_file = root / "archives" / f"redistrib_{CUDA_RELEASE}.json"
    fetch(base + manifest_file.name, manifest_file)
    metadata = json.loads(manifest_file.read_text())
    receipts = []
    for name in COMPONENTS:
        entry = metadata[name]["windows-x86_64"]
        archive = root / "archives" / Path(entry["relative_path"]).name
        digest = fetch(base + entry["relative_path"], archive, entry["sha256"])
        print(f"verified {name} {metadata[name]['version']} {digest}", flush=True)
        marker = root / "cuda-13.0" / "receipts" / (name + ".sha256")
        if not marker.exists() or marker.read_text() != digest:
            unpack(archive, root / "cuda-13.0")
            marker.parent.mkdir(parents=True, exist_ok=True)
            marker.write_text(digest)
        receipts.append({"component": name, "version": metadata[name]["version"], "sha256": digest})
    cutlass_archive = root / "archives" / f"cutlass-{CUTLASS_COMMIT}.zip"
    digest = fetch(f"https://codeload.github.com/NVIDIA/cutlass/zip/{CUTLASS_COMMIT}", cutlass_archive, CUTLASS_ARCHIVE_SHA256)
    cutlass = root / "cutlass-4.7.1"
    if not (cutlass / "include" / "cutlass" / "cutlass.h").exists():
        unpack(cutlass_archive, cutlass)
    receipts.append({"component": "CUTLASS", "commit": CUTLASS_COMMIT, "archive_sha256": digest})
    (root / "dependency-receipt.json").write_text(json.dumps(receipts, indent=2))
    print(f"ready: {root}")

if __name__ == "__main__":
    main()
