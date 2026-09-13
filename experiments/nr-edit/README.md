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
