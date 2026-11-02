# Settings UI refresh — 2026-11-03

## Purpose and source

The everyday Picture page previously mixed final composition, workload, global
network selection and up to five full model forms. This change separates those
tasks while retaining the shared pending-config transaction and capture hold.

Base: PR #346, `dc47485e184930d278eb9e2fd82f36e1c0906fb9`. The dependency already
contains per-layer network selection; this change does not implement a second
network path or add a rendering pass. It is intended to follow #346 into main.

## User-visible changes

| Page | Purpose |
| --- | --- |
| 画面 / Picture | NR switch, AI colour, final effect strength, processing order, working scale and layer count |
| 模型 / Models | Global network, per-layer network and model controls |
| 光影 / Lighting | Final brightening, darkening and optional light methods |
| 实验 / Experiments | Existing opt-in image comparisons and compatibility experiments |
| 开发者 / Developer | Existing input mechanisms and advanced controls |
| 反馈 / Feedback | Existing report, image capture, timing and runtime details |

The version and recent activity remain above the scrolling pages. A wrapping
summary shows the current process configuration: layer count, AI colour request
and final enhancement. The tooltip distinguishes requests from visible output
evidence and successful INI persistence; capture can temporarily use comparison
settings. Changing tabs does not create a separate config transaction.

“当前层全部跟随全局 / Active layers follow global” is a network-only action. It
clears overrides for active layers, including retained invalid IDs. It preserves
the global selection, inactive layer settings, all model strengths, work size and
composition settings. Disabled layers keep their independent requests when later
re-enabled. The button is disabled when all active layers already inherit. Its
discrete edit uses the existing generation check, capture hold and apply path.

No defaults, INI keys, model evaluation, shaders, resource lifetime or NGX
admission rules change. The tab IDs for existing pages and control IDs remain
stable. Models is a sixth top-level tab under the existing scrollable tab bar;
there are no nested tab bars or new collapsed parameter sections.

## Validation performed on this source

Linux host compiler: `/usr/bin/g++`, C++20 with the existing `-Wall -Wextra -Werror`
recorder settings. The checks do not execute a game or draw a real ImGui window.

| Command | Result |
| --- | --- |
| `python3 tests/test_d13_ui_compile.py` | PASS, actual production layer controls and overlay commit footer compiled/executed |
| `python3 tests/test_unified_ui.py` | 6 PASS; 1 existing complete-export check SKIP without `--generated` |
| `python3 tests/test_ui_refresh.py` | 10 PASS |
| `CXX=/usr/bin/g++ python3 tests/test_working_scale_ui.py` | PASS, actual working-scale widget and overlay footer |
| `python3 scripts/validate-picture-budget.py --compiler /usr/bin/g++` | 170,254 checks, 0 failures; updated capture-lock/page contract PASS |
| `python3 scripts/validate-recommended-picture.py --compiler /usr/bin/g++` | 1,296 checks, 0 failures; reset transaction wiring PASS |
| `python3 tests/test_network_settings.py` | 2 PASS |
| `git diff --check` | PASS |

The UI recorder now executes the network-only button in both languages, checks
that all inactive/model/composition values survive, checks no-op and capture-lock
behaviour, and verifies publication through the production overlay footer. Its
disabled-scope recorder tracks nested `BeginDisabled(false)` correctly instead
of accidentally removing an outer disabled scope.

`validate-043r2.py --compiler /usr/bin/g++` reaches a pre-existing strict GCC
failure in `src/nr_feedback_text.h` line 356 (`-Werror=misleading-indentation`).
This branch leaves that feedback parser unchanged. Its layout assertions were
updated for the independent Models page; the entire command is **not** claimed
as passed. The initial working-scale invocation without `CXX` exited with a
compiler-selection message; the explicit-compiler command above then passed.

## Remaining verification

Windows canonical build and rendered ReShade UI need to confirm both language
fonts, tab scrolling at narrow widths, large font scaling, capture hold, and
switching pages during a drag. This source has no fresh Windows, SM5, WARP, NGX,
game image-quality or frame-time acceptance. It does not claim a rendering
performance gain; its scope is the settings workflow.
