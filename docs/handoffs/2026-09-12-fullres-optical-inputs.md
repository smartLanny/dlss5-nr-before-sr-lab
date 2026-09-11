# D14 full-resolution guide experiment handoff

## What is delivered

Owner baseline `49b0deb0d09b0a442784e0ebf1073f9498eaf157` on
`codex/050-corefix8-integration-20260911`.
Independent branch `codex/050-fullres-optical-inputs-20260912`. Refs #239/#164.

Only new source/test/docs under `experiments/fullres_guides`, `docs/research` and
this handoff; no existing Core, Manager, generator, source pin, workflow or payload
is changed. No DXL/Magpie code or third-party runtime is copied.

Actual code: official FFX/NVOF API calls; full-size D3D12 source copy and fenced
private processing; last-completed flow visualization; Generic Depth raw binding
probe; default-off/AMD-selected UI. It is a companion **guide-only** probe, not a
completed D14 NR input switch. Native-first policy is portable-tested but not wired
into D14; existing native processing remains unchanged.

## Executed validation in this delivery

- Linux CMake Release build and CTest: **1/1 test executable passed**, containing
  **169 assertions** against the real production policy header.
- Clang C++17 `-Wall -Wextra -Werror -pedantic`, ASan + UBSan: same169 assertions
  passed, no sanitizer report.
- Four deliberate policy mutations compiled and failed their **specific expected
  assertion**: native priority, removed-device sentinel, unsubmitted history, AMD
  default. Compilation failure or arbitrary process failure is not accepted.
- Source scope/API/lifetime review only for the Windows backend and addon.

Windows/MSVC DLL compile: **not run**. HLSL/ReShade FX compile: **not run**.
WARP: **not run**. AMD/NVOF hardware: **not run**. NVIDIA NR: **not run**.
Real games, full-size image quality, temporal stability, latency/VRAM/performance,
Core dual-pass regression and packaged UI: **not run**. Hosted CI not claimed.
The ROG execution namespace was not available despite discovery attempts; no ROG
workspace, installed Manager, game directory or driver was modified.

## Next exact integration step

Read `experiments/fullres_guides/README.md` and research report. Resolve official
SDK inputs and compile the actual Windows code before any game. Verify signed
translation pairs and real ReShade capture/preview/resource states. The guide-only
probe explicitly requires SDR, Core NR OFF and FG OFF during this test. Keep input
extent unchanged; unsupported caps/extent should fail, not silently downsample.

D14 has no `src/interop/nr_external_provider_abi.h` or `NRExternalProvider` exports.
Forward-port only the needed project-owned consumer seam from #175 into current
D14 context/lifetime/dual-pass code; never overlay the old branch. Then connect
current source/depth/motion pair with one NR owner, reset on provenance changes,
and verify same-frame writeback before moving the selector into the real Core UI.
The guide probe has no independent SR, duplicate NR, FG or presenter to remove.

Only after this exact D3D12 composition path is accepted should it be connected to
existing D3D11 transport, Vulkan imports and x86/legacy hosts. Do not remove Manager
transport recipes prematurely. Keep #239 open until its actual NR/game acceptance
is fulfilled; this experimental PR is a partial implementation, not completion.
