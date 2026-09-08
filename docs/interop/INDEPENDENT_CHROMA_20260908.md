# #169 independent luminance/chroma candidate

This authorized continuation starts at #172 `6ee5494`, preserving #170 and
#119 ancestry. Final destination remains `next/0.5`; it is not a maintenance
default or release. Production source is `e084a5247d26eaf8fd60a741d131db7908829a7f`,
beta0.5-dev5 / PE 0.5.0.5. Later handoff documentation is not a new DLL build.

`IndependentChromaComposition=0` is the default. Enabling it selects the existing
V2 production pipeline for pre-SR and post-SR/RR composition and sets codec bit
4096, already carried in actual F7/F8 GPU constant records. Config equality,
INI persistence, load/apply logs and bilingual UI include the option. The model
input, NR call count, texture/pass count, root constants, ExactArea input and
bilinear restoration are unchanged. Disabled mode retains the legacy pre and
post reference entry points; their runtime equivalence awaits the final gate.

## Mathematics and controls

The candidate uses the existing luminance path: detail attenuation, bounded
LightingLock, optional one-inference layers, near-black/edge/frame confidence,
luminance budget and optional high-strength headroom protection. Chroma is
extracted from the *raw* model ratio before luminance-only protections:

`c = log2(max(Neural, epsilon)/max(Proxy, epsilon)) - log2(Yn/Yp)`.

The whole vector receives one scale:
`min(clamp(ColorStrength,0,2)*confidence, log2(MaxLumaRatio)/max(abs(c)))`.
A zero vector or zero budget gets zero scale. This cannot reverse an axis,
create a zero-axis edit or exceed the requested vector/budget. It avoids separate
channel clamps changing the edit direction. Nonzero TransferStrength/layer gains
control luminance only; the master zero setting still bypasses *all* edits.
Colour then renormalizes to the independently selected luminance, followed by
existing bounded-SDR/negative-gamut handling and storage encoding. Final gamut
compression can still change saturation; no claim of perceptual hue invariance
or improved game quality is made.

Existing colour-policy admission and exposure helpers remain authoritative.
Lighting protection does not grant unknown-HDR colour permission. The new
candidate preserves a signed source before decoding instead of silently
rectifying it. Existing nonfinite/model-identity/alpha/subrect safeguards remain.
An exactly zero final candidate edit writes the original stored pixel directly.
This is not a face mask or a complete live colour-semantics solution.

Post candidate selection additionally makes V2 edge/detail/layer controls
applicable on that route. For an attributable first comparison use protection
off, layer gains neutral, LightingLock/EdgeGuard/DetailStability zero, fixed
exposure and fixed R/N/O; then enable one protection at a time. Do not compare
different post defaults and attribute every change solely to independent chroma.

## Actual minimum verification

- Clean pinned source generation passed, including ancestry and reviewed blob
  checks; final generated source SHA256:
  `67a19fda16560012cce5680bacd5e270b5d5611b15b5a43cc88249e45f0bc7ca`.
- Source-manifest SHA256:
  `39654ff57f7e57e90c1adde2343825e657af26ffe74fd13d12fc9ec648fe606d`.
- Generated production C++ frontend compile passed with Chinese UI enabled.
  This is not a linked Core DLL. ReShade `novtable` attribute warnings remain.
- All six generated production SM5 entries compiled. D3DCompiler emitted
  existing exposure-helper X4000 warnings; no shader errors were suppressed.
- Compiled production scalar: 72,003 finite/direction/zero/budget/useful-edit
  assertions passed, plus configuration/routing checks.
- The extended production comparison harness compiled with `-CompileOnly`.
  EXE SHA256 `100ac03a4e35f260e30c3adfca745a08e53996059186b18df4dcaae03b9a8750`.
  **No WARP, GPU, NVIDIA or game dispatch ran in this slice.** No Core DLL was
  linked, no CI was run, and no push/merge/release was performed.

Private generated files, four compile/scalar logs and `validation.json` remain
under this worktree's ignored `build/` directory. They are not exported source.

## Centralized next action

The existing #172 fixture now exports 72 triples on identical Original/Proxy/
Neural: legacy pre, legacy post and this candidate. Schema v2 adds a sixth raw
plane and candidate luminance/edit metrics within the unchanged 8 MiB cap. The
Python verifier still accepts historical v1 captures. Real raw-N readback,
fence/alpha/padding and capture-on/off checks remain in the existing harness;
candidate zero-identity and input-equality checks are added. This extension is
compiled but its new triples/metrics have **not yet been executed**.

After the coordinating task releases the GPU, run from this branch:

```powershell
./scripts/validate-codec-gpu.ps1 -ToolchainRoot $Toolchain `
  -SourceRoot "$Generated/core" -ComparisonOutput $NewCaptureDirectory
python scripts/validate-composition-contract.py --capture $NewCaptureDirectory `
  --source "$Generated/core/src/nr_before_sr.cpp" --negative-control raw-extent
python scripts/validate-composition-contract.py --capture $NewCaptureDirectory `
  --source "$Generated/core/src/nr_before_sr.cpp" --negative-control metric
```

Use the final generated manifest above; the output directory must not exist.
`-CompileOnly` compiles without opening any GPU device. Parent coordination owns
actual WARP/NVIDIA/game/performance runs and Issue updates. Remaining #169 work:
broader exposure/near-black/saturation cases, actual live source semantics,
requested/applied per-pixel budgets and reasons, raw-N live capture, validated
game quality and timing. EditField/new input kernels and genuine repeated NR
remain separate unfinished experiments.
