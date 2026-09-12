# D16 Present recovery candidate — 2026-09-13

## Exact scope
Base: `codex/dx12-compat-multipass-20260912` at
`3c13f69119ddb0a0c1246f0348eb15c8aac64f76` (D16 / 0.5.1.16).
This is a review candidate, not a new released build. Do not silently replace a
local D16 working tree or reuse its existing DLL's identity for a new binary.

The ROG MCP returned `FORBIDDEN: This conversation does not support developer MCPs`.
No ROG file, game, installed payload or configuration was changed.

## Implemented production change
`src/nr_processing_route.h` and the actual `CarrierScope` in
`src/nr_processing_runtime.inl` no longer keep a failed automatic Present
fallback forever after one adoption.

- A previously adopted fallback gets 32 distinct *native reconstruction source
  opportunities* without another adoption before a preferred-route probe is armed.
- Initial creation/warmup gets 256 native opportunities. Merely recording private
  work no longer renews the adoption lease.
- These constants control a retry policy, NOT GPU completion. They are initial
  experimental thresholds, not measured optimal values or game profiles.
- Once armed, `Ready(Present)` rejects new Present work. Existing exact Present
  fences AND existing native NR retirement predicates must be ready before
  returning to the requested Before/After entry.
- The next entry still performs its existing format, guide, command-state, queue,
  lifetime and source-claim admission. No source claim/high-water mark is cleared.
- Duplicate/old source tokens and Present-only callbacks cannot advance the probe.
  Explicit Present remains Present. A healthy fallback with continuous adoption
  is not interrupted by periodic expiry.
- Saved settings, F6 behavior, models and default picture controls are unchanged.
  This change neither frees the old Present context on recovery nor treats a
  drained context as a displayed-frame proof. Normal lifetime teardown remains
  responsible for reclaiming its allocations.

`ProcessEvaluate` already constructs this scope under the chain lock before its
`PresentSelected()` early return. Guide capture may reconcile the same source
 earlier; separate native-opportunity tracking prevents that from hiding the probe.
No additional hook, image copy, GPU pass, forced flush or blocking frame wait is
introduced by the patch.

## The two reports must not be collapsed
### Once-working Present stays selected (Onimusha report)
The old production `Hold/Adopted -> floor_=Present`, with `BeginSource` starting
from that floor, reproduces permanent retention after 8,000 synthetic source
opportunities. This candidate changes that control flow. Whether the original
successful frame was a loading screen is still unknown.

### Submitted output has no source/content receipt (NTE report)
`submitted=1`, matching queues and `source=content=exactWrite=0` do NOT establish
a valid application-frame identity. `PresentPromotionEligible` and `CanPresent`
correctly reject that receipt before model execution. This candidate does NOT
claim to fix it.

Read `nr_source_frame_runtime.inl::OnDraw/OnCopy/CanPresent`,
`nr_source_views.inl::OnSourceInitView/FindSourceRtv/OnBindTargets` and
`nr_source_frame.hpp::Lineage::Submit`. A device-wide invalidation, unobserved
RTV target, unknown copy source, discarded batch, or an untracked postprocess
can lose the lineage. Existing evidence does not distinguish these causes.
Do not set `source=PresentCount`, attach the latest SR source to any backbuffer,
remove `nativeProducer` protection, or equate queue equality with source proof.

Next bounded sample: one real native SR output -> tracked exact copy -> Present;
then insert one known full-screen postprocess and track the exact invalidation
event/descriptor lookup. Unknown shader output must remain Unknown. Reuse an
actual same-frame provider receipt when available; do not invent one.

## Local validation actually executed
Linux host only:
- `g++ -std=c++20 -Wall -Wextra -Werror -pedantic -Isrc tests/d16_present_route_recovery_test.cpp`
- same flags for `tests/d16_processing_runtime_recovery_test.cpp`
- both under Clang AddressSanitizer/UndefinedBehaviorSanitizer
- 11 state-machine scenarios, including continuous healthy fallback, repeated
  recording without adoption, warmup, duplicate/old source tokens, explicit
  Present, After preference, no native opportunities and original backoff
- the *actual production runtime include* exercised with host stubs for Core
  lock, logging, source token and BOTH fence predicates; no mirrored runtime
- old-production reproduction retains Present through 8,000 source events
- three deliberate mutants (permanent fallback, ignore drain, recording renews
  adoption) are rejected by the same tests

Not executed: full Windows Core compile, shaders, ReShade/WARP, NVIDIA inference,
in-game Onimusha/NTE, HDR/FG, or end-to-end frame-time tests. Host fence booleans
are test inputs, not evidence of GPU completion.

## Required Windows/game exit
1. Merge only the small reviewed diff onto the current local candidate; keep
   unrelated local changes. Run the branch's existing build/validation scripts
   plus the two new tests, with both languages.
2. Onimusha: intro/transition -> gameplay, allow one successful fallback then
   reproduce source loss. Expect pending probe, no new fallback submissions
   during drain, then normal Before/After admission. Preserve actual requested
   setting; no duplicate NR for one source.
3. NTE: one fixed-scene F8 with bounded lineage-loss reason is still needed if
   its local code does not already explain the missing content. This issue is
   not automatically resolved by the route patch.
4. F6 off, explicit Present, resize, camera cuts, failed/unfinished fence,
   repeated mode selection, FG on/off and prolonged fallback must retain the
   previous source ownership and quarantine behavior.
5. Retain ZZZ's owner-confirmed latest passing result as a regression sample.

## Upstream references (ideas, not copied runtime)
OptiScaler NR reference pin: `e237f895623742b761f9e5f00067cb3dc62619f4`.
- `docs/DEFERRED-NR-DLSS.md`: clean main image; source/seam matching; failure
  keeps the main output; extra SR cost measured separately.
- `OptiScaler/shaders/dlssnr/DlssNr_ResidualPair.h`: same-frame seam pairing.
- `INSTALL-DLSSNR.md`: finished-picture placement does not manufacture guides.
- RenoDX issues #651/#652 are user reports about Present/guide or performance
  problems, NOT proofs of the root cause in this project.

Continue tracking under #190 / #226. Do not close either from host tests.
