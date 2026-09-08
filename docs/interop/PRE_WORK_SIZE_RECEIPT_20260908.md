# #82 native pre-NR coherent work receipt

This isolated continuation starts at #183 `aa27a9c` and adds only the remaining
native pre-SR diagnostic observation. Production and the complete private DLL
build are `a7c651fb980863f56e280c4f80dc132735042ad3`. The CPU fixture's later
mock-initializer correction is `326a6cf25e679fb4910ad02451b7f39ab727a673`;
it is not the DLL source. No shader, NR processing policy, Manager, 0.5 code
or external-provider ABI is changed.

## Observation and publication boundary

`PrepareBeforeSr` already resolves its actual Color active rectangle to R and
creates/binds NR at the actual `context.width/height` N. Its Reference/100%
work resolver intentionally returns early. The new diagnostic helper observes
O independently from the same invocation's Output resource/base and existing
`ResolveOutputExtentFromEvaluate`, then verifies the resolved region against
the actual resource with `TextureContainsRegion`. It does not use sampled
global width/height atomics or change the work resolver.

The call occurs after successful NR evaluation and combine_v2 command
recording, before replacing the Color parameter for native SR. It is a void
diagnostic call: missing, partial, out-of-bounds or ambiguous Output information
returns without publication and cannot reject NR, alter parameters, set a
bypass reason or deactivate the already prepared substitution. A last valid
historical receipt may remain; it is never relabelled as the unknown invocation.

The existing resolver's authoritative optional Output dimensions take priority,
followed by its existing Create/physical-resource contract. A nonzero atlas
base without a resolvable Output extent remains unknown. This slice adds no
new interpretation of an otherwise ambiguous frame.

The receipt uses `native-pre-nr-recorded`, the same real NR evaluation and
command token, config/history/feature generations, NR instance and slot-use
identity as the post receipt. N is checked against R for this route; the
post route's N-to-O constraint remains unchanged. All dimensions come from
this accepted invocation, including odd 100% R=N, rather than recomputing N
from a displayed percentage.

Submission and completion use the unchanged exact command-token, actual
Execute/Signal and nonblocking queue-fence observation gates. They do **not**
claim that subsequent native SR succeeded or that anything was presented.
F8 explicitly emits `downstreamSR=unknown` and `presentation=unknown` for
pre-NR receipts; the UI displays the same boundary. Game frame/view and an
independent resource allocation generation remain unknown. External V1 still
has no downstream O and is explicitly unsupported for a full R/N/O receipt.

The post publisher, `FinishAfterSr`, receipt snapshot and
`CompleteSubmissionBatchLocked` were compared against aa27a9c and are unchanged
function bodies. The existing frozen post-only build at ed38ae9 remains valid
for that earlier scope; its “pre unsupported” text is superseded by this slice.

## Actual validation and build

One final generated source stage, `build/pre-work-receipt-generated`, was
used for the CPU checks and the single complete bilingual Core/nrchain link.
The test extracts the actual production Render/Output resolvers and receipt
publisher. **39 CPU cases passed**, including the prior 26 receipt cases and
new odd Reference/100%, stale Create versus current Color/Output rectangles,
DRS/scaled N, read-only observation, ambiguous atlas, partial/missing/oversized
Output, invalid sample contract and unsupported external cases. Source-order
checks verify observation after actual NR/composition and before Color
substitution. These are CPU/mock-resource contracts, not GPU/model tests.

```powershell
python scripts/validate-work-receipt.py --compiler $Compiler `
  --source-root build/pre-work-receipt-generated/core
```

The initial auxiliary audit decoded Git source with the Windows default GBK
encoding and failed before updating pins; source generation correctly rejected
the stale pin. The audit was rerun with explicit UTF-8 and the reviewed pins
committed. The first CPU fixture build rejected incomplete mock descriptor
initializers under Werror; the later fixture initializes all fields. Both
original failure logs are retained; neither required a GPU run or DLL rebuild.

All **117 original manifest entries** match after linking. Both Core binaries
embed a7c651f and numeric PE 0.4.6.1. The inherited hotfix.1 filenames are not
claims of identity with released assets; hashes distinguish this private build.

| Artifact | Bytes | SHA256 |
| --- | ---: | --- |
| English Core | 1,354,240 | `549b6ab5eaeb96e3c4e078d75b2a4c0619e62a1a90bf534e2ea975611c94279b` |
| Chinese Core | 1,376,768 | `e7e2b1b15c38353aa65d5c37dc3c8dd5cf544f15402c956d8033de293273ead3` |
| nrchain | 8,192 | `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb` |

The unchanged shim contains no Core source/version resource.
Generated Core SHA256:
`b940b4bcde4e08b71f8d1fb40c2cec2b941a405b3f1634f2f9e0cd99736d2dfe`.
Manifest SHA256:
`1da25d98da732b30ff1dd7519a0136e0855dd07a95977be12f4b2463c003283b`.
Private `build/pre-work-receipt-validation/validation.json` SHA256:
`3e5a5402af0a6d091e11cbc11b63194fa639edff8c508599c63ba135241f85a4`.
CPU log SHA256:
`f62ec76a00ef7e56ce0491832d0f2c22195261196b6eaf3d9729c189f0a5107f`.
Full build log SHA256:
`d0be47cff98702a1f09048bb9417ef71b73b56f96af4492ef46482bd49801ffe`.
Warnings and earlier evidence remain private and intact.
The coordinating task independently reviewed the post-composition/pre-Color
publication point, diagnostic-only O failure behavior and preserved post/Signal
gates, then rehashed all 117 manifest entries, all three artifacts and validation
3e5a5402. Every value matched.

No GPU/NR/SR/game execution, WARP matrix, live UI/DPI or performance test was
performed. The first-class unknown identities, external downstream O, 1–49%
admission and integrated real-frame validation remain open. After its independent
review, the coordinating task authorized one fast-forward update of existing
draft #183. Publication does not add execution evidence. This is the final
ordinary code slice of this batch; larger architecture work awaits the owner's
separate decision.
