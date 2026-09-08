# Core/provider continuation after startup preflight

This batch follows the separately frozen startup result at `492f802`; those
binaries and logs remain historical evidence. The **new actual combined build
source is `e7df0fcf26f28e9661a8dcfe0322377278820dd6`**. Later documentation is not
a new build. It contains all prior ordinary maintenance/provider integrations.

## Production changes

- `f819a8e`, #107/#164: external NR recording now uses the existing RR
  transaction gate, in the same gate-before-chain lock order as native NR.
  Nested/busy transactions decline. Aborted/uncertain transactions quarantine
  recorded resources before reporting failure, rather than reporting usable NR.
  In an external-provider build, native RR post processing rechecks the
  external owner under the claim lock; it performs native RR once without a
  second native NR. Ordinary non-provider builds retain their route. This is
  new-provider coexistence, not a rewrite of the fixed cached-RR-handle bug.
- `f6de13d` (Sol source `1e90ecd`), #153: both carrier startup enumerations
  bound the returned module count to the actual 1024-entry array. Windows can
  return a larger required byte count than the caller's capacity; treating it
  as an array bound was a definite out-of-bounds read. The API contract is in
  [Microsoft's EnumProcessModules documentation](https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-enumprocessmodules).
  This fixes our avoidable memory access; existing game evidence does not
  establish that it caused every #153 crash.
- `c449882`, #14/#164: external source-pixel jitter now scales independently
  with N/R on each axis and respects the existing jitter mode and validity
  limits. Native/Extracted resource provenance declares supplied jitter,
  including zero; legacy Synthetic zero remains unspecified. Original source
  values/scales are retained in the existing diagnostic representation.
- `c449882` / `d6e61f8`, #14: a typed FFX dispatch-to-NR lease adapter reuses
  the existing FFX→DLSS validation and NR ABI. It preserves explicit provenance,
  jitter, reset, motion scales, exposure scalar and depth/HDR flags. Caller
  ownership of the command list is explicit; queue/fence/device and the claimed
  completed fence value are checked. It rejects current consumer limitations
  (exposure textures, high-resolution MV, cropped allocations) before recording
  anything. It is a descriptor adapter, **not a game dispatch hook**.
- `e7df0fc` (Sol source `8a4ed78`), #9/#10/#90: the controlled combined renderer
  records three D3D12 timestamps per source frame, queries the real queue
  frequency, and maps its dedicated readback only after that batch's actual
  completion fence. NR warmup/identity frames remain labelled as unrecorded
  transfers. No CPU time is substituted for GPU measurements.

## Final source/build identity

The full-source manifest and every generated file were rehashed after building.
Both Core languages, nrchain and carrier were compiled from this final source;
the host was compiled with the existing fixed official SDK dependencies.

| Object | SHA256 |
| --- | --- |
| Final generated manifest | `acac50ca2269f1c69fcb52c9c0b2d59d5a39829ea0b165f66b532b30c29d9c67` |
| Final provider translation unit | `0bf454eae23f86e46f9378eb80c8df6cf3110f6cfd3e26a9858c17d347a389f8` |
| Chinese Core | `8e9394889c3fd3309e035bc67e26d4d25c5b711e4a392f86f5e1c89a5a1caf38` |
| English Core | `50256a1e6ab8d069154fa8ac7f0242f8c798d46d3ff055a32cece359d75dc04f` |
| nrchain | `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb` |
| Carrier | `ae3aa923de23984bd0faf3cb2e81e382c609eff7b426691b5e3d7537baef62f1` |
| Timestamp-enabled combined host | `e2cfbd65a8fd34388c464d38778c869eaa88c8571fe8f2ab611d7a122b79db76` |

Core embeds source `e7df0fc` and retains the generator's `SourceDirty=1` marker
for transformed experimental source. A preliminary private build adapter had
used the canonical checkout's clean marker; those binaries remain archived and
were not used for this final hardware run. The final script preserves the
generated identity and version strings and writes to a separate output folder.
The prior `c449882` compile-only artifacts are also retained, not relabelled.

Private evidence `core-provider-runtime-lock-20260908` contains the manifest,
validation JSON, exact build scripts/commands, initial and corrected build logs,
all binary/file hashes, and distinct `off`/`on` runtime directories. Raw game
paths, vendor payloads and logs are not exported with this source summary.

## Concentrated validation after code freeze

- Focused Windows source/control-flow checks: ten tests passed and one
  unchanged Feeder-dependency test skipped. These include the real RR post body
  with an external owner (one native invocation, no preparation/composition),
  real worker terminal states, jitter agreement with the native resolver,
  provider transformation and carrier transformation contracts. Earlier
  startup/client/real-DLL negative tests retain their `492f802` build identity.
- Actual WARP resource admission fixture for the FFX→NR adapter passed:
  explicit command ownership, fence identity, reset/scales/provenance, unsupported
  exposure texture, cropped allocation and aliases. This fixture validates
  declared resource contracts, not GPU dispatch or observed game resource state.
- Sol's bounded carrier slice passed LLVM and MSVC links and an existing
  no-device/no-present loader/NGX-stub unload probe. The final integrated carrier
  above was freshly linked with LLVM. No game-crash resolution is inferred.
- One final independent startup per NR-off/on used the rebuilt Core and host
  above on RTX 5090 Laptop: both completed 8 real SR frames; NR-on completed 5
  actual project NR transfers after training/warmup. Each O readback contains
  3,686,400 finite half channels with spatial RGB variation. NR-off's first three
  hashes still match the earlier SR-only baseline. Each run registers one SR
  contract; NR-on creates one external NR instance. Release/shutdown succeed.
- For this synthetic R/N=640×360→O1280×720 run, the five transferred frames had
  median NR segment **2.14205 ms**, SR segment **0.362624 ms**. Boundaries are
  before external Submit, after external Submit, after SR plus input-state
  transitions. The SR segment includes those transitions; neither segment
  includes the later test readback. These five samples are not a performance
  benchmark, P95/P99, game result or proof of an RR cost-saving algorithm.

No full 40-gate replay, game launch, RR off/on/off player request, stable release,
tag movement or main/next merge occurred. Hosted CI is not started/not retried.
F7/F8 remain inherited. Manager keeps its separately pinned hotfix.1 payload.

## Remaining queue and independent branches

#153 still needs a bounded architecture experiment for loader-lock work and
first-device singleton assumptions. #139/#140 old exp1 evidence first fails at
null-PSO policy, not this OOB; #148's old runtime lacks its NGX provider. #121's
old flicker report lacks the rejected-frame command-state detail now available
in the current history; none is declared fixed by this batch.

#172 independently delivers 0.5-dev5 luminance/chroma (`e084a52` build,
`bd2b3b1` documentation): 72 production triples, old-reference byte equivalence,
full bilingual links; no NVIDIA/game quality claim. #173 independently delivers
corrected FG observations (`3d44af4` build, `22adf94` documentation): one SDK 2.12
development run reports interpolation enabled/extra presents; no generated-pixel
or display-cadence capture. Neither experiment is merged into this Core build.

Continue the [authorized code queue](CORE_CONTINUATION_QUEUE_20260908.md):
#11 minimal low-grid EditField is an independent successor to dev5; #14 is
evaluating an installed no-native-DLSS game with a real TAA/profile input source,
not pretending that the descriptor adapter alone adds game support. #162 remains
the maintainer-reported face-mask visual bug; no repeat capture is requested.
