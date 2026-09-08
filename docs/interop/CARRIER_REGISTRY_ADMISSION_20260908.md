# Carrier registry admission correction (2026-09-08)

This note records the small follow-up to PR #194 (`3c136254e0d6c4f51f682050e76800abbcad891c`).
The implementation is fixed in `6ad9cca801dd046358b0396e099a36ceb072a737`.
Only the production registry header and its focused regression test changed; the
generator, Core implementation, version, GPU, and game files were not touched.

## Proven pre-fix failures

The real `src/interop/nr_carrier_activation_runtime.h` was compiled directly
before the fix with the pinned Windows LLVM compiler at both `-O0` and `-O2`.
The focused harness reported the same eight failures at each optimization level:

- an empty registry treated `device == 0` as registered, so a swapchain and
  feature could be recorded for the empty identity and then admitted;
- `StillCurrent` remained true after same-device swapchain resize and destroy,
  and also accepted a snapshot whose flags were no longer known;
- a same-address `Created` call for an unregistered device returned zero but
  left the prior device's record live, so its old snapshot and admission stayed
  valid.

These are deterministic registry-state failures. They do not establish the
cause of a BG3 crash or any other game fault.

## Correction

`IsDevice` and `HasSwapchain` now reject zero before scanning empty slots.
`Created` clears an existing record for the same handle before rejecting an
unknown device, preventing address reuse from retaining the previous device's
record. `StillCurrent` now requires a nonzero valid snapshot, known flags, a
currently valid same-device swapchain, and a current record whose flags are
known. Resize/destroy therefore invalidates an in-flight admission snapshot
until a valid swapchain is observed again.

The generated carrier calls `Allows` while resolving the context device under
the carrier registry lock, then checks `StillCurrent` under the same lock
before entering the private bridge. A failed check keeps the native path in
control; it does not synthesize a bridge admission.

## Focused verification

The private local evidence contains the raw command output. Public source keeps
only the reproducible checks below and no machine paths or binaries.

| Check | Result |
| --- | --- |
| Focused real-header Python harness, LLVM `-O0` | passed |
| Focused real-header Python harness, LLVM `-O2` | passed |
| Existing `carrier_activation_runtime.cpp`, LLVM `-O0` | passed (CPU only) |
| Existing `carrier_activation_runtime.cpp`, LLVM `-O2` | passed (CPU only) |
| Existing loader stub and loader harness compile, LLVM `-O0`/`-O2` | passed |
| Native-bridge Python suite | 18 passed, 16 skipped because no staged production source was supplied |
| `git diff --check` | passed |

The loader executable was compiled at both levels, but its runtime check was
left for the parent after a new generated carrier DLL is built from this header.
The focused checks exercise the actual production header and do not claim a
full DLL, WARP, NVIDIA, or in-game result.

## Handoff boundary

The parent should rebuild the generated carrier and rerun the existing loader
and native-host gates after integrating `6ad9cca`. The registry remains a
bounded table whose callers serialize access with the existing carrier lock.
