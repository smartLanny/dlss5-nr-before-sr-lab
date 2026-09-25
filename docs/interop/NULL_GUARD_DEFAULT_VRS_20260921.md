# Observed default scalar VRS: bounded null-state candidate

Base: `303b2058411972c333b393001bc72ad5b6f95314`. Issue [#246](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/246).

## Evidence boundary

The private lockperf2 report records repeated `command-state-unverified`
pre/post/Present route changes during the running session, including seconds
before F8. One earlier detailed rejection has `dynamic=0x846`: viewport,
scissor, target binding and scalar shading rate. The same mask occurs in an
older d13 report. This establishes an existing admission boundary, not a new
regression's complete cause.

The latest F8 window itself has **one requested layer**, despite the report's
dual-layer description. Its 765 ms window has 31 successful NR evaluations and
31 transfers, no queue-unknown counter increase, and no start/end bypass. The
user's report that two layers flicker more often is separate testimony, not a
measured two-layer failure in this window. Previous-process log sections are
not evidence about this window. No frame-aligned visible flicker or scalar VRS
payload was captured. #302's face issue is unrelated.

## Small production change

`RasterNullRejectionReason` admits an **observed** `RSSetShadingRate(1X1,
NULL)` or `1X1` with two explicit `PASSTHROUGH` combiners, alongside the already
bounded raster subset. It still requires complete native hooks, a successful
fresh Reset, known untainted state, bounded arrays and owned target descriptors.

All nondefault rates or either nondefault combiner remain outside automatic
admission and receive `null-guard-shading-rate-not-default`. Shading-rate images,
including an explicitly observed null image, remain excluded. Unknown dynamic
bits, resource domains, query/opaque history and missing baseline evidence are
not relaxed. The raw/full null restoration policy remains experimental/off.

The existing setter forwards first and copies the rate and both combiners into
the lifetime-bound native record. CL5 hooks use QueryInterface and require the
exact native interface pointer; tear-offs and unexpected QI errors fail closed.
An unsupported interface installs no CL5 slots. Reset clears the observation;
capture uses the live registration token and scope restores on its resolved
native interface. The existing ClearState plus slot-77 restore is unchanged.
No queue submission, fence, list lifetime or resource ownership policy changes.

Microsoft specifies that null combiners mean two PASSTHROUGH values, and
ClearState returns scalar shading rate and the image selection to defaults:
[RSSetShadingRate API](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist5-rssetshadingrate),
[VRS specification](https://microsoft.github.io/DirectX-Specs/d3d/VariableRateShading.html).
This candidate does not infer optional coarse-rate support from CL5 support.

## Observation

F8 live and bounded historical snapshots now export scalar rate, both effective
combiners, and `explicit-array` versus `null-means-passthrough`. The observation
is explicitly `unobserved` or `unavailable` when appropriate; no default payload
is printed as an observed write. `deviceSupport=not-queried` prevents a setter
receipt from being mistaken for hardware capability. Formatting only reads the
existing snapshot, without device queries, runtime enabling or history advance.

## Verification and limits

`validate-command-state-native.ps1` now runs `--vrs-null` as part of its complete
entry. `-VrsNullOnly` runs just this bounded test. It uses actual WARP list
setters, hook publication, capture, scope restoration, Close and Reset; it does
not submit GPU work or load NGX. A separate ABI callback at slot 77 checks replay
arguments independently of the registry's final snapshot copy. Distinct
nondefault lanes are tested through the production observation hook with an
explicit no-op native trampoline; they are never issued to unsupported hardware.
The forbidden image observation uses the same boundary method. The composed
0x846 fixture uses an empty target set; existing owned-descriptor tests remain.

`-VrsNullNegativeControl` restores the old mask and must fail the admission
oracle. `-VrsReplayNegativeControl` removes the production slot-77 replay and
must fail the independent replay oracle. Missing observation, unpublished/fresh
Reset requirements, failed Reset, unsupported/failed/tear-off CL5 queries,
retired tokens, nondefault scalar state and forbidden image state remain covered.

The full native/WARP, null-reason, compatibility and default-off gates pass.
Actual production F8 coordinator/ZIP and combined bounded TXT/ZIP gates pass,
including unobserved/busy payload omission and both explicit combiner lanes.
An initial test incorrectly issued image VRS on WARP: Close failed. That failure
was retained and the unsupported image was removed from the native API path;
it is not counted as successful image-VRS validation.

These results establish a narrow, testable admission candidate. They do not
establish that the historical 0x846 game's scalar payload was default, that every
route change shared this cause, or that game flicker, dual-layer behavior, NGX
inference or final displayed pixels are fixed. No new game result is claimed.
