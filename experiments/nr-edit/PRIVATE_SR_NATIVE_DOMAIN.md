# Beta 4 native residual SR contracts

The producer captures original NGX parameters at the native read boundary, before
native Evaluate. NVIDIA's DLSS Programming Guide (31 March 2026), section 3.7.3,
page 23, defines jitter as the viewport/projection displacement in render pixels,
using the motion-vector coordinate directions. Its bound is [-0.5, +0.5].
Source: https://raw.githubusercontent.com/NVIDIA/DLSS/main/doc/DLSS_Programming_Guide_Release.pdf

Consequently a zero-phase N output sample reads the native raster at
`floor((n + 0.5) * R / N + native_jitter)`, clamped to the active rectangle.
This sign is independently exercised by the synthetic scene producer (scene
values at raster pixel `p` use `scene(p-native_jitter)`). The CPU/WARP atlas
oracle checks both positive and negative offsets and separate resource origins.
Depth and low-resolution MV select the same local sample. No cross-surface
bilinear MV blend is introduced. Displacements use original MV.Scale * N/R.
Native MVJittered remains refused; nonzero jitter with output-resolution MV
remains refused because its field sampling phase needs a separate contract.
Legacy NR JitterMode neither supplies nor changes this native phase proof.

Flags 75 means HDR, low-resolution MV, inverted depth and AutoExposure. Native
HDR B and final Z remain in the same FP16 native output value domain. Extraction
subtracts their raw values once, and composition restores B once. It neither
infers a transfer function from FP16 nor applies native exposure again. Private
Feature1 sees the bounded carrier with fixed exposure, not the native HDR frame.
Zero edits preserve B bytes. This is not a claim of noise reduction.

Successful original Init and successful original central Core Create produce
separate evidence kinds. The latter is ObservedInitializedDomain and permits an
RR-only parent without manufacturing a native SR parent or issuing another Init.
Receipts bind exact device, actual central Create export owner, epoch and observed
handle generation. Native wrapper aliases are sealed only after registry linkage.
Shutdown and Shutdown1 invalidate before original forwarding. A concurrent
shutdown invalidates an in-flight Create ticket. Private TLS calls never produce
game receipts; stale epochs cannot allocate, evaluate or release via old exports.
An unobserved already-existing feature does not establish this proof.

Validation layers are distinct: guide CPU/WARP; controlled exported factory and
whole-Core lifecycle; real NVIDIA native SR/RR + signed Feature18 + private SR;
and game acceptance. The controlled factory is explicitly not NVIDIA inference.
The real-model host uses native source/Capture/Finish, and directly assembles the
production NR model/codec helpers without invoking Present. Per-layer readback
checks full predecessor output transfer; the final B/Z pair is processed once.
No game is started by these validators. Physical GPU tests are serialized by the
root executor and must be reported with their actual result, not inferred from
compilation or WARP success.
