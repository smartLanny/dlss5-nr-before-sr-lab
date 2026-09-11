# Full-resolution optical-input experiment — D14 companion

**Status: source prototype. The portable policy tests have run; the Windows addon,
FX, AMD/NVIDIA execution and games have NOT been compiled/run in this delivery.**
This is an optional ReShade input-probe addon, not a replacement Core and not a
finished synthetic-NR route. It does not call Feature18, SR or FG. Do not ship it
in Manager/default packages or advertise new game compatibility.

Parent: `49b0deb0d09b0a442784e0ebf1073f9498eaf157`, owner-selected
`codex/050-corefix8-integration-20260911`. Existing D14 source is unchanged.
The previously demonstrated #175 external-provider API is absent from that tree;
this experiment deliberately does not call a nonexistent export or downgrade D14.

## Actual implementation

- `backend.cpp`: real official `ffxOpticalflowContextCreate/Dispatch` and
  `nvOFInit/nvOFExecuteD3D12` call paths, one in-flight sample, exact source size.
  AMD is the selected default; NVIDIA must be explicitly selected. No backend is
  silently substituted when the requested one fails.
- `addon.cpp`: actual ReShade post-technique callback, private color copy, known
  immediate-list/queue dependency, completed motion copy-back, AMD/NVIDIA selector,
  raw Generic Depth binding observation and preview controls. This is a separate
  experimental panel, not yet inside the D14 Core menu.
- `NR_FullResGuides.fx`: full-size SDR capture, raw `DEPTH` semantic capture and
  motion/depth previews. Place capture before preview in the effect order.
- `policy.hpp`: native-first selection contract and accepted-reference bookkeeping.
  History is used by the backend. Native selection is portable-tested only; it is
  NOT wired into D14 native evaluation. Enabling this independent probe is an
  explicit synthetic comparison even when the existing Core has native guides.
- Native Core operation, NR work scale, second NR pass, original SR and FG are not
  changed. No SDK/model/runtime binaries or third-party integration code are copied.

## What “full resolution” means

For captured `S=(W,H)`, both backends receive exactly `(W,H)`. There is no 180p cap,
640/960/1280 long-edge cap, sampling ratio, budget-triggered downscale or coupling to
D14's NR work size `N`. Full-size luminance/color-format conversion is allowed.
AMD's own seven-level pyramid and 8x8 block vectors remain its algorithm. NVIDIA's
queried 1/2/4 output grid remains a vector grid, not an input resize. A nearest-block
expansion is intentionally used for a transparent first diagnostic; it does not
invent per-pixel precision. NV flow uses S10.5 / 32, AMD block flow uses pixel units.
The movement-sign interpretation still requires the GPU translation oracle below.

This first probe accepts only x64 D3D12 and explicitly confirmed SDR RGBA8 input.
For this guide-only capture test, disable Core NR and game/driver FG so captures
do not consist of already-neural or generated frames. The UI requires this explicit
confirmation; it does not claim automatic detection of those settings.
It has a 16,777,216-source-pixel budget for its own planes; SDK memory is additional.
Unsupported or over-budget sizes are refused, never resized. AMD sources smaller
than 128 in either dimension are refused. These are conservative prototype limits,
not claims about universal SDK minimum dimensions.

NVOF uses grayscale8 at the same size, the smallest advertised supported grid, SLOW
effort, forward prediction and disabled temporal hints for the initial comparison.
The prototype does not export NVIDIA cost/confidence, nor an AMD SCD-to-NR reset
receipt. It must NOT be connected to a neural history until these semantics and
first-frame/cut behavior are verified. It does not manufacture a confidence score.

## Ownership and failure behavior

Capture is recorded on the known ReShade immediate list and flushed before its
real queue signal. The private same-device queue waits that published signal;
it never accesses an unowned game texture. The signal also covers copying the
previous completed flow into the effect texture. Only private snapshots go to OF.
One in-flight sample prevents allocator/descriptor/SDK-history mutation while busy.
Skipped captures do not advance the reference; the next pair uses the previous
accepted source, including across sequence gaps.

Only a non-removed exact fence completion permits preview/retirement. Failed
signals, opaque SDK failure and unproven teardown retain the session and its
resources until process exit instead of releasing live GPU dependencies. Registration
wait failure also retains a registered event. The addon module is pinned on first
activation. This is a conservative experimental quarantine, not a general memory
recovery implementation; restart the game after a latched failure. Never hot-unload.

Preview is the last completed pair and may lag current rendering; source IDs and
capture age are shown. It is NOT a same-frame enhanced result. Generic Depth's
binding/size is observed, but correct scene selection, reversed/logarithmic depth,
projection, subrect, MSAA and frame identity are not certified. A missing binding
is not replaced with fake depth. Raw depth preview is not a reconstruction oracle.

## Portable tests (no GPU)

```sh
cmake -S experiments/fullres_guides -B build/fullres-policy
cmake --build build/fullres-policy
ctest --test-dir build/fullres-policy --output-on-failure
```

`test_policy.cpp` executes the actual header: defaults, native priority/provenance,
explicit forced diagnostics, exact requested backend, 4K/non-aligned grid arithmetic,
removed-device sentinel, duplicate/out-of-order frames, failed submissions, skipped
captures and backend/stream/extent/reset transitions. Numbers are assertions, not
games. A CMake build with the addon option OFF compiles no Windows/SDK code.

## Windows build inputs (not executed in this delivery)

Use a fresh isolated build directory; never replace an installed game/Core.
Obtain approved official FidelityFX SDK **v1.1.4 / OF 1.1.2**, compile its DX12 backend
and optical-flow component plus required shader-blob dependencies using its own
build instructions. Supply the actual static library paths, not guessed filenames.
Use official NVOF headers and ReShade/ImGui headers matching the runtime under test.
NVOF is dynamically loaded from Windows System32, not bundled from an arbitrary
working-directory DLL. No downloads, privilege changes or automatic installs occur.

```powershell
cmake -S experiments/fullres_guides -B build/fullres-win -A x64 `
  -DNR_OF_BUILD_ADDON=ON `
  -DFFX_ROOT="<official SDK checkout>" `
  -DFFX_LIBRARIES="<actual OF library>;<actual DX12 backend library>;<other required SDK libraries>" `
  -DNVOF_INCLUDE="<official SDK NvOFInterface>" `
  -DRESHADE_INCLUDE="<matching ReShade include>" `
  -DIMGUI_INCLUDE="<matching ImGui include>"
cmake --build build/fullres-win --config Release
```

A successful configure is not a build or execution result. Missing dependencies
stop configure; no mock implementation or downgraded shader is substituted.
The first Windows owner task is full compile/link and HLSL/FX compilation, then
review actual ReShade after-technique resource states and queue identity. Fix
integration mismatches on this isolated branch; do not weaken lifetime checks.

## Required acceptance before connecting to NR

1. GPU host oracle on fixed immutable full-size pairs: stationary image; +16px X,
   -16px Y and diagonal translations; scene cut; non-8-aligned size. Inspect interior
   vectors and expected **current -> reference** sign/magnitude independently for
   AMD and NVIDIA. First/reset outputs must be explicitly identified, not counted as
   successful temporal matches. Compare sampled pixels of the captured source to
   the same source before OF. Never call a finite vector field alone “quality passed”.
2. Real ReShade host with exact SDK/runtime versions: capture -> full-size provider
   -> completed preview. Verify capture-before-preview order, effects reload,
   preview toggles, session/backend switching, resize and multi-runtime concurrency.
   Run debug-layer/DRED checks. Confirm no resource-state assumptions leaked from
   a test stub. Record actual source/module hashes and every failure.
3. Deliberate GPU stall / Signal failure / device removal / close failure. Ensure
   no half-submitted resource reuse, no retired callback pointer and no unbounded
   repeated allocations. Original Core and native SR must continue unchanged.
4. A single authorized offline SDR D3D12 game, then the native-input-vs-OF comparison
   after D14 integration. Do not call this addon on DX11/DX9/Vulkan as if transport
   were already implemented. Do not run on protected/anti-cheat games.
5. Port the existing project external-consumer seam to D14 as a narrow reviewed
   increment: unique NR owner, native route wins by validated frame/domain, explicit
   Generic Depth semantics, scene-cut/reset metadata, same-frame completion and
   D14 dual-pass/NR-SR regression. Only then can the experiment be called an NR
   fallback switch. Do not create a second SR just to trigger an already owned NR.

At every bypass, collect ReShade.log and a short report naming source commit,
backend, S/N/O, API, driver/GPU and current failure. D14 F8 remains the Core report;
this prototype does **not** add fields to F8. Keep private paths/raw captures out
of the source repository. No game visual repair, speedup or perfect compatibility
is claimed by this source prototype.
