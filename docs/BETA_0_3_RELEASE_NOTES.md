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
- Ray Reconstruction is coexistence-by-exclusion: while any feature-13 handle
  remains live, both it and associated feature-1 work pass through unchanged,
  and the UI reports `RR safe bypass`. No timeout/evaluation-count heuristic
  classifies a retained handle as dormant; explicit Release or restart is
  required before Feature 18 resumes.
- Runtime, format, exposure, conflict, and lifecycle failures report a specific
  bypass reason instead of appearing as an unexplained lack of effect.
- UI value changes are committed with debouncing so dragging a control does not
  repeatedly rebuild history and warm-up state.
- Configuration writes are checked. If the game directory is read-only, the
  live choice remains active for that process, disk polling is stopped so an
  older file cannot overwrite it, and the overlay reports the Windows error.
- Every successful Feature 1 Create starts a new contract generation, even when
  NGX reuses its handle and dimensions. The prior Feature 18 context is retired
  behind its exact fence, and flags, exposure, formats and resources are checked
  again from the new generation.

## Stability and format work

- Queue submission is treated as an immutable batch, with queue-local
  `Execute -> Signal` ordering and reset-success-aware resource retirement.
  No private cross-queue GPU Wait is injected into the game's fence graph.
  Feature 18 Create/Evaluate, its temporal context and its slots retain every
  recording command-list owner plus an exact proof fence. Several game command
  lists may overlap only when their last observed native queue matches the
  context queue; capture checks the actual queue again before Execute. Unknown
  lists bypass until affinity is established, and unresolved queue changes fail
  closed. The candidate requires reset/re-recorded temporal DLSS lists on a
  stable D3D12 direct queue; Execute-time replay/migration detection cannot erase
  commands already recorded into the current game list. NGX/COM
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
- NR execution/transfer counters distinguish work prepared by parameter
  substitution, visibly recorded into a command list, bound to a verified
  lifecycle record, and actually submitted through native
  `ExecuteCommandLists`. A sparse GPU reducer also records the
  original, model proxy, Feature 18 output and composited result mean RGB/luminance/hash,
  invalid samples, maximum neural delta, explicit final-minus-original
  maximum/mean relative delta and changed-sample count, and the sampled exposure
  scale. Stage hashes use the same basis, so identical original/composited
  samples compare identically. Configuration records also include the cumulative
  successful Feature 18 count, making an F6/native-bypass interval directly
  auditable.
  This distinguishes a pass that never ran, a near-no-op Feature 18 result, a
  shift introduced by the model, and one introduced by composition. Sampling
  is deliberately sparse and read back only after its fence so diagnostics do
  not become a CPU/GPU sync point.
- Every 600 carrier evaluations the log also reports Feature 18 success and
  prepared/recorded/bound/submitted transfer counts, success/carrier percentage,
  lifecycle bypasses, same-queue admissions, unknown/predicted queue bypasses,
  and actual queue mismatches.
  This was added after testing exposed an otherwise silent 25--28% execution
  ratio caused by an over-conservative single-command-list gate.

## Diagnostics and build identity

- The external add-on filename, ReShade title, startup record, exported metadata
  and Windows version resource identify `beta0.3` and
  `@野生的装机宅 Bilibili`. Chinese and English builds are independent files;
  only one language build may be installed in a game directory at a time.
- The overlay keeps the current bypass reason visible continuously. The file log
  writes only state transitions, accepted/rejected contracts, errors and sparse
  summaries: the first three ColorDiag samples and then one per 600 samples;
  bypass transitions are limited to the first twelve and every 600th afterward.
- Startup records include UI language, actual loaded filename and build date so
  a report can be tied to the installed binary instead of an old copied config.
- See [COMMUNITY_FEEDBACK_BETA03.md](COMMUNITY_FEEDBACK_BETA03.md) for the
  sanitized mapping between early reports, current mitigations and unresolved
  title evidence.

## Color and HDR boundary

The original HDR SceneColor remains the composition base; only the neural delta
is grafted back. `PreserveLighting` is off by default. This removes the known
asymmetric full-range Reinhard error, but it does not prove universal HDR
support or visual equality between the two processing orders.

The neural delta is now mapped from the shoulder-compressed proxy into source
HDR as a relative change around proxy luminance. Scalar PreExposure is normalized
through a reciprocal transform clamped at 1024; this reduces exposure sensitivity
but does not make the nonlinear proxy mathematically exposure-invariant. The path
avoids both the old fixed-64x attenuation and a numerically dangerous full inverse
shoulder, and retains the existing final luminance/finite-value bounds. It is
intended to address the UE symptom where Feature 18 ran and changed every sampled
proxy pixel but the color handed to native SR was effectively unchanged; the
four-stage diagnostics are the acceptance evidence for each title.

A shader-loadable 1x1 ExposureTexture in the DirectSR exposure-scale format
family is sampled through its red channel in the same GPU command stream for
encode and combine. Other texture
dimensions/formats, unknown primaries/gamut/transfer, or an unsupported packed
color contract fail closed to native SR. Texture-only exposure jumps are
fence-delayed; changes larger than 0.5 EV reset history on the next matching
evaluation rather than the frame that first observed the new texture value.
Lower-resolution NR also has less face and texture information than
output-resolution NR, and the following SR pass can reconstruct it differently.

The GTA V Enhanced beta0.2 report was traced to a rotating 1x1
`R32G32B32A32_FLOAT` ExposureTexture. That exact live resource contract is now
accepted and sampled, but this is a source/log-level fix until a fresh GTA V run
confirms it. The current Hogwarts run also confirms that very small scalar
PreExposure values can be real input to the reciprocal exposure transform;
beta0.3 logs the sampled/canonical values instead of silently treating the pass
as absent.

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

Pre-current candidates exercised Cyberpunk `NR -> SR` to 1,796/1,800 (99.78%),
`SR -> NR` to 598/600 (99.67%), five Enabled cycles, and a path-tracing/RR run
with more than 2,400 feature-13 evaluations and zero Feature 18. Those runs
precede the final affinity-marker serialization and three-stage submission
accounting, so they are regression evidence rather than qualification for the
current hash. Hogwarts/Witcher evidence also belongs to preceding candidates.
The current binary still needs the minimum three-title gate, physical F6, and
remaining DLSS-preset cycles; the tree must not be tagged as a completed
beta0.3 release yet.
