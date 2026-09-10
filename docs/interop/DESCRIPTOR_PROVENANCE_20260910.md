# Bounded descriptor provenance and explicit native use

Final canonical validation on source `462e61d7617b2f8d15b6e35587d87cfe17275bca`
passed 560 registry CPU checks and 235 full-Core WARP checks, including actual
resource destruction invalidating the route without a subsequent Evaluate.
The complete candidate passed 71 local acceptance gates. The earlier overlay
results below remain component evidence; see the [final handoff](COREFIX3_COMPARISON_20260910.md)
for source identity, full F8 validation and game-verification limits.

This change adds read-only evidence for cached D3D12 descriptors. It does not
promote cached views into the native format admission ledger, choose UNORM from
hardware support, alter selected typed formats, change resource states, or grant
NR permission. A game result remains unverified until separately observed.

## Evidence contract

`nr_descriptor_provenance.h` owns bounded value records: 256 heap lifetimes,
2,048 resource lifetimes, and 8,192 sparse descriptor slots. Heap CPU/GPU ranges,
descriptor revisions, resource identities, view kinds, typed formats and simple
Texture2D subresources are recorded without retaining graphics-object pointers
for dereference. Metadata notifications attached through `SetPrivateDataInterface`
retire records when the actual COM object is destroyed. They do not own the
device, heap or resource. GetPrivateData's temporary interface reference is
released; this follows the [D3D12 private-data contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12object-getprivatedata).

The existing SRV/UAV hooks now bracket descriptor writes with provenance
transactions. An additional optional hook batch covers descriptor heap creation,
RTV/CBV creation, CopyDescriptors and CopyDescriptorsSimple. CBV/null/unknown
overwrites invalidate an earlier UAV/SRV value. Source changes during copying,
overlapping writers, lifetime retirement, address reuse, cache eviction and
capacity loss stay unknown. Copies retain an explicit source revision; no opaque
descriptor memory is read. A copy covers at most 64 ranges and 256 descriptors;
larger or malformed ranges invalidate provenance and still forward the original
native call unchanged. The bounded copy record is allocated off the worker stack.

An independent native-call receipt records an explicit Output UAV clear only
when the CPU descriptor is from a current nonvisible heap and the GPU descriptor
is from the current bound visible heap, both have the same live device/resource,
typed view and subresource, and the command list matches that native call. These
are separate heap lifetimes; D3D12 requires a nonvisible CPU clear descriptor and
a visible bound GPU clear descriptor. See the
[ClearUnorderedAccessViewFloat contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-clearunorderedaccessviewfloat).

Each native call retains at most four descriptor pairs; a 64-entry history
supports later Execute correlation. Revisions are checked after native return
and at the intercepted Execute entry. D3D12 volatile descriptors may legally
change between recording and submission, so a native-return receipt alone is
not used to authorize the next frame. The distinction follows
[Root Signature 1.1 descriptor volatility](https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signature-version-1-1).
Execute return, CPU recording order and an observed clear never prove final
consumer ordering or presented pixels.

F8 exports the latest native-call receipt, at most four pairs and registry
counters. A separate current Output inventory has at most six kind/format/count
groups and is explicitly `cached-only-not-use`. It is collected during F8, not
by scanning the registry every frame. If native Evaluate has no explicit clear,
the report says so while preserving any independently observed cached views.
Views made before observation, worker/private-list use without a matching call,
root tables, bindless shaders, render passes, and unobserved writer paths are not
inferred. Existing format coverage is labeled `admission observer` to distinguish
the old synchronous creation evidence from these new read-only diagnostics.

## Output boundary evidence

The same registry resolves CPU RTV bind/clear handles at recording time. Exact
Output matches, known-other resources and unknown handles are distinct. Known
other RTV binds/clears have separate saturating counters and never become Output
events. A clear can describe a full known view, one exact rectangle, or the
envelope of at most 64 valid rectangles. An envelope is not the exact union and
is not evidence of full coverage. Unknown shapes, out-of-view rectangles or
exceeded bounds remain unknown.

Each observed Execute array now retains its actual zero-based list index and
array count for the origin and matching events. No cross-queue order is inferred.
The existing six native events, twelve following events and eight frozen history
windows remain bounded. The production window is 4,000 bytes, below its 4,096-byte
worker-stack guard; closed or retired windows cannot gain later operations.

When route compatibility is included, `NrDescriptorTrackRouteResource` reuses
these notifications for route semantic IDs. Actual resource/device retirement
calls the route's value-only invalidation function after descriptor locks have
been released. Color, Output and guide attachment policy belongs to the route
module; this observer does not create a second ownership system or route policy.

## Focused validation

The reusable gate is:

```text
python scripts/validate-descriptor-provenance.py --compiler <clang++> --reshade <ReShade-source> --source <stage/core/src/nr_before_sr.cpp> --output <new-output-directory>
```

`--cpu-only` runs the production registry without graphics. `--compile-only`
builds without executing GPU work. Development-only `--overlay-root` explicitly
records an overlay and preserves staged adapters; canonical validation omits it.
The gate saves the original staged source/manifest hashes, every overlay and
compiled source input hash, fixture hashes, exit codes and binary hashes.

On 2026-09-10 the component overlay over stage source `7c882545` passed 560 CPU
checks and 232 real WARP checks. CPU cases include copied views, overwritten
sources, slot/heap/resource reuse, lifetime loss, ambiguous types, bounded
capacity, 256 controlled overlapping-write races, and clear extents. WARP uses
real hooked D3D12 descriptors, cached CPU-to-GPU copies, a scripted native callback
issuing an actual UAV clear, actual RTV bind/clear and Execute arrays, exact-fence
completion and stored pixels. Known-other clears do not contaminate Output
events. Real unused resource/heap destruction invalidates receipts, and cached
native use leaves the admission ledger empty throughout.

The existing native-format host regression passed 1,161 checks. Existing output
boundary CPU callbacks and real MinHook/synthetic-COM tests passed 22 and 28
checks. These results are component evidence, not an unchanged frozen-candidate
test, NVIDIA NR result, game result or performance comparison. The final canonical
gate additionally enables a guarded real-COM resource retirement test for route
invalidation without another Evaluate; that combined case awaits integration.
