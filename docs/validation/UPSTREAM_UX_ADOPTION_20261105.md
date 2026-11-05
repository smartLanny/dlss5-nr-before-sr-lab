# Upstream review and evidence-aware UX — 2026-11-05

## Reviewed sources and decisions

The latest retrieved release entries are
[OptiScaler NR v0.8.10](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/releases/tag/v0.8.10),
published 2026-10-26, and
[Magpie Experimental v0.6.11](https://github.com/SAOG0721/Magpie/releases/tag/v0.6.11-experimental),
published 2026-10-23. Both are prereleases. A release claim is not proof of a
performance or compatibility improvement in this Core.

Magpie's release distinguishes missing/partial timing from measured costs and
capture from generated output frames. This batch independently implements that
presentation principle in our existing F8 report, without copying its code.
Its actual [DLSSNRTemporal.cpp](https://github.com/SAOG0721/Magpie/blob/4fc74a196993c883ce2f01e96e8ed8d81f1490e6/src/Magpie.Core/DLSSNRTemporal.cpp)
was also reviewed: duplicate source draws do not advance history, motion metadata
is checked against source identity/extent/direction/units, and response time is
updated for the next capture. Our source/queue ownership remains independent.
Do not substitute their 80-ms time constant for our 60-ms half-life/reliability
policy: those are different contracts, not interchangeable preset numbers.

OptiScaler's v0.8.10 notes highlight per-pass network choices, low-resolution
depth/motion alignment, denoise-first/firefly/edge/history options, and FG fixes.
The actual [e797906f safety diff](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/commit/e797906f35039370f82583556d6e1b5cbe4c52fa)
was inspected: borrowed DXGI pointers must not be released, Unicode paths use
wide-character APIs with explicit failure handling, and menu buffer counts are
bounded. Our Streamline owner/pin path already has separate ownership checks;
there is no evidence that copying those patches would repair the same bug here.
Their Vulkan/menu architecture is not ours. Do not activate unsupported Vulkan
or FG routes by analogy, or replace user DLLs from a release note.

The guide-scaling, denoise-first and RTX40 MFG items remain targeted comparisons
requiring our exact extent/subrect, colour, jitter, original-call, queue/lifetime
and game evidence. No new model, guide resampler, FG provider or default is
claimed to have been integrated by this UX batch. Existing network selection
and private-SR research are retained rather than duplicated.

## Product changes

### Actionable residual-stabilization status

`src/nr_temporal_ui_status.h` maps all existing numeric states 0–22 and unknown
values to bilingual presentation. Missing/invalid motion, missing snapshots,
unsupported later-layer scope, private-SR conflicts and unproven Present
lineage no longer collapse into a generic waiting message. Persistent refusals
explicitly request F8 and reproduction steps while keeping protection enabled.

`src/nr_temporal_ui.inl` suppresses old status/adoption counters while a settings
transaction is pending; a retained quarantine warning is never hidden by a new
selection. The parent passes its existing pending state. A zero, future or old
submission tick is not recent. Existing backend/scope/mode controls and saved
values are unchanged. A submission is not pixel-history adoption. Global status
is explicitly the latest observation, not a per-configuration/per-frame proof;
this patch does not invent a configuration binding that producers do not have.

### Performance feedback without fabricated zero/FPS

`python scripts/feedback-report.py feedback.txt --symptom performance --output report.md`

The original TXT and core ZIP readers remain authoritative for framing, hashes,
partial captures and selected metadata. The new helper reads only selected
current metadata, never historical log excerpts. A valid snapshot, nonzero
matching request/end/timing configuration, stable sample counts and valid
numeric values are mandatory. Private SR additionally requires its explicit
availability flag and owner. Missing values remain unavailable. A genuinely
measured zero is accepted only with those receipts.

The report labels the NR/SR chain and private-SR timings as separate rolling,
partial GPU observations, never sums their medians, never converts them into
FPS, and never calls them a whole-frame or same-scene benchmark. Historical
samples within one configuration are still possible; the report explicitly
states that matching configuration does not prove matching time, queue or scene.
It distinguishes cold-start and warmed testing, warns that bypass FPS is not an
optimization, preserves original feedback, and never uploads or overwrites.

## Validation and limits

- 11 new performance tests pass through the actual TXT Document/Summary writer,
  TXT parser, ZIP parser and report CLI with controlled metadata. Missing/stale
  identities, invalid numbers, duplicates, log spoofing, partial files and
  no-overwrite behavior are covered. These are not live GPU measurements.
- 3 temporal UI tests pass with both g++ and clang++: the actual panel executes
  with UI recorders in both languages across 24 states, pending/stale/fault and
  clock cases. Independent pending-mask, zero-tick and generic-status mutations
  compile but fail the executable assertions. No rendered UI or clicks claimed.
- Adjacent report (15), original TXT (19), feedback-page (3), UI refresh (10),
  unified UI (6 plus one explicit staging skip), user controls (1 plus three
  explicit staging skips), UI compile (1), full-F8 routing (2) and product
  identity (2) tests pass in the local source snapshot. Skips are not passes.
- The source snapshot's 1,936 regular Git blobs were independently hash-checked
  against the d8c20e18 review receipt. This container lacks upstream Git history
  and Windows dependencies. History-dependent local suite failures are not
  waived; hosted full-checkout CI must validate the final committed source.

No rendering shader, resource lifetime, admission decision, model selection,
experimental default, game directory, Release or Manager default pin changes.
Windows full-product compilation, canonical WARP, UI visual behavior, real NGX
inference, game performance/quality and FG acceptance remain separate gates.
