#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FILES = [
    ROOT / "src" / "nr_before_sr.cpp",
    ROOT / "src" / "nr_before_sr.rc",
    ROOT / "scripts" / "build.ps1",
    ROOT / "scripts" / "validate-beta04-structure.py",
    ROOT / "config" / "nr_before_sr.ini",
    ROOT / "docs" / "BETA_0_4_PERFECT_CHAIN.md",
    ROOT / "docs" / "BETA_0_4_TEST_MATRIX.md",
]

REPLACEMENTS = (
    ("beta0.4-perfect-chain-r3", "beta0.4-perfect-chain-r4"),
    ("beta0.4 perfect-chain r3", "beta0.4 perfect-chain r4"),
    ("beta0.4 Perfect Chain r3", "beta0.4 Perfect Chain r4"),
    ("0,4,0,3", "0,4,0,4"),
    ("0.4.0.3 perfect chain r3", "0.4.0.4 perfect chain r4"),
)

DOC_APPEND = """

## r4 native command-state safety integration

- The beta0.3.3 RE9-tested native D3D12 command-state envelope is now integrated around both the pre-SR and post-SR experimental work.
- Descriptor heaps, compute/graphics root signatures, root arguments and PSO/state-object bindings are shadowed from native setter calls and restored before control returns to the game.
- A command list with no proven Reset/ClearState baseline fails closed to native SR instead of guessing state.
- Native Reset success, ReShade reset/destroy events and command-list lifetime are tied to the same state tracker.
- CI now executes the source-extracted CPU/stack harness, a real D3D12 WARP restoration oracle, and a deliberately corrupted negative control before building either language DLL.

This reduces the state-interference risk that caused the RE9 investigation. It does not replace live RE9, Cyberpunk, Witcher, Yanyun and HDR transition testing.
"""

TEST_APPEND = """

## r4 命令状态专项

- 《生化危机 9》：启动、进菜单、读档、切场景、退出，确认不再出现设备移除或进入游戏即崩溃。
- 在日志中确认已建立 command-state baseline；若状态无法证明，应显示 `command-state-unverified` 并保留原生 SR，而不是继续写入。
- 连续切换 F6、DLSS 档位、HDR 与全屏模式，确认游戏原有 Root Signature、Descriptor Heap、Root 参数和 PSO 没有被插件遗留状态污染。
- 若某游戏仅在 Reference 正常而缩放档异常，同时保留日志、显示截图和一次阶段采集，优先排查 Guide/工作分辨率合同，而不是关闭状态保护。
"""


def main() -> None:
    changed_files: list[str] = []
    for path in FILES:
        text = path.read_text(encoding="utf-8")
        original = text
        for old, new in REPLACEMENTS:
            text = text.replace(old, new)
        if path.name == "BETA_0_4_PERFECT_CHAIN.md" and \
                "## r4 native command-state safety integration" not in text:
            text = text.rstrip() + DOC_APPEND.rstrip() + "\n"
        if path.name == "BETA_0_4_TEST_MATRIX.md" and \
                "## r4 命令状态专项" not in text:
            text = text.rstrip() + TEST_APPEND.rstrip() + "\n"
        if text != original:
            path.write_text(text, encoding="utf-8", newline="\n")
            changed_files.append(str(path.relative_to(ROOT)))

    source = (ROOT / "src" / "nr_before_sr.cpp").read_text(encoding="utf-8")
    resource = (ROOT / "src" / "nr_before_sr.rc").read_text(encoding="utf-8")
    validator = (ROOT / "scripts" / "validate-beta04-structure.py").read_text(encoding="utf-8")
    required = (
        'kAddonVersion = "beta0.4-perfect-chain-r4"',
        'kBuildIdentity = "beta0.4-perfect-chain-r4"',
        '#include "nr_command_state.inl"',
        'FILEVERSION 0,4,0,4',
        'PRODUCTVERSION 0,4,0,4',
        'beta0.4-perfect-chain-r4',
    )
    combined = source + "\n" + resource + "\n" + validator
    missing = [item for item in required if item not in combined]
    if missing:
        raise SystemExit("r4 finalization markers missing: " + ", ".join(missing))
    print("finalized beta0.4 perfect-chain r4:", ", ".join(changed_files))


if __name__ == "__main__":
    main()
