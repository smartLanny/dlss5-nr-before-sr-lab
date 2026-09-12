# AMD/NVIDIA optical-input experiment — 100% / 50%

Status: optional **guide-only source prototype**, not yet connected to D14 NR.
Windows addon/HLSL/FX compilation and actual OF/GPU/game execution remain unverified.
The folder and addon identities are retained from PR #250; no second Feeder is built.

## Scope and defaults

- Independent game-side module: experiment OFF; AMD selected; OF input 100%.
- Only two OF scales: **100% and 50% of source width and height**. Invalid values
  are refused. No automatic scale/backend changes, 75%/25% or quality caps.
- Source Color and Generic Depth capture, full-size motion output, NR work scale
  and existing D14 processing are unchanged. This prototype does not feed NR.
- At 50%, each source dimension is halved and rounded UP. Actual integer extents
  determine motion conversion; 1921x1081 becomes 961x541, not an assumed exact 2x.
- Both backends use area-overlap low-pass preparation of the OF copy. AMD 100%
  still reads the original private snapshot without an additional resize pass.
  NVIDIA keeps its same-size luminance preparation at 100%.
- AMD's internal pyramid/8x8 block vectors and NVIDIA's queried output grid are
  independent of input scale. Expanding block vectors does not invent fine detail.
- Synthetic pixel motion uses actual source/OF width and height ratios; NVOF's
  S10.5 factor is also applied. Native normalized-UV motion is not rescaled here.
- Scale/backend changes retire the prior session through existing exact fences,
  create a new one and reset history. No busy descriptor/texture is resized in place.

## Components

`backend.cpp/.hpp` call real official FFX/NVOF APIs on a private same-device D3D12
queue. `geometry.hpp` provides extents and root constants. `shader_source.hpp`
contains project-owned area-filter and pixel-unit conversion shaders. `addon.cpp`
connects capture, explicit scale selection, completion and preview callbacks.
`NR_FullResGuides.fx` reads existing ReShade DEPTH; no alternative depth detector.

Source/reference IDs and capture age label the last completed preview. It can lag
current rendering and MUST NOT be used as a current-frame NR guide. Known Generic
Depth binding/size is not proof of correct scene, projection, orientation or lifetime.
The eventual consumer must receive a matching immutable source, depth and motion
pair, not an old preview attached to a new frame.

The prototype requires x64 D3D12, explicitly confirmed SDR, Core NR OFF and FG OFF
for guide-only capture tests. It does not automatically prove those settings.
Own source planes remain limited to 16,777,216 pixels; SDK allocations are additional.
Selected AMD input below 128 in either dimension is refused, not silently enlarged.
Faulted/unknown GPU work is quarantined until exit; do not hot-unload or repeatedly
reinitialize a failed session. The existing module pin and no-reuse rules remain.

## Without Feeder

No production Core source, import table, installer, runtime owner or native-route
hook is changed. This companion does not import a Feeder DLL and remains inactive
until explicitly enabled. Missing SDK/driver/capture support reports an unavailable
experiment and asks for a BUG report; it is not permission to modify the original
image. This does not claim end-to-end NR support without Feeder on other APIs.

Reuse the existing Feeder capture/host/transport when connecting to the current
Core. Forward-port the minimal approved consumer seam; do not replace D14 with an
older dedicated Core or grow this probe into another universal bridge. Manager's
canonical public repository is `smartLanny/DLSS5-Manager-ZJZ`; installation/DLC
changes go there, not into the historical Manager copies in this repository.

## Portable validation

```sh
cmake -S experiments/fullres_guides -B build/of-policy
cmake --build build/of-policy
ctest --test-dir build/of-policy --output-on-failure
python experiments/fullres_guides/test_mutations.py
python experiments/fullres_guides/test_geometry_mutations.py
```

This compiles production policy/geometry headers and CPU reference checks, NOT
Windows, vendor libraries, HLSL or FX. The area-filter CPU oracle checks footprints
and constant preservation; it is not GPU pixel-readback validation.

## Windows build and acceptance still required

Use `NR_OF_BUILD_ADDON=ON` in an isolated MSVC x64 build. Supply official FidelityFX
SDK v1.1.4 (OF1.1.2), its actually compiled DX12/OF dependencies via `FFX_LIBRARIES`,
`FFX_ROOT`, official `NVOF_INCLUDE`, and matching `RESHADE_INCLUDE`/`IMGUI_INCLUDE`.
No dependency is downloaded or stubbed by this build. NVOF is found dynamically in
System32; no NVIDIA runtime/header or third-party integration source is bundled.

Before connecting to NR, verify Windows/FX compilation and then actual immutable
GPU pairs: stationary, horizontal/vertical/diagonal motion, scene cut, odd extents;
100% and 50% for both vendors. Measure vectors against source-pixel translations,
check source preservation, and separately inspect thin objects and disocclusions.
Test scale/backend switches, resize, effects reload, capture-before-preview order,
multiple runtimes, failed Signal and delayed completion. AMD SCD-to-Core reset,
NVOF cost/confidence and final same-frame NR writeback are still outstanding.

A bypass must visibly request ReShade.log and Core F8 (where applicable), naming
backend, requested/actual OF size and source commit. No speedup, quality ranking,
DX9/DX11/Vulkan compatibility or finished NR fallback is claimed by this prototype.
