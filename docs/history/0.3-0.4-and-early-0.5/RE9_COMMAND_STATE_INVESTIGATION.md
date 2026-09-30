# RE9: first NR evaluation followed by compute binding crash

Investigation date: 2026-09-04, local time UTC+08:00. The initial sections
record the failure of the **8E candidate before the command-state fix**.
The final addendum records the intervening unsuccessful candidates and the
limited local acceptance of candidate **2869**. No further crashes are required
to establish the original failure signature below.

## Conclusion

Both Mode 2 and Mode 1 returned success from their first Feature 18 Evaluate,
then crashed at the same NVIDIA user-mode driver instruction while the game
was calling `ID3D12GraphicsCommandList::SetComputeRootDescriptorTable` with
root parameter index **2**. The immediate failure is a CPU null read in that
binding operation. It is not a recorded failure result from Feature 18, and
the available backtrace does not place it inside a native SR Evaluate or a
shader Dispatch.

The common command-list state boundary is therefore the priority. Source
inspection confirms that the plugin changes descriptor heaps and the compute
root signature, and the first-frame warmup return has no complete restoration
of the entering game state. The exact root signature and descriptor heaps
present at the failing call have **not** been recovered from these small dumps.
Consequently, “our codec root remained bound” or “NR left a particular root
signature bound” remain hypotheses, not established identities.

## Controlled runs and evidence

Candidate addon SHA-256:
`8E4D062093B27DC0266EC5A09F4927768E1EFD97639A2834CFB18BDFC7150436`.
The actual addon, NGX libraries, configuration and plugin log were loaded from
the game's `_storage_` directory. Editing only the similarly named root files
does not select the running configuration.

Local evidence is under `test-output/local-r3-20260904/re9/`:

| Run | Process | Evidence | Result |
| --- | ---: | --- | --- |
| NR disabled | 72972 | `8e-disabled-control/` | Main task observed a successful Continue/load into the street, approximately 60 FPS for over 40 seconds. The saved log shows `enabled=0`, NR count 0 and repeated SR-only samples. |
| Mode 2, NR → SR | 53908 | `8e-mode2-first-exit/` | Enabled 06:27:23.513; first NR Evaluate success 06:27:24.412; dump 06:27:25. |
| Mode 1, SR → NR | 72972 | `8e-mode1-first-exit/` | Same process as the successful disabled control. Enabled 06:30:31.961; first NR Evaluate success 06:30:32.702; dump 06:30:33. |

The disabled-control log lines 3 and 61–72 establish the setting and repeated
SR-only samples (approximately 59.7–60.0 FPS). The visual identification of the
loaded street and the 40+ second observation come from the main task's local
test observation; FPS alone does not identify which scene was shown.

Mode 2 log lines 78 and 212–219 identify activation, the R11-to-FP16 conversion,
NR creation and the sole successful Evaluate. Mode 1 log lines 76 and 210–218
identify activation, the native-output snapshot and the sole successful
Evaluate. Both had `WarmupAfterCreate=2`, `ExperimentalPeakShoulder=0`, intensity
1 and transfer 1; neither had reached Combine or an F7 stage capture. Changing
the highlight mapping or stage-export lifecycle cannot explain a stage capture
which did not run.

Shared environment recorded by dump/log:

- RE9 `1.3.1.0`; NVIDIA GeForce RTX 5090 Laptop GPU, driver `610.62`
  (`nvwgf2umx.dll` file version `32.0.16.1062`).
- ReShade `6.8.0.2155`, REFramework `dinput8.dll`, and
  `renodx-mfgunlock.addon64` `0.2026.903.310` were loaded. Their presence is not
  proof that either caused the crash.
- Native SR: 1920×1080 → 3840×2160, Feature 1 flags 11. Color and Output:
  `R11G11B10_FLOAT`; MV: 1920×1080 `R16G16_FLOAT`; depth:
  1920×1080 `R32G8X24_TYPELESS`.
- Live exposure: 1×1 `R16_FLOAT`; scalar PreExposure and ExposureScale both 1.
  Mode 2 NR operated at 1920×1080; Mode 1 NR at 3840×2160 with 1920×1080 guides.

## Exact dump comparison

| Field | Mode 2 | Mode 1 |
| --- | --- | --- |
| PID / exception thread | 53908 / 66548 | 72972 / 72432 |
| Dump UTC timestamp | 2026-09-03 22:27:25 | 2026-09-03 22:30:33 |
| Exception | `0xC0000005`, read address 0 | Same |
| Fault address | `nvwgf2umx.dll+0x6F8B55` | Same |
| Fault instruction | `cmpl $0x3,(%rcx)`; RCX = 0 | Same |
| RDX | 2 | 2 |
| R8 | `0x125678A0127D0A0` | `0x125678A017543C0` |

Dump hashes:

- Mode 2: `49372153d495b1ecc3b5c17a18cc767ae9621279f7ffe1838006c759716f5b75`.
- Mode 1: `f31a70b5df146c74c8f0bd89941f4dbbd2489be1308dbf439234b2946f10d4f4`.

Offline LLDB produced the same backtrace in both dumps, expressed below as
module offsets rather than misleading nearest-export names:

```text
nvwgf2umx.dll + 0x6F8B55
ReShade dxgi.dll + 0x12F9E0
re9.exe + 0x5865C16
re9.exe + 0xAF4048
kernel32.dll!BaseThreadInitThunk
ntdll.dll!RtlUserThreadStart
```

Raw stack references in the JSON report are separately marked **not unwound
frames**. In particular, incidental
`dxgi.dll+0x12FE63` and `D3D12Core.dll+0x23AF69` stack words must not be presented
as independently proved call frames.

API attribution is supported by the exact loaded ReShade binary, not by
LLDB's nearest exported symbol label:

```text
ReShade image RVA 0x12F9C0: wrapper entry
ReShade image RVA 0x12F9D0: movl %edx,%r14d
ReShade image RVA 0x12F9D3: movq 0x10(%rcx),%rcx
ReShade image RVA 0x12F9D7: movq (%rcx),%rax
ReShade image RVA 0x12F9DA: callq *0xF8(%rax)
ReShade image RVA 0x12F9E0: return address seen in the backtrace
```

The local `d3d12.h` declaration places `SetComputeRootDescriptorTable` at
vtable index 31, byte offset `31*8=0xF8`. The neighboring wrappers forward
`SetDescriptorHeaps` at `0xE0`, `SetComputeRootSignature` at `0xE8` and
`SetGraphicsRootSignature` at `0xF0`, corroborating the interface layout.
The ReShade wrapper retains the incoming root parameter index in R14D; the
dump has R14 = 2 as well as RDX = 2. The game frame immediately below is a
render-job virtual-call loop; there are no NGX/addon frames in this unwound
portion. This supports the game binding a compute table after NR processing
returned, not a Feature 18 Create failure.

## Source boundary and next implementation gate

In the pre-fix `src/nr_before_sr.cpp`, `DispatchCodec` sets the plugin heap and
compute root signature before binding table 0. `PrepareBeforeSr` returns on
the first successful warmup evaluation; `FinishAfterSr` similarly calls
`PreservePostSrOnFailure` and returns. In RE9's Mode 1 snapshot route that
preservation call does not issue the redirected-output copy. Neither exit
contains a complete restore of the entering compute state.

This is a concrete missing state-preservation contract. The dumps make it a
specific reproduction target, while not proving which particular cached
driver object becomes null. A driver fault location alone does not establish
a driver-only bug. Conversely, the mere presence of an exposure texture does
not identify it as the cause: this is a synchronous CPU descriptor-binding
fault, and no valid GPU-pixel or ExposureTexture-content evidence was captured.

The restoration design needs to cover the actual command-list identity and:

1. Compute root signature, pipeline state, both CBV/SRV/UAV and sampler heaps,
   compute root descriptor tables, root constants, and root CBV/SRV/UAV values.
2. Restore heaps and root signature before rebinding saved root arguments;
   changing either invalidates bindings. Track partial constant writes and
   root-signature changes accurately, including native calls observed through
   different wrapping layers.
3. All plugin exits after modifying the list: normal NR, warmup, identity,
   failed creation/evaluation, and post-SR copy/snapshot paths. Mode 2 also needs
   a correct boundary around the original SR call, not only final return.
4. Descriptor-heap changes can invalidate graphics descriptor-table state too.
   Preserve/rebind affected graphics tables when the shared heaps are restored,
   or establish a narrower supported contract before processing that list.
5. Do not restore a partial/unknown snapshot by fabricating state. Keep the
   existing native-SR fallback when full necessary state is not observed.
   Tracking resets with command-list Reset; replays and exact-fence object
   ownership must remain safe.

`WarmupAfterCreate=0` would only compare an NR-final-state exit with a
codec-final-state exit. It is not a repair and was not used for additional
crash attempts in this investigation. Likewise, blindly setting a null root
signature or adding a UAV barrier would not restore the game's contract.

Acceptance should first use a synthetic caller that binds a root signature
with table index 2, invokes each plugin exit, then reuses its cached state.
Verify full restoration and no unexpected descriptor writes in-flight.
Only after that gate should RE9 be retried with the original warmup setting,
both modes, the same save/scene and the same existing plugins. The disabled
control already passed; do not repeat uncontrolled crashes to rediscover it.

## Reproduction of the offline analysis

The local parser is
`test-output/local-r3-20260904/re9/analyze_minidump_metadata.py`.
It reads the saved dumps, computes hashes, checks stream bounds, and writes
`command-state-analysis/dump-metadata.json`. Optional `--lldb` accepts a local
LLDB executable and writes backtrace transcripts with external symbol lookup
disabled. It does not execute the game or upload dump data.

Application log queries found no Event 1000/1001/1002 after 06:20 during the
first inspection. The latest WER entries, at 04:21, reference older August /
September 1 watchdog dumps; their report time must not be misread as a GPU
failure associated with these two 06:27 / 06:30 crashes. The REFramework dumps
provide the relevant exception evidence despite the absence of a matching
Application Error event.

## Local acceptance addendum: candidate 2869

Candidate **2869 passed the local command-state compatibility reproduction**:
the existing save loaded into the street, both NR modes produced and submitted
visible-transfer work, switching back to Mode 2 and then disabling NR worked,
and the game exited normally. This is a passing execution/compatibility result
for the setup and scene below, not a general RE9 or HDR image-quality approval.

Earlier results remain separate:

| Candidate | Local run | Classification |
| --- | --- | --- |
| 8E | First successful NR Evaluate followed by the binding crash in both modes | Failed; exact dumps and disabled control are preserved above. |
| D97 | `r4-d97-disabled-menu/` | Disabled main-menu observation only; no active NR acceptance. |
| BFF | PID 31520, `r4-bff-live/` | Failed active-NR acceptance. Both modes repeatedly reported `unknown-indirect-signature`; Feature 18 and submitted-transfer counts remained zero. A normal exit while bypassing NR is not a fix verification. |
| 2869 | PID 50168, `r4-2869-live/` | Passed the bounded active-NR reproduction described here. |

The BFF `exit.log` demonstrates zero work at Mode 2 carrier request 6000
(line 681) and Mode 1 request 3600 (line 887); line 886 still reports the
unknown-signature bypass. These observations must not be merged with the
later passing candidate merely because both print `beta0.3.3-dev-r4`.

### Exact candidate and tested setup

The Chinese addon deployed in the game's `_storage_` directory had SHA-256
`2869D7EE12BCB73C4FADC4E2673A7BF3290FD2D90A294B2437E64B72C1BDD9E8`.
Its version banner is `beta0.3.3-dev-r4`. The tested environment was the local
RTX 5090 Laptop GPU with NVIDIA driver 610.62, NR runtime 310.8.0.0,
ReShade 6.8.0.2155, and the existing REFramework and MFG Unlock installation.
The ReShade log records MFG Unlock `0.2026.903.310` at line 48 and the GPU/driver
at lines 153 and 307. This run used SDR display output; ReShade line 309 records
`SetColorSpace1(ColorSpace = 0)`. The internal floating-point render resources
and Feature 1 HDR flag do not establish HDR display validation.

The main task visually confirmed Main Menu → Continue → the existing save's
street scene. Settings were intensity 1, transfer 1 for each mode, style 0,
LocalTone/LocalStructure 1, SkinStructure 0, AutoMask 0, ColorStrength 0,
PreserveLighting 0, ProxyScale 1, ExperimentalPeakShoulder 0 and warmup 2.
Native SR remained 1920×1080 → 3840×2160, with R11G11B10_FLOAT Color/Output,
1920×1080 guides and a live 1×1 R16_FLOAT exposure texture. Mode 2 NR ran at
1920×1080; Mode 1 NR ran at 3840×2160 through the existing FP16 snapshot route.

### Active execution and orderly exit

All line references in the following table refer to
`r4-2869-live/exit.log`, under `test-output/local-r3-20260904/re9/`.
Scene identification, the Alt+F4 action and the final process absence are the
main task's recorded local observations; log counters alone do not prove them.

| Local time | Observation and supporting evidence |
| --- | --- |
| 07:13:25.748 | Enabled Mode 2 (line 83). Native-state restoration is recorded at lines 251, 279 and 306. |
| 07:16:39.742 | Mode 2 scoped totals reached 10,788 successful Feature 18 evaluations and 10,760 submitted transfers (line 1003). |
| 07:16:51.052 | Switched to Mode 1 (line 1006). Its first three successful NR evaluations are at lines 1017, 1027 and 1030. |
| 07:18:59.257 | Mode 1 scoped totals reached 3,599 successful evaluations and 3,593 submitted transfers (line 1298). |
| 07:19:05.385 | Switched back to Mode 2 (line 1328); the new scope subsequently reached 2,399 successes and 2,397 submitted transfers (07:19:51.285, line 1445). |
| 07:19:55.194 | Disabled NR; the configuration record reports 17,757 cumulative Feature 18 evaluations (line 1448). |
| 07:20:58 | Main task used Alt+F4 and observed normal shutdown. Feature 18 retirement is logged at lines 1460–1461; the ReShade log records runtime destruction at 07:20:58.519. |
| 07:21:14 | Main task's process check found no remaining RE9 process. |

This establishes active NR and submission, not merely an enabled setting or a
successful menu launch. The saved session samples report lifecycle-bypass 0,
queue-affinity actual-mismatch 0 and observerGap 0. The final log contains no
unknown-indirect-signature fallback, lifecycle quarantine or device-removal
report, and no crash occurred during the observed sequence. Existing unrelated
MFG hook retry/refusal and ReShade reference-count warnings are still present;
the result is not a claim that every log message is error-free.

Evidence retained in `r4-2869-live/` includes `exit.log`, the configuration,
`ReShade.log`, the individual mode/return/off log snapshots, three labelled
screenshots, and both raw-stage captures. The screenshots at 07:16:39,
07:19:04 and 07:20:34 correspond to Mode 2 active, Mode 1 active and NR off.
They are separate frames and do not form a controlled pixel-identical A/B.

### Stage export integrity and remaining limits

`stages/validation.json` reports `PASS_RAW_EXPORT_INTEGRITY`:

| Request | Mode / NR frame | Sample grid | Export evidence |
| --- | --- | --- | --- |
| 1 | Mode 2, frame 8569, instance 2 | 1920×1080 sampled at stride 2 → 960×540 | Recorded 07:15:57.338; exported 07:15:57.492, log lines 867 and 888. |
| 2 | Mode 1, frame 12687, instance 5 | 3840×2160 sampled at stride 4 → 960×540 | Recorded 07:17:37.785; exported 07:17:37.970, log lines 1170 and 1184. |

Each request has `complete=1` and four same-NR-frame files: original, proxy,
neural and composited. All eight files have the expected dimensions and exact
RGB float32 payload lengths; all 12,441,600 stored channels are finite and
every stage contains nonzero image data. Each capture used a fresh context at
successful evaluation 64. That settling interval does not prove temporal
convergence, and file integrity alone does not prove every GPU fence invariant.

Both captures set codecFlags bit 64, but metadata does not contain the actual
sampled GPU exposure value. Scalar exposure values of 1 do not imply a texture
exposure of 1. Validation therefore explicitly skipped original-to-proxy
encoding reproduction and cross-domain luminance calculations. The two capture
requests also have different frames, modes and source sample grids; their
matching exported dimensions do not make them a mode or NPC A/B comparison.

The candidate added explicit support for indirect argument types 9, 10 and 11,
but the first 16 logged successful CreateCommandSignature descriptions contain
only types 0 and 1 (`exit.log` lines 34–49). The saved evidence does **not**
identify which additional type, if any, was used on RE9's NR carrier, nor does
it isolate that expansion as the sole cause of BFF → 2869 recovery. The
observed result is that the 2869 candidate runs active NR in both modes for this
sequence; the specific previously unknown signature remains unlocated.

Acceptance is limited to this machine, runtime/driver combination, SDR display
and loaded street scene. It does not establish all RE9 scenes, other plugin
combinations, HDR output, image fidelity, temporal stability, facial detail or
strength-response quality. Those remain separate validation work.
