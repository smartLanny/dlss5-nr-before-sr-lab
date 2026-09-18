# colour4: early raw-reference and neutral-consumer test candidate

Related: #274, #281; inherits the explicit-source gate fixes from #285.
Base colour3: `3ae7249c1d7c482ffe9051244a4e12a99d508386`.
Implementation parent: `6126c9068ae04f72008d453fb21fccc8738eef25`.
Candidate identity: `beta0.5-dline21-colour4`.
This is a separate quality-line candidate, not main, a stable release, or a
performance-line update. Never overwrite an existing owner artifact.

## What changed

The colour3 generator contained two malformed Python declarations
(`def patch_feedbackdef ...` and `def applydef ...`). Both are corrected;
syntax/import checks must run before generating a candidate. This is a clean
build failure, not evidence that the owner's installed binary caused halos.

In both real D21 compositors the old raw-RGB consumer ran after Generic RGB/HSL
processing, the full-face-original early return, face/region previews and (V2)
edit-field validity handling. Consequently selecting raw recovery did not
reliably show the unattenuated final model edit. Colour4 places an explicit
bounded reference branch immediately after the original pixel load. It reads
the actual final-Neural accessor before these presentation decisions.

Master disable, frame readiness, the strict actual-model finite footprint,
negative-source retention, source-referenced preparation, independent colour
coefficient, gamut/max-ratio constraints, alpha preservation and all CPU-side
resource/queue/slot/retirement checks remain. Raw mode is not an unbounded
replacement image. No new texture, descriptor, GPU pass or NR invocation was
introduced. Modes 0/1 retain their old compositor bodies and remain the default
behaviour with the probe OFF. Colour/input curves, model parameters and scale
policy were not retuned speculatively to hide the reported problem.

The output-only **Zero-edit recovery test [session only]** forces the early
reference consumer to sample proxy P as synthetic neural N. The N interpolation
expression is derived from the production neural sampler, not assigned to P
AFTER sampling. P still follows the independent production proxy expression.
This tests their geometry/decode/interpolation pairing and exact zero-edit
retention through the production compositor. It does NOT test writes into the
real NeuralColor allocation, NVIDIA inference, the inverse/source scale on a
nonzero edit, or every pixel after target-format quantization.

The probe never overwrites model/history textures, never adds an NGX call and
is not saved in INI. Existing periodic reload preserves its live value. A
probe-only edit follows the preview-only transaction so it does not write INI.
A new process starts with it OFF. Return-to-legacy also turns it OFF.

## Evidence is not relabelled

F7's original/proxy/neural/composited planes retain the **real model output N**
through the existing writers. Only composed E uses synthetic N=P in the probe.
The new recorded consumer flag is bit 25 (`33554432`), together with raw/RGB
flags 27/30. An unknown or stale producer receipt cannot enable it; the UI
reports a missing receipt and asks for F8/BUG feedback rather than claiming
application. Requested config and the last recorded combine remain distinct.

On NR-before-SR, E is the game's SR input, not its final SR output. In V2,
captured P/N are resampled to the composition grid, not native-resolution model
plane dumps. Whole-frame F7 capture uses a sparse grid; fine textile analysis
needs the existing F8 ROI. The optional F8 detail-decision plane still describes
the legacy detail calculation, not attenuation adopted by the early raw branch.
F7 recreates/warmups NR and exports via existing exact-fence ownership; neither
a requested capture nor recorded constants prove a completed export.

## Windows build

Use an isolated worktree at this PR's actual commit and the already configured
LLVM-MinGW/ReShade/carrier/dependency paths. Do not modify the performance tree.

```powershell
pwsh -NoProfile -File .\scripts\build-colour4.ps1 `
  -UpstreamRoot '<existing pinned carrier checkout>' `
  -OutputRoot '<NEW isolated colour4 output directory>'
```

The script accepts the same optional toolchain, ReShade, face runtime, NGX SDK,
VcVars and reusable bridge arguments as `build-colour-lab.ps1`. It runs syntax,
inherited math/source gates, colour4 host sampling/math tests, canonical
pin-checked generation, actual colour3-export checks BEFORE
the new overlay, final colour4-source checks, then the existing Windows Core
builder. Output identity and manifest are checked. No game installation,
release, binary substitution or branch merge happens automatically.

Use **prepare/build-colour4**, not bare prepare-multi-api-beta and not the
historical colour-lab entry when building colour4. The latter remains colour3
for comparison. A downloaded source patch must be committed before generation;
dirty/untracked overlay bytes intentionally fail the source identity check.

## Next owner test: three conditions, not another broad slider sweep

Keep one layer, the same model DLL and SR/RR/FG settings, old input curve,
source-first area input and all other settings unchanged. Start at **99%**.

1. Probe OFF, recovery 1 (protected RGB): retain the previous visual baseline.
2. Probe OFF, recovery 2 (raw RGB): now bypasses the earlier aesthetic branches;
   inspect clothing structure and halos, not only perceived sharpness. Capture
   F7/F8 after the settings transaction has completed.
3. Probe ON, master enabled and readiness settled: expect S/E identity in F7.
   Repeat this condition at 100%. Do not interpret master-off, max-ratio=1,
   warmup or safety-retained invalid/signed samples as an identity-test pass.

The panel exposes the existing F7 request beside the new instrument. Capture
metadata must report colour4, complete=1, current flags and actual work/model
sizes. Keep the camera fixed for final screenshots; final SR screenshots are
not automatically synchronized with F7. On bypass, flashing, halos, wrong
colour or loss of rendering, disable the probe/restore the prior candidate,
collect **F8 + Core log + exact addon/model hashes**, and submit a BUG.

For the neutral comparison, inspect the completed F7 source and composited
planes from the SAME export, including alpha and actual codec flags. Keep the
real N/P difference separate: it is not expected to become zero when this
consumer-only instrument is active. Sparse-grid identity is not full-image or
post-SR certification. An independent offline capture analyzer was developed
locally but its GitHub write was blocked by the platform; it and its tests are
not part of this candidate and are not required by the build entry.

## Verification boundary at implementation handoff

- Actual raw Core was read from an owner-provided working tree and its Git blob
  verified against live colour3/source-gate HEAD:
  `5bf1a0d57969a8fc8252ccb87c54edc0be7196b8`.
- Exact current RGB helper, policy, generator, UI and host-math test identities
  were verified against GitHub; only reviewed source/helper transformations and
  synthetic inputs were used for local tests. No private game capture is part
  of this commit.
- Eight new actual-source integration gates pass, including early control-flow
  placement, unchanged mode 0/1 bodies, unchanged GPU ABI/capture writers,
  coherent receipts, transient reload/persistence, drift and all-before-write.
- GCC and Clang each pass 232790 production math/sampling checks; a required
  quarter-pixel negative mutation fails the intended pairing test. This is an
  artificial negative control, NOT a diagnosed game offset.
- Inherited production RGB host math: GCC and Clang each pass 200080 checks.
- Windows full generation, real HLSL compilation, Windows Core, hosted CI,
  NVIDIA GPU and game tests are **not verified by these host checks**. No new
  installable addon is claimed until the Windows builder produces one.

The owner's 99% halo and RenoDX clothing-detail gap remain open. This commit
fixes the misleading raw-reference control flow and build entry, and supplies
a usable experiment to isolate the remaining defect; it does not assert that
Lanczos or Streamline is the root cause or that the screenshots are now fixed.
