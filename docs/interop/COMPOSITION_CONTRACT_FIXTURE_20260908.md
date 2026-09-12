# #169 slice A: production pre/post comparison fixture

This separate change stacks on #170. It adds a runnable colour/composition
comparison entry point without changing any Core algorithm, live capture path,
model call or maintenance default. Runtime source remains the #168 candidate
`bf0ef31d5e822586781a349bfda77bed527a99b6` (beta0.5-dev4 / PE 0.5.0.4).

```powershell
./scripts/validate-codec-gpu.ps1 -ToolchainRoot $Toolchain `
  -SourceRoot "$Candidate/source/core" -ComparisonOutput $NewEvidenceDirectory
python scripts/validate-composition-contract.py --capture $NewEvidenceDirectory `
  --source "$Candidate/source/core/src/nr_before_sr.cpp" --negative-control raw-extent
python scripts/validate-composition-contract.py --capture $NewEvidenceDirectory `
  --source "$Candidate/source/core/src/nr_before_sr.cpp" --negative-control metric
```

Output directories must be new. The fixture bounds raw output to 8 MiB and fails
on existing output, input mismatch, changed output under capture, invalid alpha,
nonfinite output, a wrong N extent or independently recomputed metric mismatch.

## Executed evidence

72 pairs execute actual generated `CombineV2CS` / `CombineCS`, using byte-identical
Original/Proxy/Neural inputs per pair. Both 13×13 and noninteger 7×5 N grids are
tested against a 13×13 active source/output. Nine fixtures cover ordinary edits,
identity, nonfinite model values, luminance/colour edges, brightening, darkening,
chroma changes and linear HDR values; four strengths are 0, 1, 1.2 and 2.

40 pairs have distinct pre/post output. This confirms that the same strength
request is not proof of equivalent composition. The fixture reports RGB route
difference and each route's luminance edit relative to Original; it does not rank
quality from a lower edit magnitude. No novel composition candidate is selected.

Real source, N-sized Proxy/Neural and both composition outputs are copied to
readback resources after dispatch and mapped after the actual queue fence.
The capture preserves 1,214,208 bytes across 360 raw RGBA32F planes. A second
uncaptured dispatch verifies readback does not change the pre-composition bytes.
These are actual fixture texture dimensions, not full-resolution F7 model-plane
reconstruction. They are neither a game capture nor a final-SR same-frame pair.

`contract.json` explicitly declares storage and typed view as RGBA32F, a linear
BT.709 fixture contract, relative units, exposure/pre-exposure/source scale,
R/N/O, active source/output rectangles, synthetic frame/model identity and config
generation. Paper white is explicitly unavailable, not guessed from float storage.
Only the explicit HDR fixture opts out of SDR bounds. Default source/model values
are deterministic synthetic fixtures, not real Feature18 output. This RGBA32F
math fixture complements the separate FP16 post-scaled-work WARP gate.

`validation.json` binds the production source and its source manifest to all raw
plane SHA256 hashes and the metadata hash. Python independently reads each float
plane and recomputes dimensions, alpha and all reported metrics. Both deliberate
wrong-N and wrong-metric controls are rejected at their exact expected oracles.
The unchanged full codec suite also passes 384 WARP scenarios, including the
71 high-strength and 227 layered scenarios.

## Remaining #169 work

This is a useful partial A slice, not completion of #169. Still outstanding are
an optional new composition candidate, exposure sweep/near-black/saturated
highlight expansion, live runtime contract provenance and bounded live raw-N
capture, requested/applied budget diagnostics, exact same-frame exposure tracking,
F8 reporting, genuine NVIDIA/game sequences and GPU cost measurements. In
particular this fixture's declared colour semantics must not be copied into
unknown game inputs. No new face mask or segmentation is introduced.
