# A carrier production handoff (local, not published)

Historical interrupted-worktree record. For the subsequent fixes and committed
canonical build, see [A continuation validation](CARRIER_A_VALIDATION_20260908.md).
The hashes and earlier limitations below describe that earlier working copy.

## Source identity and scope

- Worktree: `codex/carrier-production-20260908-cloud` at
  `d35fe47da223b0f1b54b8dd5f514f95c073d589a` before these uncommitted edits.
- Authorized fixed plan: `0838ffb549a069dbcef9ac02914907be851d805d`;
  section A and ADR 0001 remain controlling.
- Pinned read-only inputs: carrier
  `28aed4099b0fe1c207b20b5fee5364c0773c25c2`, ReShade
  `4a50d1eddace85734871d91792ff214f13f66c01`, DirectXMath
  `28929ac6f3c1649a7641741b689ebecccb17aa8d`, and vendored MinHook 1.3.4.
- This is carrier production implementation and focused tests only. It makes no
  game, driver, GPU, release, deployment, push, PR, tag, or `main` change.

All earlier results and hashes formerly recorded in this file are superseded by
the implementation and evidence below. In particular, there is no deferred
registration worker and no scratch Git repository in the current result.

## Production behavior

- The generated DLL exports synchronous
  `AddonInit(HMODULE addon, HMODULE reshade)`. It registers with exactly the
  ReShade module passed by the pinned loader, installs lifecycle callbacks, and
  pins the successfully initialized carrier before returning. `DllMain` only
  records its module and disables thread callbacks. Registration rejection
  unregisters partial state, uninitializes private MinHook state, and permits an
  immediate unload/reload.
- Loader notification records only bounded lock-free module events, generation,
  and an atomic scan request. Present/device/swapchain callbacks drain and scan
  outside loader lock. Layer discovery, promotion, and hook installation share
  the hook mutation lock.
- Vendored MinHook publishes the trampoline and temporary target-module lease
  before enable. Only successfully enabled hook code is pinned. An enabled
  detour is never removed at runtime because a callback can have branched into
  it before incrementing an activity counter; it and its trampoline remain
  valid to process exit. Release observation is installed before Create, and a
  partial observer can never promote Evaluate or admit stale Create state.
- Native Create, Release, Evaluate, registry mutation, and device/swapchain
  destruction share the carrier operation lock. A successful native Create is
  forwarded exactly once and records its actual returned handle, exact D3D11
  context device, flags, and generation. Matching handles are replaced before
  a free table hole is considered. Missing or stale history remains native and
  emits bounded F8 BUG feedback.
- Swapchains are tracked by native identity and owning device. A resize destroy
  temporarily makes that identity inadmissible; its paired init restores the
  same record without accumulating a count. Final destroy removes it. Bridge
  teardown is conditional on the exact bridge-owner device, so destruction of
  another device cannot free the active session.
- Normal proven retirement releases the feature and mirrored resources, both
  private NGX parameter objects, the NGX D3D12 session, retained D3D11 context
  interfaces, all fences and allocators, command list and queue, adapter,
  private D3D12 device, and event handle. Any missing completion proof or failed
  NGX destruction/shutdown quarantines the remaining dependencies as before.

## Changed implementation and test files

- Generator/build: `scripts/prepare-native-bridge.py`,
  `scripts/prepare-native-bridge-045.py`,
  `scripts/prepare-multi-api-beta.py`,
  `scripts/patch-carrier-production.py`,
  `scripts/patch-carrier-completion.py`, and
  `scripts/build-multi-api-candidate.ps1`.
- Runtime support: `src/interop/nr_carrier_activation_runtime.h`.
- Tests: `tests/native_bridge/carrier_activation_runtime.cpp`,
  `tests/native_bridge/test_carrier_production.py`,
  `tests/native_bridge/test_carrier_completion.py`,
  `tests/native_bridge/reshade_loader_stub.cpp`, and
  `tests/native_bridge/carrier_addon_loader.cpp`.

The parent-owned `PR185_CLOUD_REVIEW_REQUIRED_20260908.md` was read as the six
mandatory acceptance blockers and was not edited.

## Local CPU/compiler evidence

Raw machine paths are intentionally omitted. `$Repo`, `$PinnedCarrier`,
`$LLVM`, `$MSVC`, and `$Validation` denote local fixed inputs or ignored output.

1. Applied the real ordered carrier transformations to the pinned upstream:
   native bridge, 0.4.5 integration, production carrier, NGX ABI, then completion.
   `python -m unittest tests.native_bridge.test_carrier_production -v` passed
   6/6 against those generated files.
2. LLVM and MSVC `/W4 /WX` both compiled and ran
   `carrier_activation_runtime.cpp`. It covers Create A/B, Release A, Create B
   at the same address, Release B, generation invalidation, four resize pairs,
   final destroy, unrelated-device destroy, exact flags, and ledger overflow.
3. The exact generated carrier translation unit passed LLVM and MSVC syntax
   checks. LLVM compiled the four vendored MinHook objects and linked the real
   generated carrier. The only diagnostics were seven pinned ReShade
   `novtable` warnings under LLVM.
4. The linked carrier passed the loader executable: minimal `DllMain`, rejected
   registration, actual unload, reload, synchronous successful registration,
   five-or-more lifecycle events, and idempotent `AddonInit`.
5. Extracted real generated completion functions passed 35 fake-queue
   completion/retirement cases. Extracted `BridgeReleaseSession` passed 11
   complete-session success/quarantine cases. Ordinary native errors were
   preserved and both hardware-fault children exited with `0xC0000005`.
6. `git diff --check` passed. The full candidate build was not invoked because
   it includes WARP/GPU gates and the owner granted CPU/compiler tests only.

Current local hashes:

| Item | SHA-256 |
| --- | --- |
| generated `dlss5-bridge.cpp` | `4392c5eb57bbc033b627cff8a4177301d511a2e2f30f0272b25233c00962138b` |
| generated `bridge.inc` | `e27b787d2d4ada7d986d49657b994f8c842a328393089615fc2ea783dc29530b` |
| linked LLVM carrier | `a2cf728ba0e24627bf56083b529c5ec2246ed13a615f2b0de32214bee2679f75` |
| activation runtime header | `3ed58d19182cef692479667a00179736b26ee5f1a0459139ba5af7a2246d137c` |
| production transform | `c47781b39b3b7ccc085f0e50cd381f4bfea975beee0fb3a1b58b83de354877cb` |
| completion transform | `5b1d1ac241588f819773d4a9470c9a6c800684e24b204ddce9927ad3c6fdef33` |

The canonical generator intentionally rejects a dirty tracked worktree, and no
commit was authorized in this pass. Validation therefore used a retained local
ordered-transform directory rather than weakening that guard. Those generated
sources, objects, executables, configuration, and inaccessible failed temporary
directories are local evidence only and must not enter canonical staging or a
public-source export.

## Remaining lifetime and game cases

- A native Create that occurred before observer installation cannot be
  reconstructed. It remains explicitly unsupported/native with F8 BUG feedback.
- An observer module whose Create or Release installation is incomplete remains
  safely native and pinned but is not retried during this process.
- Fixed device, swapchain, feature, and module ledgers fail closed on capacity.
  A module rescan cannot reconstruct an already missed Create.
- Enabled hook modules, the successful ReShade provider, and the carrier are
  process-lifetime dependencies by design. Dynamic successful add-on unload is
  not supported.
- No real concurrent producer, device-loss sequence, several live D3D11 devices,
  several live swapchains, resize callback stream, NGX shutdown failure, timeout
  quarantine, NVIDIA Feature18, WARP, or game was executed. Compiler and CPU
  doubles establish code contracts, not GPU lifetime behavior.
- This work does not establish the root cause of issue #153 and does not claim
  any game symptom is fixed.

Next action: parent reviews and stages only the intended files, commits in the
authorized clean environment, runs the canonical generator/build gates, then
schedules GPU/game lifetime validation only under the separate serial grant.
