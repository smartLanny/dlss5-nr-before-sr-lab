# beta0.3.3.5 validation

This revision starts from `9085a4d67e32b8a6c83bc184b80ebe26b59c207d`.
This document records implementation checks and bounded local acceptance.
Final binary identities and game results are below; package SHA-256 and build
evidence accompany the local release. An earlier build with the same version
banner is not interchangeable with the final artifact.

## Regression evidence and bounded correction

The supplied NTE / 异环 logs establish an active-NR regression, not a demonstrated
crash. The newer log identifies itself as `beta0.3.3-dev-r4`, repeatedly reports
`pipeline-unbound`, and reaches 5,400 SR carriers with zero successful NR and zero
submitted transfers. The comparison log identifies itself as
`beta0.3.3-dev-r2` despite its older filename label and reaches 2,396 successful
NR evaluations / 2,394 submitted transfers. These files do not establish equal
runtime binary hashes or equal driver versions between runs.

The existing state guard treated every absence of an ordinary/DXR PSO as unknown.
D3D12 permits a successful `Reset(allocator, nullptr)` and installs its dummy
initial pipeline. A native WARP/MinHook regression exercised that real Reset
entry followed by a viewport setter and capture: the old source rejected it
(47 checks, one failure); the new source preserves that observed baseline.
This proves the corrected API seam. It does not prove that the user's NTE carrier
has this exact provenance or that the final addon works in NTE.

The new admission requires an observed successful native Reset or ClearState
with a null initial PSO. Zero pointers in an unobserved or failed-reset record
do not establish this evidence. The existing DIRECT-only, module-pinned hook
publication, meta-command dirty-domain, token/lifetime and exact-fence guards
remain active. No main processing-order integration change is required.

For this known-null case, restoration uses `ClearState(nullptr)`, followed by
the frozen heaps, roots, arguments and recorded dynamic graphics state. It never
calls `SetPipelineState(nullptr)` or invents a renderable PSO. The game still
binds its first real PSO before its subsequent draw/dispatch. The scope before
native SR restores before that SR call; the post-SR scope restores a separate
snapshot of the state native SR actually left.

Dynamic coverage includes IA topology/index/vertex views, viewport/scissor,
blend/stencil, stream-output views, render targets/depth target, predication,
CL1 depth bounds/sample positions/view mask and CL5 shading-rate state.
RTV/DSV CPU descriptors are copied at the binding call into owned descriptor
heaps and independently copied for snapshots: a game may immediately overwrite
or release its original CPU descriptor storage. CL9 depth bias/strip-cut values
are replayed after the real PSO, whose setter resets those values. Their presence
without an ordinary PSO remains inadmissible.

Any protected-resource-session setter, including a null argument, invalidates
the record because the API clears command-list state. An active render pass,
ExecuteBundle, unknown indirect signature, work-graph SetProgram or unobserved
state remains refused until a real recovery boundary. Optional higher command
list interfaces use checked official IIDs/slots and must preserve native object
identity. No general reconstruction of opaque NGX or work-graph state is claimed.

## Recorded automated results

The following checks have passed during integration. Command-state results were
run by the state implementation owner against its frozen source; other component
counts were supplied by their implementation owners. Final artifact qualification
is a separate step and must be recorded below rather than inferred from this table.

| Check | Recorded result and scope |
| --- | --- |
| Native command-state / MinHook | 62 checks, zero failures. Actual Reset/ClearState, failed publication, native identity, strict wrapper separation, indirect affected roots and IA bindings, unknown-state refusal and recovery. |
| Small-stack gate | Two simultaneous known-null scopes repeatedly run on a 64 KiB worker stack; 195 command-state functions each use at most 4,096 bytes according to compiler stack reports. |
| Command-state WARP output | 16 scenarios pass: 12 prior controls/restores plus two known-null controls and two known-null restores. Actual compute, graphics and plugin output readbacks are checked. |
| Command-state negative controls | Three intentional faults are rejected by the independent output oracle: wrong root constants, omitted CPU RTV/DSV descriptor copy, and a wrong but valid viewport. |
| Colour policy | 164,834 checks pass. Policy admission/effective settings are tested; these are not perceived image-quality results. |
| Diagnostic ZIP helper | 104 checks pass. Archive contents/integrity are checked separately from game screenshot delivery. |
| In-game feedback collector | 168 checks pass using extracted production logic, real Windows callback-thread publication, path canonicalization and ZIP integration. This is not exhaustive concurrent-interleaving or live ReShade coverage. |
| Offline feedback collector | 38 checks pass against the shipped PowerShell collector and fixtures. |
| Codec GPU / shaders | 54 WARP codec scenarios and four shader compilations pass. Fixed expected exposure values include NaN, infinity, zero, normal textures, clamp endpoints and scalar input. Capture enabled/disabled outputs agree bit for bit; Feature 18 model behaviour is not exercised by these codec tests. |
| Stage export / lifecycle | 2,027 CPU export checks and 164 lifecycle checks pass. |

The new known-null GPU scenarios consume actual vertex/index buffers, viewport,
scissor, topology, blend factors, stencil reference, RTV/DSV bindings and root
arguments. They overwrite the original CPU RTV/DSV descriptors after binding,
record plugin work, restore the snapshot, then bind the game's first real PSO
and verify its output. These tests demonstrate that the added graphics coverage
affects real GPU results; they are not just comparisons of the shadow record.

The D3D12 debug layer was unavailable in the local WARP environment. The passing
GPU output checks must not be described as a clean debug-layer run, hardware
driver qualification or full support for every extended command-list method.
An independent source review found the protected-session, CL9 reset-order and
CPU-descriptor-lifetime issues during implementation; all three were corrected
and reviewed again before freezing the state module.

Reproduction entry points are `scripts/validate-command-state-native.ps1` and
`scripts/validate-command-state-gpu.ps1`, using the repository's configured
LLVM-MinGW toolchain. The latter accepts `-NegativeControl`,
`-CpuDescriptorNegativeControl` and `-DynamicStateNegativeControl` separately.
Negative-control mutations apply only to generated validation units, not the
production source.

## Colour controls and feedback evidence

The Wuthering Waves feedback showed a requested colour-follow value of 1 with
an effective value of 0 under the unverified automatic HDR NR-before-SR contract.
This is an existing protective policy, not evidence that the UI request reached
the shader. The revised controls and records expose requested/effective values,
their policy reason, and whether a recent Combine was actually recorded. This
change does not broaden the unknown HDR gamut/exposure contract or prove that
the weak-detail or intensity-1-versus-2 report is fixed.

The explicitly requested feedback session keeps the current settings, saves up
to three current-chain display screenshots, collects logs/settings/module hashes
and requests one exact-fence NR stage export when eligible. It saves locally;
missing items and timeout causes remain visible in the ZIP. It does not perform
a controlled A/B sequence or upload files automatically.

The stage GPU allocation now contains four planar RGBA32F sampled images plus
a 16-byte exposure trailer. Pixel `(0,0)` in the same Combine dispatch writes
raw external exposure (when texture sampling is active), resolved exposure,
source-to-canonical scale and pre-exposure. The exporter reads that trailer only
after the exact GPU fence; `stages.rgba32f` contains just the four image planes,
while metadata contains the four exposure values. Raw/PFM domains, source and
destination offsets, dimensions, codec flags and effective controls remain
explicit. The composite is captured before destination-format quantization.

The exposure trailer is a Combine sample, not an immutable shared Encode/Combine
exposure snapshot. It does not establish the value seen by Encode, guarantee an
invalid-sample temporal-history reset, or prove a dynamic-exposure fix. Display
screenshots and NR stages have independent frame identities. Capture recreates
NR history, waits at least 64 successful evaluations and then retires that owner
to drain; 64 frames are a settling interval, not verified temporal convergence.

## Remaining game and quality acceptance

- **NTE / 异环:** collect the final addon identity and a new active-game log.
  Confirm whether a known-null snapshot is observed, successful NR/submitted
  counts grow, both processing orders remain usable, and native SR continues.
  A normal exit with zero NR is not a passing result.
- **Cyberpunk 2077 and The Witcher 3:** retain the earlier r4 results as historical
  evidence, then identify any final-candidate rerun separately. Use fixed-camera
  controls for SDR/HDR, colour follow and NR intensity; a working toggle or
  nonzero NR counter alone does not prove stronger detail than RenoDX.
- **Wuthering Waves and Spider-Man HDR:** inspect requested/effective policy,
  encoded proxy versus neural stages and final composite before attributing
  small facial/colour changes to a failed intensity setting. Unknown HDR colour
  contracts remain bounded by the existing guard.
- **Other SDR feedback:** require matched settings, actual instance intensity,
  stage identities and repeat A/A versus A/B captures. Scene motion and fresh
  temporal history can otherwise dominate the measured difference.
- **Helldivers 2:** no new claim of compatible DLSS SR, bridge integration or
  validated post-NR quality is made by this revision.

## Exposure fallback and live collector regression

A GPU regression supplied a NaN ExposureTexture with PreExposure 0.25. The
previous floating-point self-comparison was optimized away by the HLSL compiler:
the resolved exposure became 1/1024 instead of the intended fallback 1. The
production shader now checks IEEE exponent bits before range handling. Fixed
expected trailer values and capture-on/off output checks pass for all 54 cases.
This corrects invalid-sample fallback; it is not an immutable frame exposure
snapshot, a temporal-reset fix, or proof that community facial-detail reports
are resolved.

The first local F8 build (Chinese SHA prefix `46A7B373`) crashed inside ReShade
when a C++ resource wrapper was returned across the MSVC/MinGW ABI boundary.
The dump resolves the call to `get_current_back_buffer()` and the write address
to 0x2. The collector no longer calls this method or `get_resource_desc()`;
display format is sampled with the native COM GetDesc out-parameter. A second
live pass exposed ReShade paths containing `.\\`, which the ZIP allowlist refused.
Canonicalization before allowlist validation fixes that case without weakening
the file-path checks. Both issues have native seam regressions; final live
acceptance is recorded below.

## Final artifact qualification

Two clean builds produced byte-identical English/Chinese/bridge outputs. The
final candidate was compiled against ReShade API 18 and tested in the game with
the same Chinese binary identity:

| Output | SHA-256 |
| --- | --- |
| Chinese | `080D4B1AD99FDF1BED4A4928A01FD81711EEF7426B1FAAADDD4A3C137E840B60` |
| English | `CBC39A9CFCFFF259535F11F8E7293C3F3DA0FE22D2C2C75792ACD2FE561B8358` |
| Bridge | `46041A5FF91AE2FD907E310D132AABC3C4A1ECD48DACE511B8672909D5D9C2FB` |

All three LLVM analyzer targets pass with zero diagnostics and unchanged source
fingerprints. Four cs_5_0 shaders, stage CPU (2,027), lifecycle (164) and diagnostic
identity checks were repeated against the frozen candidate. Compiler warnings
are recorded in the build logs; this is not a warning-free compilation claim.

Local evidence is under `test-output/beta0.3.3.5`, including both clean-build hash
lists and `analyzer-20260904-171747`. Source snapshots, package integrity and live
session acceptance are recorded with the delivered local artifacts.

### Final-candidate Cyberpunk 2077 run

The `080D4B...` Chinese addon ran in process 54512 with RTX 5090 Laptop GPU,
driver 610.62 and the original NR 310.8 runtime. The display contract was
R8G8B8A8_UNORM / SDR (3840x2400); internal HDR flags are not treated as proof of
display HDR. This was an existing save at a bar/arcade, with a stationary camera
and animated NPCs. It is functional acceptance, not a controlled quality A/B.

| State | F8 outcome |
| --- | --- |
| NR -> SR, 2560x1600 input | Three readable 3840x2400 PNGs; four 640x400 sampled PFM planes and RGBA data; required files complete. Request 1, instance 2, NR frame 2406. |
| SR -> NR, 3840x2400 input | Three readable 3840x2400 PNGs; four 480x300 sampled PFM planes and RGBA data; required files complete. Request 2, instance 5, NR frame 5594. |
| NR off through F6 | Three display screenshots and logs packaged; NR stages explicitly absent because NR is disabled. No automatic enabling occurred. |

Both enabled-mode ZIPs passed independent CRC and SHA checks for all 16 copied
files. All four PFM RGB planes match the corresponding raw RGBA planes bit for
bit; RGB and alpha are finite. All four GPU exposure values are 1, consistent
with the recorded no-external-texture/scalar contract. Twelve loaded module
file hashes were independently compared while the process was still running.
The only missing entries in these two packages are the documented optional raw
guides and extra same-frame native-SR readback. Sampling stride and image-domain
metadata must be honored; the PFM files are not full-resolution screenshots.

Before disabling, the log recorded 6,112 successful NR evaluations; the earlier
6,000-carrier checkpoint recorded 5,970 submitted transfers. Both processing
orders continued to submit. There was no new REDengine crash report after the
intermediate `46A7...` failure. ReShade recorded `Finished exiting` at 17:25:08,
and the process was absent afterward. Its existing inconsistent-reference and
add-ons-still-loaded shutdown warnings remain recorded; a warning-free exit is
not claimed.

The user stopped Computer Use with physical Escape. No further UI automation
was issued. RE9 was prepared but not launched, so this revision has no new RE9
acceptance. Ten temporary addon/config files across Cyberpunk and RE9 were
restored and checked against fresh backup SHA-256 values. No test process remains.
Evidence: `cp-final-080d/{mode1,mode2}`, final logs, and `restored-games.json`
under `test-output/beta0.3.3.5`.
