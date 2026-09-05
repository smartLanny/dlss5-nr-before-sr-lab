"""Keep the HD2 experiment distinguishable in every current release surface."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
IDENTITY = "beta0.3.8-hd2-exp1"
CHINESE_ADDON = (
    "DLSS5-AI渲染超分版-beta0.3.8-hd2-exp1-"
    "@野生的装机宅-Bilibili.addon64"
)
ENGLISH_ADDON = (
    "DLSS5-AI-Rendering-Upscaling-beta0.3.8-hd2-exp1-"
    "@野生的装机宅-Bilibili.en.addon64"
)


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8-sig")


required = {
    "src/nr_before_sr.cpp": [
        f'kAddonVersion = "{IDENTITY}"',
        f'kBuildIdentity = "{IDENTITY}"',
        IDENTITY,
        "compatibility unverified",
    ],
    "src/nr_before_sr.rc": [
        "FILEVERSION 0,3,8,1",
        "PRODUCTVERSION 0,3,8,1",
        'VALUE "FileVersion", "0.3.8.1 beta HD2 experiment\\0"',
        'VALUE "ProductVersion", "0.3.8.1 beta HD2 experiment\\0"',
        CHINESE_ADDON,
        ENGLISH_ADDON,
        "compatibility unverified",
    ],
    "scripts/build.ps1": [CHINESE_ADDON, ENGLISH_ADDON],
    "scripts/package-beta03.ps1": [
        f"$Version = '{IDENTITY}'",
        CHINESE_ADDON,
        ENGLISH_ADDON,
    ],
    "scripts/collect-feedback.ps1": [
        f"collector_version = '{IDENTITY}'"
    ],
    "config/nr_before_sr.ini": [IDENTITY, "compatibility unverified"],
    "docs/INSTALL.zh-CN.md": [IDENTITY, CHINESE_ADDON, "兼容性"],
    "docs/INSTALL.en.md": [IDENTITY, ENGLISH_ADDON, "compatibility"],
    "docs/INSTALL.zh-CN.txt": [IDENTITY, "兼容"],
    "docs/INSTALL.en.txt": [IDENTITY, "compatibility"],
    "docs/UPDATE.zh-CN.txt": [IDENTITY, "实验"],
    "docs/UPDATE.en.txt": [IDENTITY, "experiment"],
    "docs/HD2_BRIDGE_FEASIBILITY.md": [IDENTITY, "compatibility unverified"],
}

failures: list[str] = []
for relative, needles in required.items():
    text = read(relative)
    for needle in needles:
        if needle not in text:
            failures.append(f"{relative}: missing {needle!r}")
    if re.search(
        r"beta0\.3\.7-hd2-exp[123]|HD2-Feature11-exp[23]|"
        r"Feature ?11 (?:observer )?experiment exp[23]",
        text,
        re.I,
    ):
        failures.append(f"{relative}: stale exp1/exp2 identity remains")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    raise SystemExit(1)

print(
    "HD2 release identity PASS: addon/build/banner/resources/files/packages/"
    "collector/current docs all identify beta0.3.8-hd2-exp1."
)
