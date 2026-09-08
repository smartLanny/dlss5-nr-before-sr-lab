# #167: official D3D12 FG-only sample, generation not yet verified

The official renderer was built and run on the RTX 5090 Laptop GPU, using its
real scene camera, animated objects, depth, motion, HUDless/UI tagging, frame
tokens, Reflex callbacks and swapchain recreation. NR is off; no Opti/RenoDX NR
or substitute Core is involved. This is a controlled sample, not game injection.

The sample initializes Streamline before device/swapchain creation, loads the
DLSS-G backend, renders and presents 121 source frames, and shuts down cleanly.
However, **no additional presented frame was observed**. This is not completion
of FG injection or proof of generated pixels/correct display pacing.

## Fixed sources and actual binaries

- Official [Streamline_Sample](https://github.com/NVIDIA-RTX/Streamline_Sample/tree/dd6e1803b97e5b3f218f5776d85f09e4c386c34f),
  commit `dd6e1803b97e5b3f218f5776d85f09e4c386c34f`, including its Donut/NVRHI renderer.
- Official [Streamline SDK v2.12.0](https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.12.0),
  archive 231958617 bytes, SHA256
  `f5c0a3d870707dddc3570fb4bcd3655cf48a8a68c3a9d342910cfa21b77dcf48`.
- MSVC 14.44.35207, Windows SDK 10.0.26100, CMake 3.31.6; ShaderMake downloaded
  its pinned DXC v1.8.2505. D3D12 enabled, D3D11/Vulkan disabled; FG/Reflex/PCL
  enabled, SR/RR/NIS/DeepDVC/NVPerf/plugin-ImGui disabled.
- Final instrumented Release executable SHA256
  `4ac11ef25cef621c71406dea811e5e6dd7bf533935b9c2e7861ade29f780b32`.
- Runtime driver components report `32.0.16.1062` (610.62). Actual module paths
  and all module hashes are retained only in private `run.json` evidence.

| Component | Actual version | SHA256 |
|---|---|---|
| sl.interposer.dll | 2.12.0.0 | `2a79db6857ae8c75bbd871a9489c48bc6a39f7fcc88b9b02afd53d0376cbec66` |
| sl.common.dll | 2.12.0.0 | `c57930ef5a8a3fe9be85efdf71a61d8107c1148e8a6aed456464547128f7f4ae` |
| sl.dlss_g.dll | 2.12.0.0 | `1fec3f8fdfc59d78c4445c276c1a0fb798bf251985f348597dc2b44d0c995e52` |
| sl.reflex.dll | 2.12.0.0 | `7e6e4ccc4b561bd449fb0da90709d9b96b08c3f6f4697362caaa359e72a58a67` |
| sl.pcl.dll | 2.12.0.0 | `699ab461e64e95189a7fe6a21c79ad237cf56b60ea748cb6c840cd5431ba91d1` |
| nvngx_dlssg.dll | 310.7.0.0 | `135eaf0733c1e37381a8c28abcf7a862404a54132b81787c04e35d09efc5e36f` |

All six SDK binaries have valid NVIDIA Authenticode signatures. The runner checks
both signatures and exact SDK hashes. No SDK binary, model, renderer source,
scene asset or private raw log is added to this repository.

## Final isolated hardware run

The final run started after the other hardware test had exited. It used a
640×360 focused window, animated Sponza scene, Reflex On with a 60 fps cap,
DLSS SR Off, FG On, one requested generated frame and `maxFrames=120`.
Frame indices are inclusive 0–120: 121 actual rendered source frames.

| Observation | Actual result |
|---|---|
| Process exit / watchdog | 0 / no timeout |
| GLFW focused source frames | 121 of 121 |
| `slIsFeatureSupported(DLSS_G)` | `eOk` (0) |
| `slDLSSGGetState` return | `eOk` (0), 242 samples |
| `DLSSGStatus` | `eOk` (0) |
| Minimum extent / maximum generated | 100 / 5 |
| Requested mode / generated count | On / 1 |
| `numFramesActuallyPresented` maximum | 1 |
| Input-processing fence / completed | 0 / 0 |
| Estimated DLSS-G VRAM | 0 |
| Device removed result | S_OK in all 242 samples |
| Cleanup | DLSS-G release, plugin shutdown and NGX shutdown observed |

Thus lack of focus, below-minimum extent, a reported invalid common-constant
status, a reported missing-Reflex status and device removal do not explain the
final observation. The backend still did not expose an additional presentation.
`numFramesActuallyPresented` is the SDK's since-query counter, not a direct image
capture or a promise that every count corresponds to a generated image.

The initial unpinned upstream-default probe selected existing OTA 2.14 plugins
with a 2.12 interposer; its log explicitly reported `window not focused` and it
is not performance evidence. Subsequent runs disabled both OTA requests and
downloaded-plugin selection **in sample preferences only**. They loaded the local
2.12 components above. No system cache was manually changed or cleared; HAGS,
driver profiles, user games and their configurations were not changed.

## Reproduce without importing SDK code

Clone the official pinned sample into an external dependency directory and copy
the official SDK's contents into its `streamline` directory, as its README asks.
Use `scripts/prepare-fg-sample.py --sample $Sample --manifest $Manifest` to add
the small instrumentation. It checks the exact Git head and rejects unrelated
edits. It records original/patched hashes, requests focus only once, disables
console/message-box output, preserves full log lines and logs actual SDK state.

Configure with:

```powershell
cmake -S $Sample -B "$Sample/_build-fg" -G "Visual Studio 17 2022" -A x64 `
  -DDONUT_WITH_VULKAN=OFF -DDONUT_WITH_DX11=OFF `
  -DSTREAMLINE_FEATURE_DLSS_SR=OFF -DSTREAMLINE_FEATURE_DLSS_RR=OFF `
  -DSTREAMLINE_FEATURE_DEEPDVC=OFF -DSTREAMLINE_FEATURE_NIS=OFF `
  -DSTREAMLINE_FEATURE_NVPERF=OFF -DSTREAMLINE_FEATURE_IMGUI=OFF `
  -DSHADERMAKE_FIND_DXC_VK=OFF
cmake --build "$Sample/_build-fg" --config Release --parallel 6
./scripts/run-fg-sample.ps1 -SampleRoot $Sample -OutputRoot $NewEvidence `
  -EnableFG -AllowFocusedWindow -Frames 120
```

This explicit window opt-in is necessary: hidden startup can suppress the
renderer window. The window is small, requests focus once and closes itself.
If the user changes focus, the script never repeatedly steals it back. A
30-second watchdog terminates only this process if normal shutdown fails.
Device failure and `UINT64_MAX` fence sentinels cannot count as valid completion;
zero input-fence values do not count as completed input processing either.

The renderer and SDK licenses stay with the external checkout/SDK. Consult their
licenses before any binary redistribution; this experiment distributes only our
adapter scripts and this report. Patch comments identify altered sample behavior.

## Narrow next step and boundaries

Keep these exact inputs and renderer. Inspect the SDK's interpolation decision
using a verbose or development diagnostic run, especially the logged
`Engaging WAR4639162` decision, rather than changing driver profiles or inventing
replacement camera matrices. The log alone does not establish what that
internal workaround means. Then independently capture generated images and
display timing before claiming correct intermediate frames or pacing.

No more parameter sweep was performed after the final focused run. No NR+FG
combined path, real game, resize/Alt-Tab fault matrix, hardware latency,
P95/P99, HUD quality or generated-pixel correctness has been validated here.
Hosted CI was not run; the meaningful results are local MSVC compile and the
bounded NVIDIA hardware capability/render/present/lifecycle observations above.
