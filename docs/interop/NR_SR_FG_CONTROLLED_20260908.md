# Controlled renderer: external Core NR before SR and existing FG

The fixed official renderer now submits its real HDR color, depth and pixel
motion to our external Core before its existing Streamline DLSS call. The
sample's camera/jitter, tonemapping, HUDless/UI tags, FG input fence and sole
presenter remain the existing renderer path. No game was installed or started.

The **actual adapter build source** is
`abc2952b60fb6a11effc4c3e85dae07a30912782`, against official sample
`dd6e1803b97e5b3f218f5776d85f09e4c386c34f` and fixed SDK 2.12 development
components. The reused Core is the separately frozen external-provider
`e7df0fcf26f28e9661a8dcfe0322377278820dd6` build. It is not relabelled as
containing the later history diagnostic or 0.5 algorithm changes.

## Implementation and meaningful failures

`scripts/prepare-fg-core-chain.py` validates the clean official source before
patching a separate external worktree. Our owned adapter is
`scripts/fg-sample-core-chain.h`; no third-party renderer or SDK is committed.
The complete MSVC Release executable linked. An initial shader-description API
constructor mismatch was corrected during the minimal compile; an ambiguous
NIS/tag patch anchor was narrowed before the final full build. Those attempts
and the first compiled binaries are retained separately.

The adapter uses one bounded scratch set: active R-sized RGBA16F color/output,
RG16F motion, and a real depth-SRV load into R32F. It preserves source pixel
motion units and actual PlanarView jitter; pre-exposure one describes this
sample's HDR target before tonemapping. It does not pass oversized allocation
padding as active input, synthesize guides, add a presenter, or evaluate NR on
generated frames. It supports a single controlled renderer/view.

The sample's immediate depth tag must see the state declared by the fixed SL
wrapper. The adapter establishes those states through NVRHI before tagging,
restores the source states after NR copyback, and clears NVRHI's binding cache
after direct Core recording. Its serial graphics-queue Signal/Wait follows
the actual submitted source batch; only completed batches license scratch
reuse, resizing and shutdown. A hard recording/fence failure exits this
controlled process without opaque retry or unsafe resource teardown.

Two actual integration failures narrowed the implementation:

1. The first SR-only run changed R from 371x209 to 427x240. The producer had
   incorrectly incremented MC1 ownership epoch for that allocation change, so
   later frames correctly returned `epoch-mismatch`. Ownership epoch now stays
   fixed for the claimed view session; resource changes request temporal reset.
   No consumer epoch guard was weakened. The old process exited normally but
   is not recorded as a passing producer contract.
2. With NR enabled, all 31 source frames then returned `lease-invalid` before
   queue training. Fixed SL queues return a proxy device, while this sample's
   non-proxy command lists and resources return the native device. The producer
   now uses official `slGetNativeInterface` for device/queue/current list, with
   the SDK's returned AddRef adopted exactly once. The launcher fixes
   `forceProxies=false` and `trackEngineAllocations=false`; this helper is not
   a generic ownership guarantee for unknown SDK configuration. The Core's
   same-device check and two real submission requirement remain intact.

The failing runs, original logs and superseded binaries remain private. Native
identity receipts and subsequent real queue observation/NR success support the
second diagnosis. No new queue marker, fake completion or relaxed admission was
used. Initial affinity-training and NR warmup frames remain labelled bypasses.

## Final NVIDIA matrix

All four final cases use the same executable, SDK components and Core, with
explicit NR/FG switches. Output is 640x360, DLSS quality mode; the renderer
settles at R=427x240 after its first source frame. The sample requests 30 or 60
frames and its existing stop condition produces 31 or 61 observed source frames.

| NR | FG | Source frames / completed batches | NR writebacks recorded | SDK samples reporting 2 presents | SDK since-query present total |
| --- | --- | ---: | ---: | ---: | ---: |
| Off | Off | 31 / 31 | 0 | 0 | 0 |
| On | Off | 31 / 31 | 27 | 0 | 0 |
| Off | On | 61 / 61 | 0 | 59 | 118 |
| On | On | 61 / 61 | 57 | 59 | 118 |

Every final process exited 0 without watchdog timeout or reported device/async
DXGI error; each source frame had one consuming SDK state query. The NR-off
cases all reported `master-disabled`, with zero NR writeback. The NR-on cases
contain honest affinity/warmup bypasses followed by real NR evaluation and
completed writeback. No sample process remains.

Core uses its existing same-extent input/composition helper for external frames;
some internal log labels therefore say `NR-after-SR`/mode 1. Here the external
resource is the renderer's **pre-SR HDR input**, and the actual caller records
NR before the subsequent DLSS evaluation. This does not validate mode-2-only
0.5 filters through this external path. Core's existing sampled color diagnostic
also observed changed neural/composited values with zero invalid samples in
its 112-point sample; it is not a full-frame visual or quality result.

The FG-on cases establish SDK-reported extra presentations during actual
NR+SR operation. **Generated-frame pixels and physical display cadence have
not been captured.** The FG-off counter zero means FG's counter is inactive,
not that no base frames were displayed. Serial waits, capture/logging and
startup dominate this diagnostic; no FPS, latency or performance gain is claimed.

## Identity and reproduction

| Artifact | SHA256 |
| --- | --- |
| Final sample executable | `55c1d42b1c101d7f3b01c04c4effcbb05284931e021184177f4c6a4a089fa2fd` |
| Final external source manifest v4 | `1343342c569bc055a34ad8d414bc2738447df1d89c980fbf94c93c6d93df74d6` |
| Final sample link log | `e618374ea7f9fae5dfc6602da5a9bc09810542a6c294b3244bec02675b8be50b` |
| Final source/artifact/matrix verification JSON | `483658643bae1cd8701bea6ecb8b2fa31939e14ed9624cdc89b5d892afa9cb01` |
| Reused Core | `8e9394889c3fd3309e035bc67e26d4d25c5b711e4a392f86f5e1c89a5a1caf38` |
| Off/off run JSON | `c225c167970f17b4d23bd527a1b2af952bf33445824909cf48e1eb39e3fc801d` |
| On/off run JSON | `2aac4dabfee9fe253a978e5a5bf6de202f347c65803d044d86fca00ee2d93b2a` |
| Off/on run JSON | `71c310765ab40458f1cecc6bf045306105561d7e55b214111fba4f483842ac3d` |
| On/on run JSON | `dc0aacec8b7c499e042101e9d257810ba119329b9b2f7103b3a70620a1827602` |

Private `nr-sr-fg-20260908-r3-*` directories retain each run JSON, source hashes,
loaded modules, SDK identities, original sample/Core logs and configuration.
The external sample worktree retains all source manifests/build logs and the
separate superseded executables. No SDK, model, raw log or local path is pushed.

The staging manifest keeps its original `compiled=false` declaration. The
separate final verification above records the linked executable, rehashes all
four patched sources and four owned headers plus the SDK CMake shim, and checks
each final run's executable/source/log identity and completion counts. It does
not rewrite the staging declaration as a later build receipt.

Before pushing, the new source/script/document files passed diff, payload and
private-path/credential inspection. The private repository still contains four
historical feedback ZIPs inherited from `main`; these are preserved, not a clean
public export. No new feedback archive, SDK, binary or raw capture is added.

Prepare the pinned clean sample with `prepare-fg-core-chain.py`, configure its
CMake build with D3D12, DLSS-SR/FG/Reflex enabled and other features disabled,
and pass the existing fixed `STREAMLINE_SDK_ROOT` and DXC cache. Then:

```powershell
./scripts/run-fg-sample.ps1 -SampleRoot $Sample -OutputRoot $NewEvidence `
  -DevelopmentSdkRoot $FixedSdk -CoreDirectory $FrozenExternalCore `
  -EnableSR -EnableNR -EnableFG -Frames 60 -AllowFocusedWindow
```

Omit NR/FG switches for their controls; `-PrepareOnly` stages without execution.
This is a controlled renderer with Core registration stubs, not a game/release
package, full ReShade integration, multi-view or cross-API FG support. #167/#16
remain open for generated-pixel/cadence, recovery and actual game integration.

## Subsequent OS display capture preparation

`scripts/capture-fg-display.ps1` at `202ab380a53a146bd008ca763c87b2716d32cc23`
prepares a separate, fixed PresentMon 2.5.1 capture around the already frozen
sample executable. It uses a unique owned session, excludes input tracking,
keeps logs/CSV private and stops before launching the renderer if the collector
fails at startup. It never requests elevation or changes account membership.
The launcher records its own hash separately from the old sample build identity.

The cached Intel-signed tool's SHA256 is
`9bec3083069f58f911e6a512f4806db51a27bd096103087bc1d05ef54c80a191`.
The one standalone collector preflight exited **6 / access denied before trace
startup**, with no GPU sample started. Stderr SHA256:
`3bec4df2ce8a9880475dfff83efb4bb5b616f929ddbd74edd6eb30de984cb34e`;
process result SHA256:
`17dd2ab5a08a6712e0b06d064b851e8a8c8481e3326ecf966a206ce10ff47935`.
The final wrapper passed parsing and PrepareOnly; prepared plan SHA256:
`85a7d16b423288f8d19cbc8add94e40845261d89cc856b6b73fb39e13999b755`.
Its actual capture/flush path remains unexecuted, not a passing display test.

PresentMon requires administrator access or membership in Performance Log Users
to start this trace. Its frame-type field also requires application/driver
instrumentation. See the fixed official [access requirements](https://github.com/GameTechDev/PresentMon/blob/v2.5.1/README.md#user-access-denied)
and [console metrics](https://github.com/GameTechDev/PresentMon/blob/v2.5.1/README-ConsoleApplication.md).
Once run in an authorized tracing session, CSV rows must be correlated by PID,
swapchain and time with the source receipts; unknown frame types stay unknown.
OS display events would still not be generated-pixel captures or a physical
screen/latency measurement. No additional renderer matrix was run after denial.
