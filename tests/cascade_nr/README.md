# Issue #113: bounded cascade preparation (2026-09-08)

This is a CPU-only, test-local planning/execution ledger, not an NR implementation
or a production admission path. It introduces no public ABI and links no NGX.
The fake provider is actually called and updates scalar stand-ins for resources;
its results prove bookkeeping and identity composition only.

## Contract exercised

- One view, one queue, monotonically increasing source frame IDs; at most three
  independent feature/parameter pairs. Repeating a source frame is rejected
  before any provider call, including after zero-strength frames.
- Immutable original and P0; outputs ping-pong P0 -> A -> B -> A. Each layer reads
  the last completed output. Composition occurs once relative to P0, never once
  per layer. The scalar oracle is `original + strength * (last - P0)`; it does not
  substitute for the real protected full-resolution compositor.
- Each Create, Evaluate and final composition must complete before another
  stage or retirement. A successful Evaluate record alone cannot advance history.
  This is a serial fake completion protocol, not proof of D3D12 fence behavior.
- Layer count reduction retires removed layers; changed tuning recreates that
  feature and resets it and every downstream history. Epoch/guide/phase changes
  must be represented by a new source epoch; size changes recreate all features.
- Zero strength returns the original identity without Create/Evaluate/Composite
  or retirement. It invalidates histories and defers retirement to active work.
- A host decline before opaque work may compose the last completed layer.
  Create/Evaluate/completion/retirement failures poison the ledger, retain opaque
  dependencies and prohibit subsequent calls. On a real NGX failure, terminate
  the isolated host; returned last-output metadata is not permission to render,
  retry, release a pending feature, or reuse a possibly damaged command list.

The only prospective real experiment is explicitly Synthetic, a known static
input, all layers reset every frame, one view/queue and serial completion. Source
zero jitter is scoped to that static input. Every downstream phase remains
`Unverified`: zero jitter is not a general cascade contract. The separate
`mock_temporal_history_only` switch exists solely to exercise history invalidation
with a fake provider and must never admit an actual runtime experiment.

## Why no real two-feature host is attached yet

Contract inspection used Core source at
`74b737aa55a499c05df9e3367dede011b8db6bc7`. The existing `NRChain_*` exports in
`src/nrchain_nvngx.cpp` only forward function pointers; they do not own independent
parameters, resource bindings, context registration, or feature retirement.
`FindOrCreateContext` has no layer identity. `BindNrParameters` binds each slot's
proxy to its neural output. `PrepareBeforeSr` owns Encode -> NR -> Composite as a
whole. External Submit is single-view/frame admission to that whole chain, not a
second-stage NR-only API. Calling either twice or inventing view IDs would not
implement the requested experiment safely.

A genuine host therefore first needs a reviewed **internal** NR-only record seam:
explicit independent feature/params ownership; canonical input/output and guide
bindings; per-layer phase/reset/epoch; exact queue/command-list/fence registration;
and completion-gated retirement including failed-Create returned handles. It must
reuse the existing canonical parameter setup and dependency closure, skip repeated
Encode and Composite, preserve P0, and compose only once at the end. That factoring
is production work beyond this test-only preparation. No stub, copied Core chain,
fake view identity, or executable claiming to run two NGX features is supplied.

## Local validation

Base: `9d717566a9ef19a0a12200808138960e8daeff56` (isolated 0.5 worktree).
Compiled with LLVM-MinGW 20260826, assertions enabled:

```text
x86_64-w64-mingw32-clang++ -std=c++20 -O2 -static -Wall -Wextra -Werror tests/cascade_nr/ledger_test.cpp -o ledger_test.exe
ledger_test.exe
PASS 23 test-only cascade ledger scenarios; fake calls only, no NGX or GPU.
```

Coverage includes three-layer identity output, composed nonidentity output,
same-frame deduplication, increase/decrease/reincrease, failure and pre-call decline
at each layer, downstream invalidation, zero strength, duplicate opaque handles,
wrong reported output, failed completion, retirement failure and rejected inputs.

SHA256 (private binary/log retained separately, not committed):

| Artifact | SHA256 |
| --- | --- |
| ledger.h | `225cd96e5200ca90f7f7b22d74b7ec2bd86bcc010a9e4086a581d91d2ae4a0b5` |
| ledger_test.cpp | `5f817e8373f364fed0266f616edd71b8ffcf05fce02157c221a126b30d48fab5` |
| ledger_test.exe | `09b311b431b5a612d0b88685680bfc36e228763bd2b2f48c615cb620b4488a39` |
| run-final.log | `eaa86f8eeb22981482fe266a8b3c8a02df8462eeec1a88786b05e514b921ca35` |

No GPU, NGX, game, deployment, real temporal history, quality or speed result is
claimed. The next decision is whether to authorize the internal seam factoring;
any real runtime experiment still requires separate owner scheduling/review.
