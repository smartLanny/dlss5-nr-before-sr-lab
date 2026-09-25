# Unified5 private SR continuation — isolated, not the base Core

## Post-reconstruction final-residual transport foundation

The separate [post transport module](POST_RESIDUAL_TRANSPORT.md) extracts
`Area(Z-B)` from proven same-frame FP16 SDR code values and adds one reconstructed
edit back to B. It preserves the legacy Deferred/#258 entry points. It has no
Core runtime hookup or NR/private SR inference claim; its dedicated validator
also runs the legacy shader/lifecycle/staging regressions.

The additive stager now accepts the final `prepare-colour4-edge.py` pipeline,
puts its controls once on the unified Experiments page, and preserves every
unrelated generated file. Verified target: `4976ca48` (exact hash in the private
build manifest), six changed generated files and 219 preserved inputs. Both
language addons with the full face backend compile. This is an independent
overlay, not the base unified5 package or a new default.

## Real private Feature1 and neutral-carrier repair (2026-09-21)

`validate_private_sr_ngx.py` includes the frozen production Core, initializes
the official NGX loader through the unchanged verified bridge, creates a real
parent SR feature, exercises production owner resolution and the private SR
state machine, then reads actual NVIDIA GPU output. It never installs into a
game. Keep the explicit SR runtime beside the test bridge: placing the bridge
in a separate dependency directory produced `FeatureInitResult=bad00004`;
the co-located layout made the same runtime available. Both attempts are kept.

The real SR model moved the neutral 0.5 carrier: mean 0.5004882, maximum error
0.0036621 in this controlled fixture. The old compose then changed a 0.5 clean
image to mean 0.5039055, maximum error 0.0292969. Shader-only identity tests had
not exposed this nonidentity response.

The encoder now records whether the FP16 carrier actually contains an edit in
the existing FP32 tail alpha (0 invalid, 1 valid tail-only, 2 carrier edit).
The compositor ignores SR's carrier contribution when all contributing taps
have none, but still adds the exact spatial tail. Explicit nearest-even FP16
encoding avoids asymmetric truncation around the neutral carrier. No threshold
discards small residuals; no extra texture or GPU pass was added.

Validation uses actual 320×192 to 640×384 Feature1 inference, including neutral,
positive and negative carriers, resets, separate parameters and exact release.
Composition with the real neutral SR output preserves zero, +1/1024 and -1/2048
edits **exactly at FP16 output**. The frozen old shader fails the same real GPU
zero-edit oracle. Six-shader WARP checks also preserve signed edits and quantized
tails under deliberately biased neutral reconstruction. Eight full-source
staging tests and the private lifecycle/native-once CPU suite pass.

Run with a fresh evidence directory:

```text
python experiments/nr-edit/tools/validate_private_sr_ngx.py --core <frozen-overlay> --compiler <clang++.exe> --reshade <checkout> --nrchain <verified-bridge.dll> --sr-runtime <existing-nvngx_dlss.dll> --output <fresh-directory>
```

Limits: synthetic input, no actual Feature18 output or game scene in this host;
no moving-scene ghosting/occlusion/performance approval. Tail-only correction
remains spatial. The current-support rule can remove a historical edit where
the current carrier is neutral; transitions need image-quality testing. The
preferred post-reconstruction low-resolution route, five-layer integration on
the latest bridge candidate, and complete SR/RR/OF combinations remain pending.
This does not establish a separate full-image RR backend.

---

# Historical D21 private SR precision continuation

Imported only this experiment from project commit `04eda2e4`; Core source and
D21 runtime are unchanged. The older README below is historical build evidence,
not a claim that its generated overlay is now integrated with uniform2.

The Deferred encoder now computes the FP32 tail from an explicit FP16-roundtrip
carrier value. This preserves both clamp overflow and quantization error. Its
previous `delta - bounded` tail lost small edits which rounded out of the carrier.
The explicit stored value is representable, so the subsequent RGBA16F store
cannot introduce another rounding difference.

Actual D3D12 tests on WARP and NVIDIA hardware use RGBA16F original/edited/carrier/final textures and an
RGBA32F tail. Four new cases cover +1/1024 and -1/2048 edits with 1x/2x matching
exposure. Identity reconstruction reads the *actual encoded texture*, not an
unquantized CPU value. Tests inspect the stored half, require nonzero quantization,
and check tail/final within 1e-7. On both adapters the old shader fails the same harness; the new
shader passes, together with existing six-shader ABI, identity, signed, history
and invalid-motion checks. The old 0.002 tolerance could hide these small losses;
existing larger cases retain it and the precision cases use their tighter bound.

This does not validate NVIDIA private SR, its non-neutral response, zero-edit
frame suppression, or game quality/performance. The tail is spatial, so retaining
it does not imply temporal denoising of quantization error. A common private SR
backend and the post-reconstruction low-resolution route still need integration.

Relevant intrinsic reference: [Microsoft f32tof16](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/f32tof16).
Tests read GPU storage directly rather than assuming a CPU conversion's rounding.

---

# NR edit experiments: additive D18 base, D19 verified, D20-ready migration

Scope: #258 (NR edit around native Feature13 RR) and #259 (Deferred private
Feature1 SR for the edit). This directory does not implement or replace #257,
Present source tracing, sparse NR2, a loader, a temporal provider, or an installer.
The existing ReShade product remains the host.

## Why this is additive

The clean common base is D18 `305074d6f786064071fda24ac7f81d99645ab338`.
D19 checkpoint `4542731470b48ba2493aed22fd8bec856a6fc439` is a descendant and
has been used for a real generated Windows build and WARP validation of this
package. D20's unfinished local changes were not imported or modified.

This source commit adds only `experiments/nr-edit/`. It does not replace the
checkout's Core, Present, descriptor observer, version, configuration, or UI.
`Build-Against.ps1` invokes the selected target's OWN product generator, then
applies checked transformations in a NEW build directory. The target's version
is retained with an `-edit-v4` build identity and addon filename suffix.

Exactly five generated files are extended:

- `src/nr_before_sr.cpp`
- `src/nr_settings_ui.inl`
- `src/nr_processing_runtime.inl`
- `src/interop/nr_rr_post_runtime.inl`
- `scripts/build.ps1`

All other generated inputs are hash-preserved (194 files in the verified D19
checkpoint). The extra module files, six shaders, and compiler are then added.
Unknown anchors, concurrent source edits, duplicate application, or an existing
output directory fail rather than overwriting or guessing. This is an explicit
integration boundary, not a guarantee against arbitrary future source changes.

## Build against the completed local target

After that target's own work has a clean checkpoint, cherry-pick this additive
commit and run from its repository root:

```powershell
.\experiments\nr-edit\Build-Against.ps1
```

Alternatively run this package from its isolated worktree and supply
`-CoreRoot <completed-target-root>`; no cherry-pick is required for a test build.
Dependencies are detected from the existing local toolchain layout or the normal
`DLSS5_*` environment settings. Explicit `-ToolchainRoot`, `-ReShadeSourceRoot`,
`-UpstreamRoot`, `-FaceRuntimeRoot`, `-CodecCacheRoot`, and `-OutputRoot` are
supported. A cache is reused only after the original product script checks its
source/compiler/options identity. Do not bypass a rejected cache or source pin.

The command stages, builds both addon languages, executes the private-SR
lifecycle/flow host test, then runs real WARP shader/pixel validation. It writes
`validation.json`, an exact integration diff and manifest, and individual logs.
It does not install a game file or change a running process/configuration.
The unchanged cold `nrchain_nvngx.dll` is not rebuilt or included: retain the
working target's matching dependency rather than substituting an old payload.

## Runtime behavior

The experimental setting is off by default. It uses the existing settings
transaction and NR1/NR2 image pipeline; it does not feed residuals into NR2.

- Deferred: compute private E_R; run native SR with untouched input; run a
  distinct private Feature1 on the encoded edit; compose only at the matching
  successful native-output boundary.
- RR: compute private E_R; preserve native Feature13 inputs; normalize verified
  guides, process the edit, and compose after successful native RR.
- The saved legacy placement remains unchanged. Off restores existing behavior;
  rejected preflight keeps the target's legacy route. Partial opaque failures
  follow existing quarantine, not a second call to native SR/RR.

The first admitted image contract is D3D12, proven stable direct queue/device,
full FP16 HDR active raster, scalar exposure and supported native guides.
There is no game executable allowlist. This bounded capability contract is not
an all-format/all-API support claim.

RR history is enabled only when source reference, motion, projection and phase
are proved. In particular, nonzero/unknown raster jitter does not become a
fabricated camera transform: the UI reports spatial-only/cold history. Successful
WARP history tests do not establish those metadata in a real game.

Resource/parameter/descriptor retirement needs both the exact GPU fence and
release of the original command recording's references. Opaque vendor release
occurs outside the chain lock. The experimental allocations share the existing
budget and have an additional explicit cap.

## Evidence and limits

See `docs/VERIFIED_BASELINES.json` and `docs/LOCAL_AGENT_HANDOFF.zh-CN.md`.
The verified D19 product was compiled/linked in English and Chinese on Windows;
six SM5 entries passed strict compilation/reflection and actual WARP execution.
Positive, identity, signed edits, NaN/Inf motion and depth-history acceptance/
rejection were tested. The full runtime contract test uses test NGX callbacks:
it is not actual NVIDIA inference.

No actual NVIDIA private SR/NR, game image quality, game HDR/FG behavior, full
project regression suite, full performance comparison, or future D20 build has
been claimed. The actual D19 build used the existing disabled optional separate
face backend; enabling it on another target needs that target's normal build
inputs and validation. The host's native NR model controls remain intact.

This package retains the repository's licensing and provenance policy. It
contains no proprietary model/runtime, extracted shader binary, game file,
raw feedback, credentials or embedded build output. Algorithm and integration
files are source-only; compiled artifacts remain in ignored build directories.
