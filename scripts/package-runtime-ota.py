"""Assemble Beta14 manual overlay bundles from fixed, verified inputs."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile

SOURCE_CORE_ZIP_SHA = "1f604963132f3760a28445d2e2098aaf47bd3c8bd5186c9234960873ec7c7599"
SOURCE_FACE_MANIFEST_SHA = "392a6d9e601b2cf445027405d1181fb662114e8cc1f4504d0c9b740dda5b90f7"
CORE_SHA = "a10947a9902ea3870d48945f3d08bcc0c1c5b142a11deb9c9575f9054144ec35"
CHAIN_SHA = "1acf3cbe509a031be1763a8231cd81e6019aa3532368cd0b08a6c17bc94b70a2"
LICENSE_SHA = "55299bccff4c98983c0560d2d5319ee6d4854a3652c8a567a072dde1d3593128"
RUNTIME_SHA = {
    "RTX20-30": "6dac1b40f0c87af84a8177b18c741e84fb0c914f204c9d87d95916b665ba3af8",
    "RTX40-50": "a2d16f9fba2b619559427821169918df56fc8e410a6ac87569953598a00b66fa",
}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(4 * 1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def need(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def x64_pe(data: bytes, label: str) -> None:
    need(data[:2] == b"MZ" and len(data) >= 64, "Missing PE: " + label)
    offset = struct.unpack_from("<I", data, 60)[0]
    need(offset + 26 <= len(data) and data[offset:offset + 4] == b"PE\0\0" and
         struct.unpack_from("<H", data, offset + 4)[0] == 0x8664 and
         bool(struct.unpack_from("<H", data, offset + 22)[0] & 0x2000),
         "Expected x64 DLL: " + label)


def safe_name(name: str) -> None:
    parts = name.replace("\\", "/").split("/")
    need(name and not name.startswith("/") and ":" not in name and all(part not in ("", ".", "..") for part in parts),
         "Unsafe archive path: " + name)


def main(core_zip: Path, nr_root: Path, face_root: Path, out_root: Path) -> None:
    need(file_digest(core_zip) == SOURCE_CORE_ZIP_SHA, "Beta14 Core OTA identity mismatch")
    with zipfile.ZipFile(core_zip) as z:
        need(z.testzip() is None, "Beta14 Core OTA CRC failure")
        base_manifest = json.loads(z.read("ota-manifest.json"))
        need(base_manifest["version"] == "0.5.2-beta.14" and base_manifest["language"] == "zh-CN", "Core identity mismatch")
        core_name = "nr-before-sr.0.5.2-beta.14.zh-CN.addon64"
        declared = {row["name"]: row for row in base_manifest["files"]}
        need(len(declared) == len(base_manifest["files"]) and
             set(z.namelist()) == set(declared) | {"ota-manifest.json"}, "Core OTA manifest entries mismatch")
        for name, row in declared.items():
            blob = z.read(name)
            need(len(blob) == row["bytes"] and digest(blob) == row["sha256"], "Core OTA member mismatch: " + name)
        core = z.read(core_name)
        chain = z.read("nrchain_nvngx.dll")
        licenses = z.read("LICENSES.txt")
        old_changelog = z.read("CHANGELOG.md").decode("utf-8")
        need(digest(core) == CORE_SHA and digest(chain) == CHAIN_SHA and digest(licenses) == LICENSE_SHA,
             "Core, chain or license hash mismatch")
        need(len([name for name in declared if name.endswith(".addon64")]) == 1, "Core OTA must contain one Core")
        x64_pe(core, "Beta14 Core")
        x64_pe(chain, "nrchain")
        need("本OTA只更新Core与配套nrchain，不包含NR/SR/RR/FG运行库或人脸资源" in old_changelog,
             "Expected Core-only changelog sentence missing")
        changelog = old_changelog.replace(
            "本OTA只更新Core与配套nrchain，不包含NR/SR/RR/FG运行库或人脸资源，不写INI，不自动启用实验。保留既有安装、运行库和回退包。",
            "此手动完整升级包包含中文Core、配套nrchain、对应显卡系列的NR运行库和已验证的人脸资源；不含INI及原生SR/RR/FG运行库，不自动启用实验。安装前保留已有设置与回退包。",
        )
        need(changelog != old_changelog, "Changelog update failed")

    face_manifest_path = face_root / "manifest.json"
    need(file_digest(face_manifest_path) == SOURCE_FACE_MANIFEST_SHA, "Face source manifest identity mismatch")
    face_manifest = json.loads(face_manifest_path.read_text(encoding="utf-8"))
    face_rows = face_manifest["face_assets"]
    need(len(face_rows) == 7 and len({row["path"].casefold() for row in face_rows}) == 7,
         "Expected seven unique face companion files")
    for row in face_rows:
        need(row["path"].startswith("install/nr_face/"), "Unexpected face path")
        safe_name(row["path"])
        file = face_root / row["path"]
        need(file.resolve().is_relative_to(face_root.resolve()), "Face path outside source")
        need(file.stat().st_size == row["bytes"] and file_digest(file) == row["sha256"],
             "Face source mismatch: " + row["path"])
        if file.suffix.lower() == ".dll":
            with file.open("rb") as stream:
                x64_pe(stream.read(4096), row["path"])
    for family in RUNTIME_SHA:
        runtime = nr_root / family / "nvngx_dlssnr.dll"
        family_chain = nr_root / family / "nrchain_nvngx.dll"
        need(file_digest(runtime) == RUNTIME_SHA[family], "NR runtime mismatch: " + family)
        with runtime.open("rb") as stream:
            x64_pe(stream.read(4096), "NR runtime " + family)
        need(file_digest(family_chain) == CHAIN_SHA and family_chain.read_bytes() == chain,
             "NR chain mismatch: " + family)

    for family in RUNTIME_SHA:
        directory = out_root / family
        need(not directory.exists(), "Preserve existing output: " + str(directory))

    for family in RUNTIME_SHA:
        directory = out_root / family
        directory.mkdir(parents=True)
        archive = directory / f"DLSS5-0.5.2-beta.14-{family}-OTA.zip"
        runtime = nr_root / family / "nvngx_dlssnr.dll"
        readme = (
            f"DLSS5 0.5.2 Beta 14 - {family} 手动完整升级包\n"
            "本ZIP供退出游戏后手动解压覆盖使用；当前Manager不能导入此含大体积NR运行库的包。\n"
            "先将现有Core、nrchain、NR运行库和nr_face目录成对备份到插件搜索目录外。\n"
            "选与显卡系列相符的一包，只把install目录内文件放到已确认的游戏插件目录；只留一个中文Core。\n"
            "保留现有INI、ReShade设置及游戏自带DLSS文件，不自动改变新功能或实验开关。\n"
            "本包含Core、nrchain、该系列NR运行库及人脸资源；不含原生SR/RR/FG运行库。\n"
            "Beta14新实验保持默认关闭；建议使用普通恢复。\n"
        ).encode("utf-8")
        files = [
            ("install/nr-before-sr.zh-CN.addon64", "core", core, None),
            ("install/nrchain_nvngx.dll", "core-chain", chain, None),
            ("install/nvngx_dlssnr.dll", "nr-runtime", None, runtime),
            ("LICENSES.txt", "license-notices", licenses, None),
            ("CHANGELOG.md", "changelog", changelog.encode("utf-8"), None),
            ("README.txt", "manual-instructions", readme, None),
        ]
        for row in face_rows:
            face_name = row["path"]
            role = "face-license" if Path(face_name).name.lower() in {"license", "thirdpartynotices.txt", "yunet-license"} else "face-asset"
            files.append((face_name, role, None, face_root / face_name))
        names = [name for name, _, _, _ in files] + ["manifest.json"]
        for name in names:
            safe_name(name)
        need(len({name.casefold() for name in names}) == len(names), "Duplicate archive path")
        need(len([name for name in names if name.endswith(".addon64")]) == 1 and
             not any(name.lower().endswith(".ini") or name.lower() in
                     {"install/nvngx_dlss.dll", "install/nvngx_dlssd.dll", "install/nvngx_dlssg.dll"}
                     for name in names), "Unexpected Core, INI or native runtime")
        rows = []
        for name, role, blob, path in files:
            size = len(blob) if blob is not None else path.stat().st_size
            checksum = digest(blob) if blob is not None else file_digest(path)
            rows.append({"path": name, "role": role, "bytes": size, "sha256": checksum})
        manifest = {
            "schema": "dlss5-manual-full-ota-v1",
            "version": "0.5.2-beta.14",
            "gpu_family": family,
            "language": "zh-CN",
            "installation": "manual-overlay-only",
            "manager_install_scope": "unsupported; manual overlay after game exit",
            "manager_import_supported": False,
            "core_only_ota_sha256": SOURCE_CORE_ZIP_SHA,
            "binary_source_commit": base_manifest["sourceCommit"],
            "core_sha256": CORE_SHA,
            "nrchain_sha256": CHAIN_SHA,
            "nr_runtime_sha256": RUNTIME_SHA[family],
            "nr_runtime_included": True,
            "face_assets_included": True,
            "face_source": "verified Beta13 r2 companion files, identical hashes in both GPU-family manifests",
            "ini_included": False,
            "native_sr_rr_fg_runtime_included": False,
            "game_acceptance_for_this_package": False,
            "preserve": ["nr_before_sr.ini", "ReShade.ini", "ReShadePreset.ini", "nvngx_dlss.dll", "nvngx_dlssd.dll", "nvngx_dlssg.dll"],
            "files": rows,
        }
        with zipfile.ZipFile(archive, "x", zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as z:
            for name, _, blob, path in files:
                if blob is not None:
                    z.writestr(name, blob)
                else:
                    z.write(path, arcname=name)
            z.writestr("manifest.json", (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
        print(json.dumps({"family": family, "archive": str(archive), "sha256": file_digest(archive),
                          "bytes": archive.stat().st_size, "entries": len(rows) + 1}, ensure_ascii=False), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core-ota", required=True, type=Path)
    parser.add_argument("--nr-root", required=True, type=Path)
    parser.add_argument("--face-stage", required=True, type=Path)
    parser.add_argument("--output-root", required=True, type=Path)
    args = parser.parse_args()
    main(args.core_ota, args.nr_root, args.face_stage, args.output_root)
