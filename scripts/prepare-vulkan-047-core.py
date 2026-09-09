#!/usr/bin/env python3
"""Stage the 0.4.7beta Core with the reviewed external-provider ABI.

The 0.4.7beta generator remains the source of the Core algorithm, UI, PE
identity and default INI.  This opt-in stage adds only the external-provider
consumer and its four exported entry points.  Feeder/Vulkan transport code is
kept outside this Core stage and is not relabelled as a Core implementation.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE_COMMIT = "6a0ad7684683993328a7a98a9bea83ac76b01a64"
CARRIER_COMMIT = "28aed4099b0fe1c207b20b5fee5364c0773c25c2"
EXTERNAL_FILES = (
    "nr_external_jitter.inl",
    "nr_external_observer_startup.inl",
    "nr_external_provider.h",
    "nr_external_provider_abi.h",
    "nr_external_provider_completion.h",
    "nr_external_provider_consumer.inl",
    "nr_external_provider_core.inl",
    "nr_external_provider_d3d12.h",
    "nr_external_provider_feedback.h",
    "nr_external_provider_feeder_map.h",
    "nr_external_provider_producer.h",
)


def run(*args: str, cwd: Path | None = None) -> bytes:
    return subprocess.run(list(args), cwd=cwd, check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout


def git(root: Path, *args: str) -> bytes:
    return run("git", "-C", str(root), *args)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def once(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        raise ValueError(f"build-script anchor changed: {old!r}")
    return text.replace(old, new, 1)


def external_build_script(source: str) -> str:
    source = once(source,
        "$ProjectRoot = Split-Path -Parent $PSScriptRoot",
        "$ProjectRoot = Join-Path $PSScriptRoot 'core'")
    source = once(source,
        "$Build = Join-Path $ProjectRoot 'build'",
        "$Build = Join-Path $PSScriptRoot 'build-external'")
    source = once(source,
        "$resolvedBuild = (Resolve-Path -LiteralPath $Build).Path",
        "$BuildItem = Get-Item -LiteralPath $Build -Force\n"
        "$resolvedBuild = $BuildItem.FullName\n"
        "$expectedBuild = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'build-external'))")
    source = once(source,
        "if (-not $resolvedBuild.StartsWith($ProjectRoot, [StringComparison]::OrdinalIgnoreCase)) {",
        "if (-not $BuildItem.PSIsContainer -or\n"
        "    ($BuildItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or\n"
        "    -not [String]::Equals($resolvedBuild, $expectedBuild, "
        "[StringComparison]::OrdinalIgnoreCase)) {")
    # prepare-multi-api-beta.py already freezes these three provenance values
    # in the staged 047 script.  Accept both its generated form and the
    # original build-script form so this wrapper remains composable.
    source_commit_generated = re.compile(
        r"\$SourceCommit = '[0-9a-f]{40}'\n\$LASTEXITCODE = 0")
    source_commit_original = "$SourceCommit = (& git -C $ProjectRoot rev-parse HEAD).Trim()"
    generated_matches = source_commit_generated.findall(source)
    if len(generated_matches) == 1:
        source = source.replace(generated_matches[0],
                                f"$SourceCommit = '{SOURCE_COMMIT}'\n$LASTEXITCODE = 0", 1)
    else:
        source = once(source, source_commit_original,
                      f"$SourceCommit = '{SOURCE_COMMIT}'\n$LASTEXITCODE = 0")
    tracked_generated = "$TrackedChanges = @('generated-native-carrier')\n$LASTEXITCODE = 0"
    tracked_original = "$TrackedChanges = @(& git -C $ProjectRoot status --porcelain --untracked-files=no)"
    if source.count(tracked_generated) == 1:
        source = source.replace(tracked_generated,
                                "$TrackedChanges = @('generated-vulkan-047-provider')\n$LASTEXITCODE = 0", 1)
    else:
        source = once(source, tracked_original,
                      "$TrackedChanges = @('generated-vulkan-047-provider')\n$LASTEXITCODE = 0")
    dirty_generated = "$SourceDirty = '1' # Generated experimental source; see source-manifest.json"
    dirty_original = "$SourceDirty = if ($TrackedChanges.Count -eq 0) { '0' } else { '1' }"
    if source.count(dirty_generated) == 1:
        source = source.replace(dirty_generated,
                                "$SourceDirty = '1' # Generated source; see source-manifest.json", 1)
    else:
        source = once(source, dirty_original,
                      "$SourceDirty = '1' # Generated source; see source-manifest.json")
    include = "    \"-I$(Join-Path $ProjectRoot 'src')\",\n"
    source = once(source,
        "    \"-I$(Join-Path $MinHook 'include')\",",
        include + "    \"-I$(Join-Path $MinHook 'include')\",")
    source = once(source,
        "$AddonSource = Join-Path $ProjectRoot 'src\\nr_before_sr.cpp'",
        "$AddonSource = Join-Path $PSScriptRoot 'provider.cpp'")
    return source


def tree_hashes(root: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise ValueError(f"generated output contains a symlink: {path}")
        if path.is_file() and path != root / "source-manifest.json":
            result[path.relative_to(root).as_posix()] = sha256(path.read_bytes())
    return result


def prepare(core: Path, carrier: Path, output: Path) -> None:
    if output.exists():
        raise ValueError(f"refusing to replace existing output: {output}")
    source_head = git(core, "rev-parse", "HEAD").decode().strip()
    # The provider seam itself is tracked in this worktree, so HEAD may be a
    # small integration commit above the pinned 047 Core.  The generator still
    # records SOURCE_COMMIT as the algorithm/UI provenance and the underlying
    # 047 integration generator verifies its reviewed source pins.
    if subprocess.run(
            ["git", "-C", str(core), "merge-base", "--is-ancestor",
             SOURCE_COMMIT, "HEAD"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode != 0:
        raise ValueError("Core worktree does not descend from pinned 0.4.7beta source")
    if git(carrier, "rev-parse", "HEAD").decode().strip() != CARRIER_COMMIT:
        raise ValueError("carrier checkout is not the pinned v1.4.12 source")
    if git(carrier, "status", "--porcelain").strip():
        raise ValueError("carrier checkout must be clean")

    interop = {}
    for name in EXTERNAL_FILES:
        path = core / "src" / "interop" / name
        if not path.is_file():
            raise ValueError(f"missing reviewed external-provider input: {path}")
        interop[name] = path.read_bytes()

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="nr-vulkan-047-", dir=output.parent) as tmp:
        temp = Path(tmp)
        stage = temp / "stage"
        # Reuse the actual 047 integration generator. It preserves the normal
        # 047 UI/algorithm source instead of starting from an older R5 Core.
        run("python", str(core / "scripts" / "prepare-multi-api-beta.py"),
            "--core", str(core), "--upstream", str(carrier), "--output", str(stage))

        destination = stage / "core" / "src" / "interop"
        for name, data in interop.items():
            (destination / name).write_bytes(data)

        provider_script = core / "scripts" / "prepare-external-provider-recovery.py"
        if not provider_script.is_file():
            raise ValueError("missing provider translation-unit generator")
        run("python", str(provider_script),
            "--source", str(stage / "core" / "src" / "nr_before_sr.cpp"),
            "--output", str(stage / "provider.cpp"))

        build_text = (stage / "core" / "scripts" / "build.ps1").read_text(encoding="utf-8-sig")
        (stage / "build-external.ps1").write_text(
            external_build_script(build_text), encoding="utf-8-sig", newline="\r\n")

        manifest_path = stage / "source-manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["vulkan_provider_core_047"] = {
            "source_commit": SOURCE_COMMIT,
            "source_head": source_head,
            "carrier_commit": CARRIER_COMMIT,
            "generator": "scripts/prepare-vulkan-047-core.py",
            "provider_generator": sha256(provider_script.read_bytes()),
            "external_interop": {
                f"src/interop/{name}": sha256(data) for name, data in interop.items()
            },
            "provider_cpp_sha256": sha256((stage / "provider.cpp").read_bytes()),
            "build_script_sha256": sha256((stage / "build-external.ps1").read_bytes()),
            "new_config_keys": [],
            "scope": "047 Core compile with four external-provider exports; no GPU/game validation",
        }
        manifest["files"] = tree_hashes(stage)
        manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                                 encoding="utf-8")
        if manifest["files"] != tree_hashes(stage):
            raise ValueError("generated manifest does not match staged files")
        if output.exists():
            raise ValueError(f"output appeared during preparation: {output}")
        shutil.move(str(stage), str(output))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=Path, default=ROOT)
    parser.add_argument("--carrier", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    prepare(args.core.resolve(), args.carrier.resolve(), args.output.resolve())
    print(f"Prepared 047 Vulkan-provider Core at {args.output}; offline compile only")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit(str(exc)) from exc
