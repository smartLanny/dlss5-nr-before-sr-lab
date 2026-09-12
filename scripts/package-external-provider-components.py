#!/usr/bin/env python3
"""Rebuild strict external-provider component packages from compiled candidates.

The input is the retained r1 evidence layout. The script fixes route ownership
and installation bases without changing any compiled binary, then writes a new
directory and zip for each Provider. Core, Core chain/config and NR runtime are
deliberately absent because the Manager injects their current inventory objects.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import zipfile
from pathlib import Path

CORE_CAPABILITIES = [
    "same-frame-output",
    "source-frame-claims",
    "external-exact-fence-completion",
    "present-color-depth-motion",
]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def spec(role: str, file: str, base: str, target: str,
         architecture: str | None = None, mutable: bool = False) -> dict:
    return {
        "role": role,
        "file": file,
        "base": base,
        "target": target,
        "architecture": architecture,
        "mutable": mutable,
    }


def runtime_assets(root: Path) -> list[dict]:
    rows: list[dict] = []
    for path in sorted((root / "shaders").rglob("*")):
        if path.is_file():
            rel = path.relative_to(root).as_posix()
            rows.append(spec("shader", rel, "runtime", f"reshade-shaders/Shaders/{path.relative_to(root / 'shaders').as_posix()}"))
    for path in sorted((root / "textures").rglob("*")):
        if path.is_file():
            rel = path.relative_to(root).as_posix()
            rows.append(spec("texture", rel, "runtime", f"reshade-shaders/Textures/{path.relative_to(root / 'textures').as_posix()}"))
    for path in sorted((root / "licenses").rglob("*")):
        if path.is_file():
            rel = path.relative_to(root).as_posix()
            rows.append(spec("license", rel, "runtime", rel))
    preset = root / "config" / "ReShadePreset.ini"
    if preset.is_file():
        rows.append(spec("preset", "config/ReShadePreset.ini", "runtime", "ReShadePreset.ini", mutable=True))
    return rows


def route(route_id: str, api: str, architecture: str, files: list[dict], *,
          host: bool = False, relay: str | None = None, wrapper: dict | None = None) -> dict:
    return {
        "id": route_id,
        "api": api,
        "architecture": architecture,
        "hardwareFamilies": ["RTX40", "RTX50"],
        "loadingBackend": "local",
        "proxyEntries": ["d3d9"] if api == "dx9" else ["auto", "dxgi"],
        "hostRequired": host,
        "transport": "ipc-v9-exact-frame-completion" if host else "same-process-private-d3d12-external-v1",
        "relay": relay,
        "wrapper": wrapper,
        "coreDirectory": "host64/addons" if host else "",
        "runtimeDirectory": "host64/addons" if host else "",
        "files": files,
    }


def provider_manifest(root: Path, route_id: str) -> None:
    manifest_file = root / "external-provider-package.json"
    manifest = json.loads(manifest_file.read_text(encoding="utf-8-sig"))
    manifest["interface"]["requiredCoreCapabilities"] = CORE_CAPABILITIES
    provider = spec("provider", "provider/dlss5-feed.addon64", "addon", "dlss5-feed.addon64", "x64")
    loader = spec("game-loader", "transport/ReShade64.dll", "game", "dxgi.dll", "x64")
    files = [loader, provider, *runtime_assets(root)]
    manifest["routes"] = [route(route_id, "dx12", "x64", files)]
    write_json(manifest_file, manifest)


def legacy_manifest(root: Path) -> None:
    manifest_file = root / "external-provider-package.json"
    manifest = json.loads(manifest_file.read_text(encoding="utf-8-sig"))
    manifest["interface"]["requiredCoreCapabilities"] = CORE_CAPABILITIES
    assets = runtime_assets(root)

    def direct(api: str) -> dict:
        files = [
            spec("game-loader", "transport/ReShade64.dll", "game", "dxgi.dll", "x64"),
            spec("provider", "provider/dlss5-feed.addon64", "addon", "dlss5-feed.addon64", "x64"),
            *assets,
        ]
        return route(f"{api}-x64-legacy-direct", api, "x64", files)

    def hosted(api: str, architecture: str, relay64: bool = False) -> dict:
        bits = "32" if architecture == "x86" else "64"
        provider_file = "provider/dlss5-feed-relay.addon64" if relay64 else f"provider/dlss5-feed.addon{bits}"
        files = [
            spec("game-loader", f"transport/ReShade{bits}.dll", "game", "dxgi.dll", architecture),
            spec("provider", provider_file, "addon", f"dlss5-feed.addon{bits}", architecture),
            spec("host", "provider/dlss5-feed-host64.exe", "addon", "host64/dlss5-feed-host64.exe", "x64"),
            spec("host-loader", "transport/ReShade64.dll", "addon", "host64/dxgi.dll", "x64"),
            *assets,
        ]
        return route(f"{api}-{architecture}-host", api, architecture, files,
                     host=True, relay="project-frame-completion-v1")

    def dx9(architecture: str) -> dict:
        bits = "32" if architecture == "x86" else "64"
        wrapper = {
            "id": f"d3d9on12-{architecture}",
            "version": "source-owned-r5",
            "outputApi": "dx12",
            "entry": "d3d9.dll",
            "systemRuntime": True,
            "privateRuntimeBundled": False,
            "minimumWindows": "Windows 10 1903",
        }
        files = [
            spec("api-wrapper", f"transport/d3d9-{architecture}.dll", "game", "d3d9.dll", architecture),
            spec("game-loader", f"transport/ReShade{bits}.dll", "runtime", f"ReShade{bits}.dll", architecture),
            spec("provider", f"provider/dlss5-feed-dx9.addon{bits}", "addon", f"dlss5-feed.addon{bits}", architecture),
            spec("host", "provider/dlss5-feed-host64.exe", "addon", "host64/dlss5-feed-host64.exe", "x64"),
            spec("host-loader", "transport/ReShade64.dll", "addon", "host64/dxgi.dll", "x64"),
            *assets,
        ]
        return route(f"dx9-{architecture}-host", "dx9", architecture, files,
                     host=True, relay="project-frame-completion-v1", wrapper=wrapper)

    manifest["routes"] = [
        direct("dx12"),
        direct("dx11"),
        hosted("dx11", "x86"),
        hosted("dx10", "x86"),
        hosted("dx10", "x64", relay64=True),
        dx9("x86"),
        dx9("x64"),
    ]
    write_json(manifest_file, manifest)


def component_manifest(root: Path, package_id: str, version: str, variant: str,
                       architecture: str, apis: list[str], capabilities: list[str],
                       source_commit: str) -> dict:
    pe_arch = {
        "provider/dlss5-feed.addon64": "x64",
        "provider/dlss5-feed.addon32": "x86",
        "provider/dlss5-feed-relay.addon64": "x64",
        "provider/dlss5-feed-host64.exe": "x64",
        "provider/dlss5-feed-dx9.addon32": "x86",
        "provider/dlss5-feed-dx9.addon64": "x64",
        "transport/ReShade32.dll": "x86",
        "transport/ReShade64.dll": "x64",
        "transport/d3d9-x86.dll": "x86",
        "transport/d3d9-x64.dll": "x64",
    }
    files = []
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.name == "component-manifest.json":
            continue
        rel = path.relative_to(root).as_posix()
        row = {"path": rel, "sha256": digest(path), "bytes": path.stat().st_size}
        if rel in pe_arch:
            row["architecture"] = pe_arch[rel]
        files.append(row)
    return {
        "schema": "dlss5-component-v1",
        "id": package_id,
        "kind": "feeder",
        "version": version,
        "variant": variant,
        "architecture": architecture,
        "interface": "NRExternalProviderV1",
        "gameApis": apis,
        "hardwareFamilies": ["RTX40", "RTX50"],
        "inputInterfaces": ["NRExternalProviderV1"],
        "supportsPresent": False,
        "capabilities": capabilities,
        "validation": {
            "status": "candidate",
            "compileLinkVerified": True,
            "peImportsChecked": "optical-flow" in variant,
            "amdOfStaticLink": "optical-flow" in variant,
            "nvofCompiled": False,
            "gpuVerified": False,
            "gameVerified": False,
            "sourceCommit": source_commit,
        },
        "files": files,
    }


def zip_tree(root: Path, output: Path) -> None:
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(root.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(root).as_posix())


def prepare(input_root: Path, output_root: Path, source_commit: str) -> None:
    if output_root.exists():
        raise ValueError("Use a new output directory; prior candidate evidence is retained")
    output_root.mkdir(parents=True)
    legacy_source = input_root / "feeder-legacy-host-d15-r1"
    loader = legacy_source / "transport" / "ReShade64.dll"
    if not loader.is_file():
        raise ValueError("The retained legacy input does not contain ReShade64.dll")

    entries = [
        ("feeder-stable-external-of-r2", "feeder-stable-external-of-r1", "0.15.1", "stable-optical-flow-amd-static", ["dx12"],
         ["same-frame-output", "integrated-amd-of-100-50", "nvof-source-present"]),
        ("feeder-preview-external-of-r2", "feeder-preview-external-of-r1", "1.16.0-beta.1", "preview-optical-flow-amd-static", ["dx12"],
         ["same-frame-output", "integrated-amd-of-100-50", "nvof-source-present"]),
        ("feeder-legacy-host-d15-r2", "feeder-legacy-host-d15-r1", "0.15.1-d15-adapter-r2", "legacy-ipc-v9", ["dx9", "dx10", "dx11", "dx12"],
         ["same-frame-output", "ipc-exact-frame-completion", "host-vort-guides"]),
    ]
    index = []
    for package_id, source_name, version, variant, apis, capabilities in entries:
        source = input_root / source_name
        target = output_root / package_id
        shutil.copytree(source, target)
        (target / "component-manifest.json").unlink(missing_ok=True)
        if "optical-flow" in variant:
            (target / "config" / "dlss5-feed.cfg").unlink(missing_ok=True)
            (target / "transport").mkdir(exist_ok=True)
            shutil.copy2(loader, target / "transport" / "ReShade64.dll")
            provider_manifest(target, "dx12-x64-stable-of" if version == "0.15.1" else "dx12-x64-preview-of")
            architecture = "x64"
        else:
            legacy_manifest(target)
            architecture = "mixed"
        component = component_manifest(target, package_id, version, variant, architecture, apis, capabilities, source_commit)
        write_json(target / "component-manifest.json", component)
        archive = output_root / f"{package_id}.zip"
        zip_tree(target, archive)
        index.append({"id": package_id, "file": archive.name, "sha256": digest(archive), "bytes": archive.stat().st_size})
    write_json(output_root / "bundle-index.json", {"schema": "dlss5-provider-bundle-v1", "sourceCommit": source_commit, "packages": index})


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    args = parser.parse_args()
    prepare(args.input.resolve(), args.output.resolve(), args.source_commit)


if __name__ == "__main__":
    main()
