# D light-style shader slice

Base: #200 `2e5bf87082cd573a640066b0342e66f46a934ffb`. This independently
implemented slice is intended for the coordinating D integration, not a
standalone linked Core: `ChainConfig` and capture-record field declarations,
INI/UI/transaction handling and source staging pins belong to that integration.

The shader appends five floats and one uint at constants 35..40:
`LightBroad`, `LightDark`, `LightReflection`, `LightStructure`, `LightGlow`,
`LightPreview`. The CPU names are `light_broad`, `light_dark`,
`light_reflection`, `light_structure`, `light_glow`, `light_preview`.
Root constant count is 41; original offsets remain unchanged. Production flag
262144 selects D; 131072 remains reserved for the existing Catmull test.

The incoming pipeline and mode are checked before independent-colour routing.
Only incoming `combine_v2` + mode 2 supports D v1. Neither legacy post nor
legacy post redirected into V2 gains D implicitly. Requested values are
separate from the actual constants; `light_supported` is true on a supported
neutral route even when flag 262144 is absent.

Centre and neighbour edit samples use the same reconstructed log-edit
expression on the composition grid. The field path uses the existing strict
Catmull/BilinearSafe validity contract; invalid neighbours contribute zero
weight. The known-valid centre anchors the denominator. A radius of eight
composition pixels remains unchanged with NR resolution. Log edits are
averaged directly, rather than mixing interpolated log edits with the log of
an averaged model ratio. This is an additional four-neighbour estimate;
it does not reuse the older differently expressed LightingLock estimate.

Same-sign broad/fine classification precedes D gains and the original
detail/lighting protections. The original partition is mapped proportionally
into the remaining protected edit, so a surviving fine edit cannot be
reclassified by clamping the old mean against a smaller protected residual.
Positive broad alone receives `(1-glow)`; positive fine, negative broad and
negative fine retain their separate controls. The resulting effective
luminance strength includes legacy layer strength but is capped at four.
Colour arithmetic and all original final ratio/HDR/gamut/storage guards stay
in place. Neutral settings bypass D arithmetic and sampling unless preview
was explicitly requested. No model, texture, dispatch or history is added.

Preview 1: orange wide-positive, blue wide-negative, yellow fine-positive,
magenta fine-negative; brightness is the remaining protected log magnitude.
Preview 2: absolute linear difference of the final protected NR result from
the original source, displayed at x4. It includes original NR/colour changes
and is not an isolated D-on minus D-off result. Both preserve alpha and use
the existing output encoding. Invalid input and master/warmup bypass retain
the original image. Preview settings must remain transient in the integration.

Local checks completed without GPU dispatch:

- `python scripts/validate-four-region.py --compiler <LLVM-MinGW clang++> --negative-control`:
  312,724 numeric assertions; all-positive glow, protection reclassification
  and missing-cap mutants each compiled and then failed the exact numeric
  oracle. Runtime/compile failures are not accepted as negative-control passes.
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/validate-codec-shader.ps1`:
  all eleven production SM5 entries compiled. D3DCompiler emitted exposure
  helper warnings, the optional restore-helper potential-initialization
  warning and existing finite-optimization warnings; no errors were ignored.

The coordinator still needs the real generated-source C++/bilingual link,
updated 41-constant fixture binding, neutral/partition/preview WARP, and actual
game/timing validation. No GPU, NVIDIA model, game, performance, hosted CI,
push, package, or deployment was performed by this slice.
