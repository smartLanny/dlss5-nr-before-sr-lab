# Direct submission batch observation and QI reduction

Base: Perf12 `86acb70ba9f56715d26596651bea7cdfd9f677fa`.

## Change

The native Execute hook previously queried every submitted Direct list for
`ID3D12GraphicsCommandList` while any NR lifecycle record existed. Lists with
an exact `DXGI_ERROR_NOT_FOUND` lifecycle-token result on the submitted
`ID3D12CommandList`, and `GetType() == DIRECT`, now update their queue marker
through that same base interface without QI. Both interfaces use one shared
marker update implementation. A malformed, failed, non-Direct or present token
still follows the previous QI path. There is no pointer cache or retained
absence result. Capture re-reads its token and queue marker under its original
lock after queue-local serialization.

Queue affinity learning still occurs under `g_chain_cs`, including native
bypass lists. Migration bits, marker-update failure handling, capture pins,
duplicates, replay, detached records, native Execute, exact Signal and failure
quarantine remain intact. Source/guide and descriptor/post-boundary callbacks
retain their original entry/return placement; lifecycle absence excludes only
NR lifecycle capture. It does not mean those independent observers have no work.

The new `submitBatch` feedback records input list count, queue identity, one
sample token, token-list count, actual scans/QI, direct-absence decisions,
reason bits and chain-lock hold ticks/maximum. Existing acquisition, Execute,
Signal and fence-wait measurements remain. Batch records occupy their own
32-entry ring, with an epoch and completion-order sequence, so they cannot
evict existing carrier/Signal records. Totals and the ring reset with existing
timing policy epochs; late prior-epoch batches are excluded.

## Validation

`scripts/validate-submit-batch.py` extracts actual production Process, affinity,
Capture, Quarantine and Complete functions. The CPU fixture uses real Windows
critical sections/QPC and explicit COM callback seams. It also compiles and
exercises actual feedback export with printf format checking.

- Production: 155 checks passed, including independent callbacks, unknown/error
  fallback, malformed tokens, list reuse, token changes between lock sections,
  mixed batches, duplicate/replay, queue migration, failed Signal, detached
  ownership, 300-list batches and concurrent affinity updates.
- Six inverse controls rejected: old per-list QI cost, removed affinity,
  removed source observer, read failure treated as absence, skipped lifecycle
  capture and stale token reuse at capture.
- Existing submitted-fence fixture: 74 checks and six inverse controls passed.
- Existing RR timing fixture: 476 timed checks, 176 checks without timing and
  two inverse controls passed.

Evidence receipts retain compiler/source/binary hashes and raw output outside
the repository. Initial fixture failures were preserved: an unused-function
warning was fixed with `inline`; an expected inverse-failure label was updated
after the new list-reuse assertion became the first failing oracle. No safety
assertion was removed or weakened.

## Limits and integration

This is a CPU submission-overhead change, not a demonstrated game FPS gain.
No GPU, NGX model, WARP or game was run for this increment. Full staged Core
build and integrated game validation remain with the integration owner.

An unrelated list avoids one QI and Release. A list that needs fallback incurs
one extra base private-data read. Every eligible batch still takes the affinity
lock; the new bounded observations also have their own CPU cost. Hold ticks
exclude acquisition and metric publication, and cannot be summed with nested
stage durations. The sample token is not a complete list trace or GPU proof.

Fence waiting, lock-outside-wait lifetime design, UI, color, shader workloads,
configuration and wrapper identity recovery were not changed. Integration
must update the canonical pins for the four modified runtime/timing files;
this branch contains those exact pin updates only.
