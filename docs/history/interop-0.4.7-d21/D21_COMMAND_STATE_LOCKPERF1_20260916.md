# #277 — D21 command-state lockperf1 candidate

Base: `cbfb33919696a2840ed5154c91abaea56228b661` (`next/0.5`).
This is an isolated performance candidate, not a stable release or a measured
in-game fix. It preserves the existing #223, FG, Present, model and memory paths.

## Implementation

`MutateNativeCommandState` previously took the one registry-wide exclusive SRW
lock for every tracked setter. The candidate holds registry-shared ownership to
pin record/state lifetime, then takes a cache-aligned lock for that stable record
slot. Independent command lists may mutate concurrently. The same list remains
serialized; no observer is disabled and no mandatory update is dropped.

All existing registry-exclusive publication, capture, reset, restore and retirement
paths remain exclusive, and therefore cannot invalidate a record while a setter
is running. The two shared-registry readers of mutable descriptor-heap state now
take the same record-slot lock. Other shared readers inspect registry metadata or
separately published evidence only. Lock order is always registry then slot.

The negative TLS lookup cache is retained. It invalidates on the existing registry
epoch so later registration/DLSS enable remains observable. Positive entries are
never dereferenced before shared lifetime ownership, and the epoch is reloaded
after acquisition. Locks are in separate stable storage, not copyable state records.
Windows storage cost is 2048 cache lines (128 KiB), with no additional GPU allocation.

This is not a lock-free design. Shared registry traffic, exclusive snapshots and
other locks can still cost time. The uploaded v3 traces identify addon-originated
SRW contention, but the exact historical DLL was not available to map every RVA
to this variable. Game CPU/frametime improvement remains to be measured.

## Validation already executed

GCC 14.2 and Clang 17 ASan/UBSan: 16 mutation and 11 actual reader scenarios each.
The original function permits only one simultaneous independent-list mutation;
the candidate permits both. Same-list updates, late registration, address reuse,
2000 lifetime changes, descriptor read coherence, references and exception unwind
are checked. D3D12/COM objects are test doubles. Non-Windows runs use a host shared
mutex adapter; Windows runs use the actual SRW APIs.

No Windows full Core compile, WARP, NVIDIA inference or game test is claimed by
these host results. Do not count a missing compiler/shell as a test failure in Core.
Do not close #277 from these tests alone.

## One existing build path

Use the same five dependency/output parameters already used with D21:

```powershell
./scripts/build-lockperf1-candidate.ps1 -UpstreamRoot <pinned-carrier> -ToolchainRoot <llvm-mingw> -ReShadeSourceRoot <pinned-reshade> -DirectXMathRoot <pinned-math> -OutputRoot <new-directory>
```

Commit the isolated source first. The wrapper runs the new native-SRW gate and
the existing full generated bilingual/WARP build. It writes separate single-addon
TEST-zh-CN/TEST-en folders and retains exact linker maps privately. It does not
install an addon, change INI, add Bridge, change GPU settings or run a game.

## Owner acceptance

Compare the exact old D21 and candidate in the same scene/settings/display mode.
First check cold OFF and ON-then-OFF CPU time/frametime, not just average FPS on a
GPU-limited 9950X system. Then validate delayed DLSS enable, Before/After/Present,
FG ownership and resize/load/exit on already-working samples. Keep real NR work
sizes, passes, model controls, foreground/background load and actual FPS aligned.
An apparent FPS gain obtained by stopping NR or changing FG is not a pass.

A new failure or no CPU improvement should return this candidate's exact hash,
linker map and a short existing-style trace. No game PDB or global ntdll hook is
required to attribute this candidate's own code. No release/issue closure is automatic.
