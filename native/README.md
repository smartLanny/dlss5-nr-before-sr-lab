# Quickbar native add-on (experimental)

Uses the public ReShade 6.8.0/API 20 + ImGui 1.92.5 `reshade_overlay` callback.
F10 toggles our panel without opening the main ReShade window. Home still opens
the original window. F6/F8 remain owned by the NR core. It does NOT remove ReShade,
hide its copyright or suppress its startup/error UI. No second Present hook,
Electron pixel streaming, localhost server or Xbox SDK dependency is introduced.

Build on Windows (Visual Studio 2022 C++ tools and CMake):

```powershell
cmake -S native -B build/quickbar -A x64 -DRESHADE_SOURCE=D:/src/reshade
cmake --build build/quickbar --config Release
ctest --test-dir build/quickbar -C Release --output-on-failure
```

The ReShade checkout MUST be v6.8.0 with its ImGui submodule initialized. Ship the
relevant ReShade and Dear ImGui licenses. The runtime DLL is not bundled here.

## NR control

Requires the accompanying `NRUI_GetStateV1`/`NRUI_SetValueV1` core build. Old cores
are **read-only/unavailable**, not controlled by private ImGui pointer tricks or
concurrent INI writes. Keep the core's original page as recovery. The opt-in core
wrapper is in `core-integration/`. Only six setting keys are exported; core
validation, persistence, F8 capture exclusion and generation checks remain in
force. No shaders or rendering-hook code is replaced by this UI.

## Metrics

- FPS and curves: QPC timing of **ReShade callbacks**, independent ring per runtime.
  Not displayed FPS, not guaranteed render FPS, and not MFG output FPS. No fake
  1% low, latency or NR GPU time is shown.
- Long gaps (>10 s), focus loss and runtime destruction reset the sample window.
- Output size is the real swapchain buffer size, not guessed DLSS input size.
- GPU utilization, power, temperature and used memory: documented NVML queries
  from a background thread (~1 Hz), loading ONLY System32/nvml.dll. Unsupported or
  stale values are --. Ampere/newer power is usually a driver averaged reading.
- Device-wide sensors, not process-specific. With multiple NVIDIA GPUs the user
  must choose a sensor device; NVML indices are never assumed to match DXGI.
- Fixed OSD is NoInputs/NoNav; interactive panel requests ReShade input blocking
  for subsequent frames. This blocks input, not game simulation/time.

The glass appearance is alpha-blended dark material with rounded borders. It is
NOT an actual blurred game capture. No screenshot/readback/blur pass is added.

Windows build, real-game input, HDR, font/DPI, hotkey collisions, device-loss and
NVML shutdown behavior require the release test matrix in docs/VALIDATION.md.
