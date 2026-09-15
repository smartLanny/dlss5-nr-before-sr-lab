# D21 uniform2 implementation and owner test batch

This is a staged successor to `cbfb33919696a2840ed5154c91abaea56228b661`
(`beta0.5-dline21-223fix2`). It does not mark the complete A-E roadmap done.
Candidate identity: `beta0.5-dline21-uniform2`, PE version `0.5.1.25`.
The uniform1 source, binaries and configuration remain separate rollback evidence.

## Implemented in this batch

- Every enabled NR layer uses the primary working raster. The primary route
  applies the work scale once: pre uses native SR input, post uses reconstructed
  output, Present uses its admitted capture. Ordinary low-resolution pre NR
  still spatially restores the final edit before native SR. No private SR is
  enabled by increasing layer count.
- Five independent model settings and Create signatures, nested enable controls,
  retained hidden descendants, per-layer reset and release-to-apply editing.
  Existing active legacy layers inherit the old primary model parameters.
  Legacy auxiliary ratios remain archived in INI, never drive the new chain.
  A changed-work migration is marked custom and reported in UI/F8.
- Native auxiliary epochs propagate model changes downstream. Present compares
  every layer signature and rebuilds safely after draining. Present currently
  retires the entire context on a layer edit; selective retirement remains open.
- Same-raster auxiliary inputs borrow the previous complete output; unused
  downsample/FP32 intermediate residual resources are absent. Each auxiliary
  still owns its output and each model retains independent history. Full
  ping-pong storage reuse and all-in-flight VRAM budget admission remain open.
- Color budget follows the actual composed shader in native and Present paths,
  including fallback to the gain-coupled shader. Existing protection remains.
- Reference white UI exposes the reciprocal of the existing ProxyScale once.
  It is relative model HDR input brightness, not a display brightness control;
  SDR paths bypassing HDR mapping do not consume it.
- Source-scoped conservative power saving defaults on; frame-ID heuristic off.
  Existing native SR/FG execution is retained. Resume resets history before
  admitting NR. Diagnostics count gate admission checks, not unique frames.
- PR263 typed-view lifetime evidence is isolated from Core-created descriptors;
  retiring one Feature does not invalidate other live Feature generations.
- Explicit bridge reuse requires matching source, SDK pins, license and binary
  hashes. Build receipts distinguish reuse from compilation.

## Compatibility continuation (uniform2)

- A current application color surface can be admitted without a native guide
  snapshot when its resource lifetime, exact submitted write, active source,
  successful native result, current configuration and actual Present queue are
  all proven. Admission and first GPU recording both recheck ownership. Stale,
  generated, FG-consumed, processed, failed and cross-queue color-only sources
  remain rejected. A rejected existing guide snapshot cannot bypass its failure
  through this path. This does not yet admit every game without a native source.
- The fallback owns an R32 depth texture initialized to 1 and an RG16 motion
  texture initialized to 0. GPU initialization and transitions precede model
  creation. The textures share the uniform work grid across all layers and
  retire with the exact private generation. They are explicitly marked as
  placeholders, jitter stays unknown and every model Evaluate resets history.
  Model computation and actual adopted output are still required for success.
- UI/F8 distinguish placeholder inputs and per-frame reset from native guides.
  The same-size chain allocates primary Bootstrap descriptor heaps only for
  the first layer; later layers already use their parent's full output. At five
  layers this removes eight unused heaps (48 descriptors), without claiming
  measured frame-time or model-history savings.
- The real NVIDIA host now uses the actual FaceBackend build receipt and permits
  production cold bootstrap without an unused local MSVC/SDK requirement. It
  checks actual Create/Evaluate parameter blocks, independent per-layer tuning,
  uniform dimensions, complete parent output, per-frame Reset, GPU readback of
  placeholder values, adoption, fence completion and F6 no-work admission.
- A development snapshot passed nine controlled color-only NVIDIA scenarios:
  layers 1-5 at 100%, layers 2/3/5 at 50%, layer 4 at 75%. This is synthetic
  640x360 SDR input with the signed 310.8.0.0 runtime, not game or FG validation.
  Final frozen-source build and hardware receipts are recorded separately.
- Generator PE metadata now matches the actual resource version. Earlier
  uniform1 generator metadata incorrectly named 0.5.1.23 while its PE was
  0.5.1.24; old evidence is retained rather than rewritten.

## Validation boundaries

CPU fixtures execute actual configuration, Create-signature, color-consumer,
pause admission and UI commit code. WARP executes actual spatial kernels with
an explicit shader model substitute; it does not run NVIDIA NR. Face tests
cover the real fixed ORT model/worker and WARP tracker separately. Windows
Core build, hosted CI and game evidence must each have their own status in the
private delivery manifest. A new reusable CI job is called by the existing
single PR workflow for the D21 base/successor branches. It is not a release job.

## Still required by the agreed roadmap

| Batch | Remaining before acceptance |
| --- | --- |
| A | Matched D13 OFF / D21 OFF / candidate OFF traces; measured CPU improvement; GPU power test; memory switching/retirement measurements. |
| B | Present without any native source identity, per-guide native/ReShade/OF selection, broader format conversion, saturation controls and texture-stage comparisons. Missing-guide sources with proven current native color lineage now have a marked placeholder path. |
| C | Selective Present model retirement, proven scratch aliasing, budget rejection preserving prior runnable configuration, full resize/rapid-edit/FG game regression. |
| D | AMDOF accumulation/+ and low-frequency reconstruction; private SR backend, post low-res route, #259 and #258; actual FP16 residual and model tests. No new controls claiming those features are exposed. |
| E | Measured two/three-layer presets, final sharpening, Manager bridge recipes/conflict handling, complete installer/rollback, XeSS MFG feasibility and FG coexistence. |

Do not close #250/#256/#258/#259/#263/#276/#278 merely because parts have
been extracted. #214/#257/#265 still require traceable coverage checks. No
stable merge, tag, release or game installation is part of this batch.

## First game test: instructions for the owner

1. Exit the game. Preserve the entire installed D21 Core set and its INI in a
   separate backup, and record the old build identity. Keep the Manager/runtime
   and native DLSS DLL versions fixed. Use only one language edition and one
   active NR Core. Follow the delivery manifest for dependency files.
2. Start with **one layer, 100%, sharpening off**, conservative saving on and
   heuristic off. Verify pre-SR 4K Performance reports base/model 1920x1080.
   Compare SDR color follow at extra gain 1x/2x/4x. Capture model input, raw
   NR output and final composition with F7; F8 records actual route and settings.
3. Enable layers 2 through 5 in order. Change only layer 4 intensity/style and
   confirm layers 1-3 settings remain unchanged. Close layer 2 and reopen it:
   the hidden downstream settings must return. Collapse headers separately.
4. Compare the same layer count at 100/75/50%. NR settings, scene, display
   resolution and FG settings must be identical. Do not compare the old mixed
   raster chain as if its model work were equal. Record visible detail, NR edit
   amount, flicker and trails separately. Keep sharpening off for this phase.
5. NTE: fixed face/turning face and dark scene. Cyberpunk 2077: fixed RR/FG/HDR
   settings, reflections, fence/vegetation, disocclusion, scene cuts and HUD.
6. For each OFF build, warm the same scene, then record about 20 seconds of CPU
   sampling and frame times; repeat three times. Provide median/P95/P99 and raw
   captures. Test saving by opening/closing a pause menu and watching NR GPU
   time, power and resume behavior. Heuristic mode is a separate second test.
7. Cycle layer count, work scale, model parameters and pre/post route. Record
   steady VRAM, peak during change and settled VRAM after retirement. Include
   resize and HDR changes. On failure save F8 and restore the original Core
   set **and original INI**, keeping the failed candidate logs for analysis.

The implementation team analyzes the evidence; no extra local agent is needed
for the fixed game test. Balanced defaults remain undecided until matched
whole-frame data is available. Original image weight remains unchanged.
