#!/usr/bin/env python3
"""Compile the nine fixed auxiliary shaders, independent of the 26 codec CSOs."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import time

O3 = 1 << 15
STRICT = 1 << 11
GROUPS = {
    "present": [("nr_compat_present_shader.h", "kCompatPresentShader", "own-compat-present", entry, target, O3 | STRICT)
                for entry, target in (("ConvertCS", "cs_5_0"), ("InitColorCS", "cs_5_0"), ("InitCS", "cs_5_0"),
                                      ("FullscreenVS", "vs_5_0"), ("BlitPS", "ps_5_0"))],
    "dual": [("nr_dual_embedded.hpp", name, label, entry, "cs_5_0", O3 | STRICT)
             for name, label, entry in (("downsample", "nr_dual_downsample", "DownsampleCS"),
                                        ("residual", "nr_dual_residual", "ResidualCS"))],
    "guides": [("nr_native_guide_snapshot.inl", "kNativeGuideSnapshotShader", "NR.NativeGuideSnapshot", "SnapshotGuidesCS", "cs_5_0", O3)],
    "rr_depth": [("nr_rr_depth_shader.h", "kRrDepthShader", "NR.RRDepth", "ConvertRrDepthCS", "cs_5_0", O3)],
}

def sha(data):
    return hashlib.sha256(data).hexdigest()

def shader_body(path, symbol):
    text = path.read_text(encoding="utf-8")
    pattern = re.compile(r"\b" + re.escape(symbol) + r'\s*\[\s*\]\s*=\s*R"([^()]*)\((.*?)\)\1";', re.S)
    found = pattern.findall(text)
    if len(found) != 1:
        raise ValueError("Auxiliary shader must have one exact embedded source: " + symbol)
    return found[0][1].encode("utf-8")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("source", "compiler", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--cache-from", type=Path)
    args = parser.parse_args()
    source, compiler, output = args.source.resolve(), args.compiler.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    rebuilt = sha(compiler.read_bytes())
    if args.cache_from:
        # Share the existing complete-PE comparison; only build timestamps may
        # differ. Preserve original cache receipts and compiler identity.
        path = Path(__file__).with_name("prepare-codec-artifacts.py")
        spec = importlib.util.spec_from_file_location("codec_artifacts", path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        previous = args.cache_from / "compile-aux-artifact.exe"
        if module.compiler_identity(compiler.read_bytes()) != module.compiler_identity(previous.read_bytes()):
            raise ValueError("Auxiliary cache compiler differs from rebuilt executable")
        if previous.resolve() != compiler:
            shutil.copy2(previous, compiler)
    compiler_sha = sha(compiler.read_bytes())
    records, headers = [], {}
    for group, cases in GROUPS.items():
        header = ["#pragma once", "// Generated; build-time bytecode only. No resource or PSO lifetime changes.",
                  "namespace nr_aux_" + group + "_precompiled {",
                  "struct Blob {const unsigned char* data; SIZE_T size;};",
                  "inline bool Equal(const char* a,const char* b){if(!a||!b)return false;while(*a&&*a==*b){++a;++b;}return *a==*b;}"]
        entries = []
        for index, (filename, symbol, label, entry, target, flags) in enumerate(cases):
            body = shader_body(source / filename, symbol)
            identity = {"source_sha256": sha(body), "compiler_sha256": compiler_sha,
                        "source_label": label, "entry": entry, "target": target, "flags": flags, "macros": []}
            key = sha(json.dumps(identity, sort_keys=True).encode())
            folder = output / "aux" / key
            if args.cache_from and not folder.exists():
                cached = args.cache_from / "aux" / key
                if not cached.is_dir():
                    raise ValueError("No auxiliary cache for exact source/compiler/options: " + entry)
                shutil.copytree(cached, folder)
            folder.mkdir(parents=True, exist_ok=True)
            binary, receipt = folder / "shader.cso", folder / "artifact.json"
            if receipt.exists() and binary.exists():
                info = json.loads(receipt.read_text())
                if info.get("identity") != identity or info.get("sha256") != sha(binary.read_bytes()):
                    raise ValueError("Auxiliary cache mismatch; preserve it for inspection: " + entry)
            else:
                shader = folder / "source.hlsl"
                shader.write_bytes(body)
                start = time.monotonic()
                command = [str(compiler), str(shader), label, entry, target, str(flags), str(binary)]
                result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                (folder / "compile.log").write_bytes(result.stdout)
                if result.returncode:
                    raise RuntimeError("Auxiliary shader compilation failed: " + str(folder))
                info = {"identity": identity, "sha256": sha(binary.read_bytes()), "bytes": binary.stat().st_size,
                        "seconds": time.monotonic() - start}
                receipt.write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
            data = binary.read_bytes()
            if data[:4] != b"DXBC":
                raise ValueError("Expected actual SM5 bytecode: " + entry)
            header.append(f"inline constexpr unsigned char shader_{index}[]={{")
            header.extend(",".join(str(value) for value in data[n:n + 32]) + "," for n in range(0, len(data), 32))
            header.append("};")
            entries.append(f'if(Equal(entry,"{entry}")&&Equal(target,"{target}"))return {{shader_{index},sizeof(shader_{index})}};')
            records.append({"group": group, "file": str(binary.relative_to(output)), **info})
        header.extend(["inline Blob Find(const char* entry,const char* target){", *entries, "return {nullptr,0};", "}", "}"])
        path = output / ("nr_aux_" + group + "_precompiled.h")
        path.write_text("\n".join(header) + "\n", encoding="utf-8", newline="\n")
        headers[path.name] = sha(path.read_bytes())
    manifest = {"schema": "nr-aux-artifacts-v1", "compiler_sha256": compiler_sha,
                "headers_sha256": headers, "artifacts": records, "count": len(records),
                "codec_artifacts_included": False, "game_verified": False}
    if args.cache_from:
        manifest["cache_reuse"] = {"rebuilt_compiler_sha256": rebuilt, "original_receipts_preserved": True}
    (output / "aux-artifacts.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Embedded {len(records)} fixed auxiliary shaders; codec artifacts excluded", flush=True)

if __name__ == "__main__":
    main()
