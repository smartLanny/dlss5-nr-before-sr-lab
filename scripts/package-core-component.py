#!/usr/bin/env python3
"""Package bilingual Core candidates from one canonical generated build stage."""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import zipfile
from pathlib import Path

BASE_CAPABILITIES = [
    "same-frame-output",
    "source-frame-claims",
    "external-exact-fence-completion",
    "present-color-depth-motion",
    "multi-pass-nr",
]


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def archive(root: Path, target: Path) -> None:
    with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as output:
        for path in sorted(root.rglob("*")):
            if path.is_file():
                output.write(path, path.relative_to(root).as_posix())


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", type=Path, required=True,
                        help="canonical development stage containing source/core/build")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--id-prefix", required=True)
    parser.add_argument("--capability", action="append", default=[])
    args = parser.parse_args()
    stage, output = args.stage.resolve(), args.output.resolve()
    build = stage / "source" / "core" / "build"
    if output.exists():
        raise ValueError("Use a new output directory; prior candidate evidence is retained")
    addons = sorted(build.glob("*.addon64"))
    zh = [path for path in addons if ".en.addon64" not in path.name]
    en = [path for path in addons if ".en.addon64" in path.name]
    chain, config = build / "nrchain_nvngx.dll", build / "nr_before_sr.ini"
    if len(zh) != 1 or len(en) != 1 or not chain.is_file() or not config.is_file():
        raise ValueError("Canonical bilingual Core, chain or generated config is missing")
    capabilities = list(dict.fromkeys([*BASE_CAPABILITIES, *args.capability]))
    output.mkdir(parents=True)
    packages = []
    for variant, addon in (("zh-CN", zh[0]), ("en", en[0])):
        package_id = f"{args.id_prefix}-{variant.lower()}"
        root = output / package_id
        root.mkdir()
        for source in (addon, chain, config):
            shutil.copy2(source, root / source.name)
        files = [
            {"path": addon.name, "sha256": sha(root / addon.name), "bytes": addon.stat().st_size, "architecture": "x64"},
            {"path": "nr_before_sr.ini", "sha256": sha(root / "nr_before_sr.ini"), "bytes": config.stat().st_size},
            {"path": "nrchain_nvngx.dll", "sha256": sha(root / "nrchain_nvngx.dll"), "bytes": chain.stat().st_size, "architecture": "x64"},
        ]
        manifest = {
            "schema": "dlss5-component-v1",
            "id": package_id,
            "kind": "core",
            "version": args.version,
            "variant": variant,
            "architecture": "x64",
            "interface": "NGX-D3D12-Feature1",
            "gameApis": ["dx12"],
            "hardwareFamilies": ["RTX40", "RTX50"],
            "inputInterfaces": ["NGX-D3D12-Feature1", "NRExternalProviderV1"],
            "supportsPresent": True,
            "capabilities": capabilities,
            "validation": {
                "status": "candidate",
                "sourceCommit": args.source_commit,
                "compileLinkVerified": True,
                "gpuVerified": False,
                "gameVerified": False,
            },
            "files": files,
        }
        write(root / "component-manifest.json", manifest)
        target = output / f"{package_id}.zip"
        archive(root, target)
        packages.append({"id": package_id, "file": target.name, "sha256": sha(target), "bytes": target.stat().st_size})
    write(output / "bundle-index.json", {
        "schema": "dlss5-core-bundle-v1",
        "sourceCommit": args.source_commit,
        "version": args.version,
        "capabilities": capabilities,
        "packages": packages,
    })


if __name__ == "__main__":
    main()
