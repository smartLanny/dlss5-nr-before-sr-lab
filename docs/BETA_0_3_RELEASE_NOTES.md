# beta0.3 stabilization candidate

beta0.3 is a safety and compatibility release for the experimental D3D12
DLSSNR-before/after-DLSS-SR add-on. It remains a research build, not an NVIDIA
product or an official joint NR/SR implementation.

## Release behavior

- `NR -> SR` is the default chain. Existing beta0.2 settings are migrated to
  this default once; the add-on does not switch modes automatically afterward.
- `SR -> NR` remains fully available as the full-resolution quality/reference
  path. It is admitted by the live format, view capability, subrect, exposure,
  guide and command-list contracts, rather than by a hard-coded game-name list.
- Master-off takes the native-SR fast path. It must perform no Feature 18 work,
  texture substitution, conversion pass, exposure change, or retained NR work.
- Ray Reconstruction is coexistence-by-exclusion: while feature 13 is active,
  both it and the associated feature-1 work pass through unchanged, and the UI
  reports `RR safe bypass`.
- Runtime, format, exposure, conflict, and lifecycle failures report a specific
  bypass reason instead of appearing as an unexplained lack of effect.
- UI value changes are committed with debouncing so dragging a control does not
  repeatedly rebuild history and warm-up state.
- Every successful Feature 1 Create starts a new contract generation, even when
  NGX reuses its handle and dimensions. The prior Feature 18 context is retired
  behind its exact fence, and flags, exposure, formats and resources are checked
  again from the new generation.

## Stability and format work

- Queue submission is treated as an immutable batch, with queue-local
  `Execute -> Signal` ordering and reset-success-aware resource retirement.
  No private cross-queue GPU Wait is injected into the game's fence graph.
  Feature 18 Create/Evaluate, its temporal context and its slots share a
  recording owner and proof fence; unresolved queue changes fail closed, while
  proven same-queue submissions rely on native queue ordering. NGX/COM
  destruction is drained only after submission and lifecycle locks have been
  released, and a separate API lock serializes NGX method calls.
- A failed Feature 18 creation that returns a non-null handle is isolated with
  its dependency closure; further process-lifetime retries are disabled.
- Controlled FP16 intermediates remain the NR contract. Render-resolution
  `R11G11B10_FLOAT` input conversion is supported, and beta0.3 adds the matching
  output conversion needed for eligible `SR -> NR` comparisons.
- `R8G8B8A8_UNORM` conversion plumbing is present but stays disabled unless a
  validated color contract declares linear or sRGB-encoded storage. A typed
  `_SRGB` resource remains fail-closed until its legal UAV write-back view is
  proven. The linear and sRGB contracts use distinct encode/combine paths; the
  setting is not merely a format bypass.
- Active subrects, non-zero bases, dynamic extents, alpha, resource state, and
  conversion write-back are validated before the add-on may modify game input.
- Per-Evaluate render/output subrects and bound live resources outrank Create
  scalar dimensions. Create values are fallback evidence only after they fit the
  exact resources used by that Evaluate call.
- NR execution/transfer counters plus a sparse GPU reducer record original,
  model proxy, Feature 18 output and composited result mean RGB/luminance/hash,
  invalid samples, and maximum neural delta. This distinguishes a pass that
  never ran, a near-no-op Feature 18 result, a shift introduced by the model,
  and one introduced by composition. Sampling is deliberately sparse and read
  back only after its fence so diagnostics do not become a CPU/GPU sync point.

## Color and HDR boundary

The original HDR SceneColor remains the composition base; only the neural delta
is grafted back. `PreserveLighting` is off by default. This removes the known
asymmetric full-range Reinhard error, but it does not prove universal HDR
support or visual equality between the two processing orders.

An unverified non-null ExposureTexture, unknown primaries/gamut/transfer, or an
unsupported packed color contract must fail closed to native SR. Lower-resolution
NR also has less face and texture information than output-resolution NR, and the
following SR pass can reconstruct it differently.

## Runtime tracks

RTX 50 and RTX 40 testing use the same beta0.3 add-on source and build. Only the
externally supplied Feature 18 runtime differs:

- RTX 50 qualification targets the NVIDIA-signed 310.8 runtime.
- RTX 40 qualification is a community test track for ShortFuse SF-v2 and is not
  a claim of complete RTX 40 compatibility.

No NVIDIA or ShortFuse runtime is stored in this source repository. SF-v2 must
not be included in a public package unless its redistribution rights have been
confirmed. See [RTX40_COMMUNITY_TEST.md](RTX40_COMMUNITY_TEST.md).

## Release gate

The beta0.3 label is a candidate until the three-title minimum gate in
[BETA_0_3_TEST_MATRIX.md](BETA_0_3_TEST_MATRIX.md) passes: 300–500 frames per
exercised mode, five F6 and mode switches, and three DLSS-preset changes in
Cyberpunk 2077, Hogwarts Legacy, and The Witcher 3. Master-off and RR-safe-
bypass intervals must record zero Feature 18 evaluations. The seven-title,
600-frame, 20-cycle matrix is deliberately deferred to RC/community testing.
The fixed-scene image/performance gate now contains four isolated groups:
RenoDX/RHI 4.70 pure SR, its official post-SR NR, this add-on's `SR -> NR`, and
this add-on's `NR -> SR`.
