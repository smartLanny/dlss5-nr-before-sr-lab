# D19 automatic Present mapping and descriptor batching

This isolated candidate continues D18 `305074d6f786064071fda24ac7f81d99645ab338`
using the owner's R2 cloud package (SHA256
`8b00fef19529a1dbfbb4a1a1b6b9d22d4b4c5d4e8e64a1a46199978982dcfff1`).
The input patch applies to the exact baseline blobs; checkout-only CRLF
differences were normalized in the new worktree. D18 remains available.

## Runtime changes

- Present can pair real native guides with an equally proportioned final
  output of a different size. The bounded raster-rounding tolerance is an
  inference of whole-viewport correspondence, not proof of image lineage or
  correct handling of crops, letterboxing or unrelated views.
- Native success, device identity, submitted producer fence, freshness,
  once-only consumption, ambiguity and known FG/inline exclusions remain.
  Original receipts are immutable; both motion-scale axes must validate
  before rebasing the leased binding from native output to Present units.
- Consumer demand is checked before new guide snapshots. Configuration and
  swapchain lifetime changes refresh or forget that demand. No dummy guides
  and no manual INI declaration are introduced.
- Descriptor resource/heap lookup uses address indexes; descriptor-copy
  metadata begins and finishes in batches. The real API still executes
  outside the metadata lock, with pending ranges and retirement preserved.

The developer menu exposes a low-overhead descriptor experiment, off by
default. Its saved preference applies before observation at process startup;
changing the checkbox cannot restore an omitted GPU shadow during the same
run. CPU format recovery and real native calls remain enabled.

Motion compensation has two choices: conservative automatic (default,
preserving D18 layer units) and forced compensation (additional Present-to-
layer-raster rebasing). Both preserve the required native-output-to-Present
mapping and use real guides. Switching retires the old context before the
new mode is bound; no game restart is required. Dimensions alone do not
establish that the forced mode will improve temporal quality.

NR pass counts, second-layer scaling and user defaults are unchanged.
Mapping a 1080p native output to a 4K Present with 100% NR
still costs a 4K model invocation; it is not a same-cost 1080p comparison.

## Validation boundaries

Windows CPU and WARP checks cover descriptor lookup, copies, revisions,
range masking, lifetime, stored pixels and native clears. The controlled
NVIDIA host checks real capture/Present execution with exact and scaled
output dimensions. Its native SR metadata is a fixture; it is not proof of
a real game's SR/FG relationship or subjective motion quality.

The frozen package's manifest records exact source, artifacts and results.
Cloud host scenarios and microbenchmarks are not game FPS measurements.
Existing D18 reports remain historical baseline evidence. D19 game results
and like-for-like Off/On frame-time measurements require the new candidate
under the same scene, display configuration, model dimensions and settings.

Owner acceptance starts with Onimusha Present at the existing settings,
including static view, camera movement and occlusion. Compare NTE's three
routes and Cyberpunk's equal-extent Present with D18, then collect only the
first new failure's F8 report. ZZZ keeps its existing mihoshade loading path.
Do not change display environment or lower inference precision and report
the resulting FPS difference as a code-only performance improvement.
