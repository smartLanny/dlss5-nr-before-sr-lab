# Full-resolution AMD/NVIDIA guide routing — source investigation

## Owner decision and exact baseline

The owner selected `codex/050-corefix8-integration-20260911` at
`49b0deb0d09b0a442784e0ebf1073f9498eaf157` (D14), not current `main`
`daac394025405a94cc056b0223c1e27e9c7d9ee7`. Preserve D14's cumulative Corefix1–8,
dual NR and picture controls. The experiment is default OFF, AMD OF selected,
NVIDIA OF explicitly switchable, with **no additional OF input downsampling**.
Native validated guides remain the desired preferred production route.

DXL and Magpie are architectural references, not code to transplant. No DXL/GPL
integration source is incorporated. This report distinguishes upstream source
behavior from owner-game acceptance and from this PR's unexecuted Windows paths.

## Carrier is not optical flow

At this exact D14 tree, `scripts/prepare-native-bridge.py`'s
`NrNativeBridgePolicy` forces `synth=0` and `source=mirror` (except explicit OFF).
It copies a native D3D11 NGX contract into private D3D12 resources and provides an
execution location for Core, preserving real depth/MV/dimensions where supported.
Its real-depth transport failure keeps native SR. This is not “automatically make
OF for DX11”. Current generated/package identity must still be checked separately
from the historical upstream pin in that script.

Three independent responsibilities must not be conflated:

| Responsibility | Input/output | Why it remains needed |
|---|---|---|
| Guide acquisition | native NGX/FFX/extracted guides or captured color -> estimated motion | Determines data provenance and temporal quality |
| API/process transport | D3D11/9/Vulkan/GL/x86 -> compatible D3D12/x64 resources, and back | Optical flow does not make API handles or pointer sizes interchangeable |
| Neural/composition execution | an explicitly owned frame -> the existing Core -> original presentation | Needs color domain, timing, model support and safe result delivery |

The desired priority is **valid native contract before estimated guides**, not
“DX11 always uses carrier”. A native-compatible route may use carrier transport;
a generic capture route may use the same transport but different guides. Captured
final color need not wait for a game's Feature1 call. A production transport should
not require an extra DLAA pass only to wake our own Core.

## Five upstreams read at fixed revisions

| Repository/ref | Source examined | Useful behavior and limits |
|---|---|---|
| NIGos/dlss5-bridge `d7100ddc1635bb912a0764fa5646b5ca44443536` | `README.md`, `src/bridge.inc` | stable1.4.12 / candidate1.4.13-pre7; separate native mirror and opt-in substitute contracts; native create flags, multi-view histories and conversion fixes. Synthetic route includes DLAA and approximate guides; not a quality-equivalent native path. Do not transplant its skip-game behavior into our Core without original-call analysis. |
| jlrouzies-fr/DLSS5-Feeder `76a08db83652617cfe4192eeecf9e46bfc008382` | `README.md`, `src/dlss5-feed.cpp` | source version0.15.1; ReShade color/raw depth/motion, D3D11 shared textures, Vulkan/GL import, x86 host. Makes NGX calls for an external neural consumer. Useful transport, not a required second NR or automatic universal input oracle. |
| kibblerz/DLSS5-Reshade-AIO `09301f5528e619e8b9ec17c257d167e2985f53b0` | `README.md`, `addon/include/nvof-motion-provider.hpp`, `addon/src/nvof-motion-provider.cpp` |2.2.3, NVOF current->previous S10.5 conversion, slot consumers and independent presentation. Its 180p default is expressly NOT adopted. OF cost/rejection is not a face mask/NRControlMask. Read retirement callers before borrowing code; the function-local shutdown/slot predicates alone are not our lifetime proof. |
| SAOG0721/Magpie `experimental@7eb19987e73ad2e65bc7d75056aece5465a10204` | `DLSSNRFilter.cpp`, `OpticalFlowSettings.h`, `MotionVectorRequest.h`, `AmdOpticalFlowProvider.cpp` under `src/Magpie.Core/` | AMD/NVIDIA choice and shared guide requirements; capture route independent of game engine. Its shared policy prioritizes requested NVIDIA, unlike this owner's AMD default. The AMD densifier's fixed confidence0.65 is a heuristic, not measured per-pixel confidence; do not copy or compare it directly with NVOF cost. Input preparation can change OF extent, which our experiment forbids. |
| LCPD15/DXL `f7ea753d24141b32475fa7e0420183f65f98d142` | `OpticalFlow.cpp`, `DepthTracker.h`, `DlssNrFilter.h`, `DlssNrFilter11.h/.cpp`, FidelityFX notice | actual FFX OF, native-guide preference, generic depth heuristics and D3D11 shared-fence transport. Its640/960/1280 cap is NOT adopted. Its code is AGPL; this is inspiration only. Headers/source are evidence of implementation, not proof of successful operation in every game. |

Direct source navigation: append `/blob/<revision>/<path>` to the repository.
- https://github.com/NIGos/dlss5-bridge
- https://github.com/jlrouzies-fr/DLSS5-Feeder
- https://github.com/kibblerz/DLSS5-Reshade-AIO
- https://github.com/SAOG0721/Magpie
- https://github.com/LCPD15/DXL

Official contracts, not third-party integration code, are the implementation basis:
- https://gpuopen.com/manuals/fidelityfx_sdk/techniques/optical-flow/
- https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/v1.1.4/sdk/include/FidelityFX/host/ffx_opticalflow.h
- https://docs.nvidia.com/video-technologies/optical-flow-sdk/nvofa-programming-guide/index.html
- https://github.com/crosire/reshade/blob/main/include/reshade_api.hpp
- https://github.com/crosire/reshade/blob/main/examples/09-depth/generic_depth_addon.cpp

## Generic Depth should be reused, not reimplemented

ReShade's `DEPTH` semantic is the sensible first generic depth source. It points to
an observed/selected game depth buffer or a preserved copy; it does not infer scene
geometry from RGB. A generic depth menu being present does not certify this frame's
binding, main view, contents before clear, projection/reversed/log-depth, subrect or
lifetime. Consume its existing selection and manual override; do not add a second
resource-scanning heuristic that disagrees with it.

The initial FX probe reads the same `DEPTH` binding for visualization and reports
its raw dimensions. It does not feed that raw value to NR yet. Native guide textures
can be valid but in a different frame/crop/domain from final captured color, so
mixing components blindly is not “native preferred”; it is a temporal mismatch.

If no usable depth exists, distinguish: (a) explicitly bounded neutral/missing-depth
NR experiment whose particular runtime acceptance has been tested, (b) an independent
estimated-depth model with its own cost/scale/history, (c) original-picture bypass.
OF does not supply metric/geometric depth and is not a substitute for those checks.
Do not block all RGB-only research forever, but do not call placeholder depth native.

## Full-resolution OF and native priority

Keep separate `S=captured source`, `R=proven game internal render`, `N=NR working
extent`, `O=final presentation`. OF input stays S exactly. Lowering N must not lower
OF input. Internal AMD pyramids/block vectors and NVOF output grids remain normal.
No implicit lower-quality fallback is allowed in an A/B test: choosing NVOF then
silently running AMD would invalidate the comparison. Report requested vs actual.

Native priority must require matching stream/view/frame/reference/configuration,
valid source region, known units/direction, states and lifetime, not just a non-null
resource. A guide-source change requires explicit temporal reset/rebuild after old
consumers retire. Native motion may be UV-scaled, pixel-scaled, jittered, render-size
or output-size; “scale equals width” is only a narrow special case, not a general
conversion detector. OF returns screen-space appearance correspondence and can
mis-track occlusion, reflection, transparent effects, UI and repeated textures.

## DX9 and universal coverage

Native x64 D3D12 can run the backend without cross-API transport. D3D11 can share
resources with a private same-adapter D3D12 device and GPU fences. D3D9/D3D9Ex and
ReShade's internal effect backend need explicit compatibility handling; native x86
requires a small x86 capture addon and x64 host for a 64-bit neural runtime. Vulkan
needs correct external-memory/semaphore/layer contracts, GL its supported import or
capture route. None of this is removed by obtaining an optical-flow texture.

A last-resort window-capture/presentation path (Magpie-style) can broaden coverage
without engine hooks, at the price of final-image/UI capture, window/input behavior,
extra latency and absent native guides. NR-only enhancement of a finished image is
not the same as lowering the game's R, replacing temporal AA, or native NR-before-SR.
Device/model capability and permitted injection/capture remain admission conditions;
protected content or anti-cheat restrictions are not to be bypassed.

## Manager boundary

Do not remove working Feeder/carrier/Vulkan/x86 recipes now. Once equivalent
transport actually exists inside our addon/host, Manager can deploy one coherent
component set instead of asking users to choose a feeder. Runtime guide selection
belongs in the plugin; architecture selection, validated versions, hashes, installation,
conflict detection and reversible removal still belong in Manager. Internalizing a
bridge eliminates a separately visible component, not synchronization or bitness.

## This delivery and integration gap

`experiments/fullres_guides` contains real AMD/NVOF backend calls and ReShade
capture/preview source, isolated from D14. It is default-off and build-opt-in.
The only executed tests are portable production-policy assertions; Windows/FX,
GPU, motion correctness, HDR, game quality and end-to-end performance are unverified.

D14 lacks `src/interop/nr_external_provider_abi.h` and the corresponding exports.
Do not merge the old #175 branch wholesale over D14. Bring its minimal owned
consumer/submit seam forward against current context, command-state and dual-pass
lifetime code, test it with full-size OF and Generic Depth, then expose the selection
inside the existing Core menu. The independent guide probe is NOT completion of
#239. Keep #239 open, preserve original plans and report the precise remaining gap.
