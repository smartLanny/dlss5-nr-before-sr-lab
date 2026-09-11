# Exact sampled Output recording boundary (#205)

## Evidence already received

Two owner F8 captures made on 2026-09-09 at 13:19 local time identify
`0.4.7beta-output.2`, source `8890fe6d50f17102379272d6c3d785e4131d0e02`,
Core SHA-256 `f27849a99e31b718802335b4c5eea97da49060b36c4ce86cc55df3a756e02beb`.
They supersede the handoff's request for another output.2 report.

The post route (mode 1, config 2, epoch 1) advances NR and submitted counts by
54 each. Unknown queue bypasses, actual queue mismatches, marker failures and
retirement timeouts stay zero. Create-first-recording is captured with reason
none. Actual/expected mean stored RGB deltas are 0.0147735/0.0147616 and later
0.0136490/0.0136369, each with 2040 valid paired samples. This continues to prove
sampled Output storage, not final presentation.

Each reported native SR window has 26 ReShade entries but zero Output matches.
Following usage declarations are SRV (0xC0) to RT (0x4) and back to SRV, with no
copy/RT-bind match. Neither zero matches nor a usage declaration establishes
native exit state or a real overwrite. Probe frames 121/241 and boundary frames
214/268 were different frames, so they cannot form one same-frame causal trace.

The second capture is already pre route mode 2/config 5/epoch 2 and advances NR
and submitted counts by 76 each. Its post observer still says config 4/frame
1082; age grows from 5000 to 5765 ms and barriers from 962 to 1114. The cause of
this diagnostic contamination is concrete: closing at Present copied the live
window but did not disarm it. The current image defect is still open; the old
Feature18=0/command-state-unverified description is no longer its current state.

## This component change

- A sampled post-store dispatch arms one trace with the same slot, successful
  NR frame, configuration and Feature generation. The store evidence carries
  that trace ID. F8 joins all four IDs exactly; no nearby/latest frame substitutes
  for a missing trace. Start keeps summary metadata; end exports the event list.
- First matching-device Present freezes and disarms the recording window.
  Configuration/master changes, resource retirement, the next Evaluate and a
  500 ms recording deadline also close it with an explicit reason. Eight closed
  histories remain immutable. Same-device Present remains a CPU cutoff, not
  proof that this Output supplied that swapchain.
- Native ResourceBarrier and command-list-7 Barrier use actual SDK ABI types
  and the existing four-target CommandStateHookFamily/serialized publisher.
  An independent bounded diagnostic batch leaves the 48-slot restoration
  request array and NR admission unchanged. QI tear-offs, missing interfaces,
  publication failures and lost records are explicit coverage gaps.
- Native events preserve legacy subresource/split flags and enhanced
  layout/access/sync/six-field ranges. Nested calls and other list identities
  cannot supply the carrier's exit state. Global barriers stay global.
- CopyResource/CopyTextureRegion, UAV clears, ResolveSubresource and Discard
  provide explicit resource identity. Draw/indexed draw, Dispatch and native
  RTV clear/bind are observed but their targets remain unknown without proven
  descriptor ownership. ReShade can additionally resolve a real RT bind/clear.
  Repeated unresolved operations are counted without evicting every useful
  resource event. Opaque existing Bundle/Indirect/Meta/RenderPass/SetProgram
  hooks report their limitations; no general descriptor decoder is added.
- The existing Execute detour associates actual returned calls with command
  token/reset generation and known queue identity, including ordinary lists
  without an NR lifecycle batch. It adds no GPU work or fence wait. A CPU
  submission receipt is not GPU completion or cross-queue ordering.

The trace is bounded to six native events (first three/latest three if full)
and twelve following events, with explicit loss counters. Native methods cover
the installed carrier implementation families. Unobserved ray/mesh/render-pass
operations, descriptor-driven targets, aliases, opaque meta work and incomplete
queue relationships cannot be treated as a complete state or consumer proof.
No corrective resource barrier, composition change, queue permission or new
rendering fallback follows from these observations.

## Verification boundary

`scripts/validate-post-boundary.py` compiles the complete staged Core translation
unit and invokes the production callbacks and actual MinHook detours through a
synthetic COM ABI fixture. It does not create a D3D device or run WARP/NGX/game
work. Independent Microsoft/DirectX SDK and compiler SDK ABI checks cover the
legacy/CL7 slots and texture barrier layout. Tests cover exact forwarding,
target/list/scope isolation, publication failure, saturated history, immutable
closure, ordinary submission forwarding and the actual F8 writer.

Component-local validation uses a controlled overlay of this working slice on
the frozen output.2 full staging. The first attempt's unchanged main-source
overlay and test-name ambiguity failed compilation and remain in private logs;
neither is a pass. The corrected overlay passes 22 callback and 28 native-hook
checks. The actual F8 writer uses 29297 of 32768 bytes with full-width event
values, full bounded command history and 6000 bytes reserved for other metadata.
O2 compiler stack-use output records 2936 bytes for DispatchEvaluate, 3176 for
PublishPostBoundary, 3128 for ClosePostBoundaryLocked and 3752 for the F8 activity
writer; the new native event observers use 312/392 bytes. These are component
checks, not a claim that all pre-existing functions meet that stack budget.
The integrated
candidate must still be frozen and validated from its own canonical manifest;
the parent handoff records that separate result. No new game pass is claimed.

Next game evidence is one post-route F8 from the integrated candidate, using the
normal scene already reported. If exact native exit state conflicts with the
plugin's assumed state, investigate that state contract; if a proven later writer
replaces Output, follow that consumer. Any remaining coverage gap stays unknown.
Existing output.2 reports and seven-group F7 captures need not be resubmitted.
