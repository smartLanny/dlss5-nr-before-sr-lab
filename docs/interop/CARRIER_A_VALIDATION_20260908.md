# A carrier production continuation and validation

This candidate continues #185 (`d35fe47`) and preserves the complete #175
ancestry. It imports the interrupted implementation without changing its old
worktree, then fixes defects found in the actual generated production paths.
The fixed execution plan is `0838ffb549a069dbcef9ac02914907be851d805d`, A only.
B/dev8 and the Manager candidate remain independent.

## Defects corrected

- An incomplete observer installation could leave a recursively held mutation
  lock and fail to count a retained live detour slot. Every failure now balances
  the lock, and enabled partial observers permanently retain their own slot.
  Release precedes Create; incomplete coverage cannot promote Evaluate.
- Enumeration supplied raw HMODULE values. The scanner now leases each source
  image before export lookup and each forwarded code dependency before prologue
  inspection. Installed observers retain the source export table as well as
  their actual hooked code. Other scanned images are released immediately.
- A notification epoch captured before enumeration rejects an intervening load
  generation change, including identity-table overflow. Rejections are scoped
  to the epoch so a newly loaded image at a rejected address is reconsidered.
  Missing loader notification registration remains native with explicit F8 BUG
  feedback. A full hook table still permits promotion of existing observers.
- Native Create may mutate its parameter object. The bridge now consumes the
  exact flags captured before that call, rather than rereading them afterward.
  Missing flags make the recorded handle ineligible for private bridging.
- Normal session retirement released resources but left the bootstrap latch
  set, preventing a later session. Proven retirement now clears the owner and
  rearms bootstrap together. Unproved completion retains the latch, owner and
  resources in quarantine; destruction of another device cannot retire them.

The imported production implementation also supplies synchronous exported
AddonInit, minimal DllMain, loader-only atomic event recording, permanent
MinHook trampolines, actual Create/Release observation, a serialized device/
feature/swapchain registry, paired resize handling and complete bounded normal
session retirement. These changes do not establish the cause of #153.

## Exact build and evidence

Production source: `4a564a361d32c307b59015ee976c4238b4a7bb6a`.
Documentation or PR commits after this point are not the DLL build source.

The canonical generator ran from committed, clean tracked source. Dependency
pins, ancestry gates, generated-file hashes and all existing build checks were
kept. The old worktree's local transform outputs are not this build's evidence.

| Result | Scope |
| --- | --- |
| Source/build gate | All 43 canonical check groups completed |
| Python suites | 73 multi-API tests: 71 passed, 2 skipped; 31 native-bridge and 35 UI/feedback/RR tests passed |
| Partial observer | 4 actual generated promotion cases passed |
| Scanner | 8 actual generated scanner scenarios passed, with real Windows critical sections and loader/hook API doubles |
| Canonical binaries | LLVM linked bilingual Core, nrchain and carrier; six generated production shader entries compiled |
| MSVC | Final carrier and activation test compiled/linked; activation and embedded native-host execution passed |
| Loader | Actual built DLL rejection/unload/reload, synchronous AddonInit, registration and idempotence passed using the ReShade loader stub |
| Actual detours | Real DLL exports and real MinHook patches; native Create/Evaluate/Release each forwarded exactly once |
| Lifecycle | Two real WARP D3D11 devices; controlled ReShade identities; repeated resize, multiple swapchains, unrelated-device destruction, flags, reused handles, and concurrent Evaluate/Release/destroy passed |
| Retirement | Real WARP D3D12 queue/fence retirement released owned state and rearmed bootstrap; missing completion fences preserved quarantine |
| Existing WARP | Format/baseline, completion, scaled-work, command-state, blend negative control and F7 checks passed |

The two skips require a separate pinned Feeder checkout. They are not A failures
and are not counted as passes. Existing upstream/compiler warnings remain;
the D3D12 debug layer was unavailable. The Core static-analysis gate reported
zero diagnostics; this is not a whole-carrier static-analysis claim.

Negative controls are retained privately: the old generated scanner fails all
eight new scenarios; the pre-rearm native host fails `g_probe_done == 0` after
real queue retirement. The first full gate stopped on an obsolete test-only
Core blob pin; the exact #175 input was verified and the pin updated, without
removing or relaxing the check. Its failed log remains intact.

| Artifact | SHA-256 |
| --- | --- |
| Canonical source manifest | `1a9bae15165f9838f9a85122b9c63878887081cf95e2cb253a5a78ed77597e1a` |
| LLVM carrier | `8859fbbd7107eb93ce8aaf84959b771bbe9dc49c3d660eb93c097c6f3738d6c1` |
| MSVC carrier | `7df3b846e51d192313919da7dd2b36b9e987c1b7fa185744ba8bc4a261a00b0b` |
| English Core | `eec03815b7ec94368622c8c3c6b29f10d4ef1647d276c4edb46437abdbe5bf7a` |
| Chinese Core | `0d02d024d8634ce0573138e59f1528ae37f96289b44f0b2add599165d5ee5bef` |
| nrchain | `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb` |

Run the existing `scripts/build-multi-api-candidate.ps1` with the pinned upstream,
LLVM, ReShade and DirectXMath directories and a new output directory. It includes
`validate-carrier-native-host.py`. The separate MSVC build uses the same frozen
generated translation unit and vendored MinHook sources. Raw build commands,
logs, test DLLs and binaries remain private local evidence.

## Remaining runtime work

ReShade callback identities in the native host are test wrappers, not an actual
ReShade game dispatch. The native DLL imitates NGX entry points; no NVIDIA model
or real carrier private-bridge frame ran. These tests therefore do not establish
Feature18, real shared-texture/NGX retirement, device-loss recovery, game visual
quality or long-session stability. #153 and the A/M1 game milestone remain open.

BL3 is now downloaded: Steam app 397540, build 15245523, executable 116,228,592
bytes, SHA-256 `923afd263631681aff88037add504bf353fc6f4caa9045cfdb709e049fe4f101`.
The read-only fingerprint found no temporal-upscaler DLL name, but does not
identify the runtime API or prove an absent statically linked route. No game
files were changed and no game was started. The next A step is the existing
[BL3 input observation route](BL3_INPUT_SEAM_20260908.md): establish real DX11
input/official SR-only behavior before connecting this carrier and owned NR.

No main merge, existing tag replacement, player installation, Manager payload
replacement or public binary release is part of this validation handoff.
