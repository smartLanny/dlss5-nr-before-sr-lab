# Compatibility / FG coexistence / UI maintenance candidate

Base: released `beta0.4.3r2`, `dc876fc81ba831089f407f01a498e4b04d616122`.
Target: `codex/beta0.4.2`. This branch is not a new release. Match candidate
binaries by source commit, dirty state and actual addon SHA256, not their r2
filename. No main, DX11 bridge, FG patcher, runtime DLL or model changes.

## What changes

### Freshly observed pristine null-PSO admission

A known successful Reset(NULL) is not sufficient when graphics instrumentation
was installed after recording began. The automatic subset requires all of:

- A registered real DIRECT list and covered device meta-command contract.
- Complete native state and graphics guard publication for that list family.
- A successful native Reset observed AFTER guard publication.
- Known state, no unknown dirty domains, no bound PSO/DXR state object.
- Null baseline and no recorded dynamic graphics, vertex or stream-output state.
- No opaque bundle, render-pass, protected-session or work-graph taint.

The first known-null carrier requests the extra guards lazily, invalidates its
old recording and remains native until a subsequent successful Reset. Typed
PSO paths and default device startup do not proactively install the extra
surface. Failed publication backs off for one second; no false readiness.

Capture then uses the existing exact native ClearState(NULL) and root/heaps/
arguments restoration. ClearState cannot retroactively certify a recording
that predates guard publication. Opaque-command taint persists across
ClearState and is cleared only by a fresh successful native Reset. Full dynamic
null-state replay remains behind ExperimentalNullPsoCarrier=0; a dirty or
partially observed null list is NOT automatically enabled.

The shadow records presence and explicit setters, not whether a requested
view happened to look correct. Zero dirty bits in an old log do not establish
full observation. This implementation changes a narrow proven predicate, not
a game-name allowlist, not a timeout or a global removal of null-PSO protection.

### Missed ReShade list initialization

When a carrier has no registered list, a later real ReShade submission can
repair its owner mapping through the existing bounded registration table.
Submission does not establish PSO, bindings or a Reset baseline. A subsequent
capture allocates the shadow, hooks are installed and a new successful native
Reset remains mandatory. Destroy/reuse changes the token and discards proof.
The repair scan is active only after an unregistered carrier has been seen;
it does not fabricate a ReShade owner from an arbitrary native pointer.

### F8 evidence that corresponds to actual admission

The bounded last-carrier record includes schema/age/sequence, logical list
token, registration, hook/guard publication, fresh Reset, taint, baseline,
reset serial, pipeline-write count, dirty/dynamic/VB/SO masks, meta coverage,
root/heap presence and argument counts. No raw pointer or GPU address is
serialized. Nonblocking lock acquisition reports unavailable when contended.

Both start and end records enter the actual core collector's capture-info.txt.
They are last observations, not an all-frame trace or a dump replacement.
The copied ticket and local report show concrete reason/route and sample age.
A pristine-null last sample does not establish continuous neural rendering.

First NGX Hook failure evidence records operation, status, sample age and entry
shape. ReadProcessMemory inspects only bounded entry/prefix bytes on the cold
failure path; no destination is followed, no foreign bytes are modified and
no private path, raw bytes or addresses appear in the ticket.

### UI correctness

- The exported ReShade add-on names now match the existing r2 internal version.
- Pending edits carry the configuration generation they displayed. F6, reload
  or capture restoration can invalidate a stale edit; it cannot overwrite
  the newer configuration when dragging ends.
- A busy capture (including one starting inside ApplyConfig) preserves the pending edit for retry; conflicts discard it
  with a bilingual explanation. Snapshot, generation check and ApplyConfig
  use the existing chain lock and capture hold.
- Observer/state failures explain why stronger NR settings cannot fix them.
- Identity-original mode explicitly warns that the NR picture is discarded;
  this warning does not replace or hide the actual admission failure.

No global font, window geometry, focus or another add-on's UI is changed.

## FG/MFG: the important remaining limitation

Issue #63 reports Universal RTX40 MFG Unlock V1.2 with Alan Wake 2 / 4090 on an
older beta0.4.2 sample: core Create/Evaluate hooks fail, NR stays fail-closed.
The inspected upstream is dashdogy/RTX40MFG-Unlock at
4e776d068f91b4a665425542bb005dd57cc3d891, especially
source/native/entry_detour.cpp and its InstallForwarding callers.

That implementation publishes EB F9 with an E9 prefix, verifies its own
published destination and can dispatch through an anonymous relay carrying
register context. Overwriting the prefix breaks its current-entry check;
treating its private relay as an ordinary exported function loses the verified
ABI/lifetime boundary. This branch therefore DOES NOT force-hook that entry,
bypass its callbacks, adopt another trampoline, or clear RR ownership faults.

The new classifier makes the hypothesis distinguishable in one current report.
It does not establish that every unsupported entry belongs to that MFG add-on.
MFGAdaUnlock-RenoDx is a different implementation; upstream RenoDX DLSS5
coexistence claims are not acceptance evidence for this NR-before-SR core.
No FG multiplier client, Ada unlock, new FG creation, or display-cadence proof
is added. Existing FG remains under its original owner's control, including
when our master switch is off. #63 and the broader #16 remain open.

Primary source references:
https://github.com/dashdogy/RTX40MFG-Unlock/blob/4e776d068f91b4a665425542bb005dd57cc3d891/source/native/entry_detour.cpp
https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-reset
https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-clearstate

## Tests and CI

Portable actual-code extraction: scripts/validate-compat-sweep.py exercises
217 checks for pristine admission, every state-mask bit, stale UI transactions,
hook entry bounds/non-mutation and ticket privacy. Parameter iterations are
not distinct games. Existing bug/043r2 math and feedback parsers remain active.
The report suite has 15 tests, including redaction and historical-vs-current
interpretation of the new fields.

The Windows native harness includes tests/pristine_null_native.inl and the
actual production state observer/MinHook code. It exercises late registration,
lazy guard publication, failed Reset, ClearState, opaque taint, reuse and
snapshot contention. A real WARP compute dispatch must restore game constant
73 after a plugin dispatch writes 999. Deliberately skipping restoration must
produce exactly 999 and the expected negative-control exit 42. An arbitrary
crash or setup failure cannot satisfy that negative test.

The actual Windows core collector writes complete, cancelled and busy ZIPs.
The independent parser verifies those results and exact new metadata keys;
the test does not substitute a reimplementation of FeedbackAppendActivity.

This work also found a pre-existing CI gap: the structural identity script
still expected plain beta0.4.3, and a following successful Python command could
hide its failure in the PowerShell step. Updated r2 expectations and explicit
exit-code checks prevent that false-green condition. Unchanged offline rescue
collectors keep their own 0.4.3 identity. CI toolchain downloads now use the
same fixed SHA256 as the release publisher, and test artifacts identify their
candidate channel rather than claiming to be an official release.

Consult the final PR checks for Windows/compile/WARP results; this document
never predeclares those jobs successful. No real NVIDIA model/game, HDR/DPI,
MFG coexistence/cadence or performance matrix was run in the editing environment.

## Issue disposition / minimum acceptance matrix

| Issue | Candidate contribution | Still required |
|---|---|---|
| #38 Sinking City 2 / #50 Metro EE | Replace blanket rejection for the freshly observed pristine subset | Existing logs already justify the work. Test the candidate, inspect route and continuous successful submissions; dirty graphics remain bypassed. |
| #64 Elden Ring + ERSS | Same D3D12 null baseline candidate, no special Feature11 bypass | ERSS FG ownership and real output/cadence alongside NR; not universal ERSS support. |
| #65 Street Fighter 6 | Recover missed real list-owner registration without inventing state | Both PCs, exact r2/candidate identities; retain actual 3839x2159 region, mode and identity flag. |
| #43 RDR2 | Publication/backoff and actual state evidence | Intermittent NR root cause not proven resolved by these changes. |
| #53 NTE old sample | Shared safe subset if encountered | Current candidate and mode-specific retest; do not relabel old 0.3.7 evidence as r2. |
| #58 BG3 DX11 bridge | Design is reusable, no backport in this PR | Reconstruct separate bridge against current core; no immediate DX11 compatibility claim. |
| #63 / #16 MFG | Preserve first exact failure shape and explain coexistence boundary | Verified cooperative backend/observer path and NR/FG-only/combined A/B, real cadence. |
| #47 feedback | Structured admission evidence and report/ticket integration | In-game submission workflow and remaining offline collector unification. |
| #45 UI coexistence | Stale edits and clearer failure states, no foreign UI changes | RenoDX coexistence and 100/150/200% DPI remain game-side checks. |
| #49 007 crash | Evidence infrastructure only, no claimed crash fix | Matching crash stack/device evidence; F8 cannot replace an unavailable dump. |

For every candidate: offline supported environment, native baseline / NR off /
NR on, both NR modes, RR pass-through, F6 during slider edit, capture busy,
resolution/HDR/AltTab/load/exit, long-run submission and memory growth. Compare
actual frame time: extra guard observation and evidence have overhead; no
performance improvement is claimed. No game issue closes merely for CI success.
