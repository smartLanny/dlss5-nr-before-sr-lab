# #14 Borderlands 3: read-only input investigation, no game support claim

## Full receipt add-on compiled and linked; still not loaded or deployed

The subsequent authorized dependency pass completed the real generic UE
`Development-Release|x64` project with both `unity_build.cpp` and the patched
`main.cpp`; MSBuild exited 0 and produced a 1,897,472-byte DLL-format `.addon`.
SHA256: `1a93cb0d960f8f4d75bf4d69e8afa48d4beb9b5074d5fc3d1070510bb4278db0`.
This supersedes only the earlier full-build dependency blockage. It does not
supersede the absence of real BL3 input observations or DLAA/R=O limitation.

Actual receipt implementation source remains
`fa4c6eb88d0b6f05a97d6deb30d41e8bc30b77b1`; this later report is not its build
source. Patched main SHA256 is
`1dc5d9c1ffbe47ded56bbfe0e0eaa541fd02f4404fef4f4b126988b006e5ee3e`, receipt
header SHA256 is `dd9cc1f562d3168e94f245e94f271e43d8bf782f2cba32822079533cc9cda74d`.
The fixed upstream source/headers, exact NGX library, actual ATL package hashes,
compiler and artifact evidence are in
[the dependency lock](BL3_RECEIPT_BUILD_DEPS_20260908.json).

Only 166 selected upstream text/source files (6,413,090 bytes) were obtained,
with every downloaded file's Git blob verified. These cover Core, generic UE,
Global/Includes/UE shaders, relevant project files, required ReShade headers and
NGX headers. ReShade is fixed at its Luma submodule pin `18deaa52…`; existing
ImGui matches its required pin `3912b3d…` exactly. The small stb header is fixed
at `28d546…`. DirectXMath was reused from the existing cache. Optional FSR is
disabled with the supported `UseLumaFSR=false` property; no AMD SDK, DKUtil or
unused NVAPI libraries were pulled into this build.

The matching Luma NGX static library was fetched separately (5,576,350 bytes).
No differently versioned cached NGX library was substituted, and no 69 MB NGX
runtime was downloaded. The remaining actual runtime selection is untested.

The first full build failed at `shader_compiler.hpp:3` because official
`atlbase.h` was absent. Its original log is preserved with SHA256
`d72cf370656e743496ac4cc5a88486d9f7f7f9e7f77a65f516234c651ce898b4`.
Only the official catalog's ATL Headers/X64 VSIX payloads were then downloaded
(1,388,331 bytes total), SHA256-verified and extracted outside Visual Studio.
No system component installation was run. An intermediate attempt to pass ATL
through process environment alone did not propagate through this MSBuild
project; that failure log is also retained. The external project finally adds
only ATL/DirectXMath include paths and ATL library paths before Cpp.targets.
The resulting project hash is recorded in the dependency lock.

The build command used `msbuild` on the existing UE vcxproj with
`Configuration=Development-Release`, `Platform=x64`, `PlatformToolset=v143`,
`WindowsTargetPlatformVersion=10.0.26100.0`, an explicit external `SolutionDir`,
`SolutionName=Luma`, `UseLumaFSR=false`, `LumaPackage=false`, and
`PostBuildEventUseInBuild=false`. The build process also cleared
`LUMA_UNREAL_ENGINE_BIN_PATH`. Full success log SHA256:
`462a625a60bfe2cd7c37ebc16ab6c766d1243e9dfd15926626a02a45bdab967b`.

Read-only PE inspection confirms `AddonInit`, `AddonUninit`, `NAME`,
`DESCRIPTION` and `WEBSITE` exports and expected Windows/VC runtime imports.
This verifies a complete compiled add-on, not a successful load: no LoadLibrary,
ReShade session, game launch, shader compilation in a game or GPU work occurred.
The Development build resolves shader assets through its external source
`SOLUTION_DIR`; the selected Global/Includes/Unreal Engine shader directories
must remain present. It is **not a self-contained single-file install** and no
player package, deployment or runtime compatibility result is implied.

## Follow-up: bounded input receipt prepared, full add-on not built

The fixed upstream already provides a Development-only **Frame Capture** button
(`Source/Core/core.hpp:10847`), followed by **Captured Commands** (`:11087`).
Its existing display includes shader/SRV/RTV/UAV state and CB index/hash/range
(`:13010`). **Auto Dump Shaders** / **Force Dump Shaders** (`:11041/:11055`)
preserve shader bytecode. Use those existing facilities for resource binding
inspection; no duplicate draw-capture or shader-dump framework was added.

The generic UE custom entries at `main.cpp:1039–1049` and `:1087–1097` record
decoded-motion/SR output resources, while the normal log reports TAA detection.
They do not serialize the final DrawData jitter/reset/exposure/MV scales
together with inferred CB indices. The new
`scripts/prepare-luma-ue-input-receipt.py` fills only that missing receipt.
It accepts exactly the upstream main.cpp SHA256
`34a750a7ccbe373b2ab5e8bd690b122c6782c2003e3def864c05edc80fb8a935`, checks
unique include/Draw anchors, and writes a fresh external directory. Original
input is never edited. No Luma file is committed in this repository.

The inserted `#if DEVELOPMENT` call runs immediately before the existing SR Draw,
after confirmed inputs and decoded motion are available. It logs first four
eligible calls, structural changes/reset rising edges and every 120th call,
capped at 32 rows per process. Rows contain actual TAA hash/stage, source/depth/
velocity slots, global-CB slot/size and inferred indices, final resource formats
and extents, pixel jitter, MV scale, reset/camera-cut/output-change flags and
all scalar DrawData values. `evaluated=0` explicitly means a pre-call receipt,
not success. No line appears for failed detection that never reaches SR Draw;
retain upstream detection logs and original-TAA capture for that case.

Resources/context use bounded session IDs instead of addresses or game paths.
IDs describe observed addresses only, not independently verified view ownership
or object generations; pointer reuse can reuse an ID. No buffer contents are
dumped by the receipt. It only queries texture descriptors, emits the log and
returns; the original call/fallback/resources/settings remain unchanged.
Logging exceptions are contained. Logging overhead is not performance evidence.

Actual verification: the staged helper was compiled and linked with MSVC
14.44.35207/C++20, Windows D3D11 headers and the **real fixed upstream**
`super_resolution.h` (SHA256
`4a138f7faa4b94a73d8433f09fc9264c7e323354c91beb9df2d553566b3a2939`).
`tests/interop/luma_receipt_header_check.cpp` executed 5,000 CPU-only calls using
null resources, verified the 32-row cap, field formatting, address concealment
and unchanged input values; exit 0. It creates no GPU device. Check EXE SHA256:
`cd26f8229c133d25703c90480007db3ce35ba1c3cb809e21ec09af960d912a15`;
compile log SHA256:
`25184aef2c72da2275c7d47472d8855ed2ec9ecb357ce3f2929f69bb407f07c0`.
Upstream header unused-parameter warnings remain.

This is a genuine minimum **helper/header** compile and CPU check, not a full
Luma add-on compile or proof the patched generic UE translation unit links.
The selected-text cache lacks the full Luma build tree, its pinned ReShade
submodule and other core dependencies/assets. Those were not downloaded to
inflate this input-observation slice. No loadable add-on is delivered here,
and no game/real texture/input receipt has been captured.

Prepare source with:

```powershell
python scripts/prepare-luma-ue-input-receipt.py --source $PinnedUpstreamMain `
  --output $NewExternalStage
```

For a later complete external source build, the two staged files belong in
`Source/Games/Unreal Engine/`, using the upstream Development configuration.
The receipt logs automatically at the real SR seam; its prefix is
`NR_UE_INPUT_RECEIPT`. This is an integration instruction, not authorization to
install or launch now. Generic SR remains DLAA/R=O, with Core NR absent for the
first actual input observation. The original TAA fallback is preserved.

The smallest existing executable seam is the generic UE adapter's completed
`SR::SuperResolutionImpl::DrawData`, immediately before the official D3D11 DLSS
evaluation. It is not the backbuffer or a new synthetic provider. No DLL was
installed, no game was launched, no GPU test was performed and no third-party
source was copied into this repository. This branch starts at `e7df0fc`.

## Fixed installation and upstream evidence

The new `scripts/probe-bl3-input-baseline.py` reads an explicitly selected game
installation and Steam manifest, hashes the executable and records named
temporal-DLL candidates. It writes only a new report outside the game directory.
It does not read account IDs from the manifest into the report, alter settings,
invent shader hashes, inspect GPU memory or claim DLL absence proves no static
integration.

Actual local result: Steam app 397540, build 15245523; executable 116,228,592
bytes, SHA256 `923afd263631681aff88037add504bf353fc6f4caa9045cfdb709e049fe4f101`.
No installed DLL filename matched nvngx/DLSS/FSR/FidelityFX/XeSS. Runtime graphics
API, shader identity/registers and temporal inputs remain unobserved. The game
executable has no populated file/product version in its Windows version fields;
UE 4.20.3.0 is the upstream compatibility-list entry, not a local binary finding.

Official Luma source was pinned at
[`91babfe12abe5f7de0c4890f0efc321d8277887f`](https://github.com/Filoppi/Luma-Framework/tree/91babfe12abe5f7de0c4890f0efc321d8277887f).
Only selected text sources were fetched into an external private cache; no full
clone, binary download or shader extraction was required.
The [official wiki](https://github.com/Filoppi/Luma-Framework/wiki#unreal-engine)
still marks BL3 experimental with a flickering note. [Issue #88](https://github.com/Filoppi/Luma-Framework/issues/88)
remains open: a maintainer acknowledged regression; a later user reported
working DLAA/no flicker on two planets with a 50-series GPU/596.36 driver on
June 30. Neither report identifies this local executable or proves reliable
support across views, cutscenes and settings. No second game was investigated.

The pinned [root license](https://github.com/Filoppi/Luma-Framework/blob/91babfe12abe5f7de0c4890f0efc321d8277887f/LICENSE.md)
is **Custom MIT**, not GPL. Its addenda require author/project attribution and
author permission for commercial usage. This is a source-license observation;
dependencies have their own terms. This investigation introduces no copied
Luma code and no redistribution decision.

## Concrete source touchpoints

All line numbers below refer to the fixed source above.

| Input / decision | Existing upstream code and actual contract |
|---|---|
| TAA candidate | `Source/Games/Unreal Engine/includes/shader_detect.hpp:310` `IsUE4TAACandidate`, then `FindShaderInfo:497`: bytecode signatures and CB/clip-to-previous indices, not a BL3 hardcoded hash |
| Runtime TAA confirmation | `Source/Games/Unreal Engine/main.cpp:719`: PS/CS SRV inspection; at least two color textures, depth and UNORM velocity, with size/aspect checks. Lowest matching color slot is assumed current color; that assumption still needs BL3 verification |
| Color / output | `main.cpp:870–983`: confirmed source SRV resource, RTV0/UAV0 destination, linear HDR before tonemapping. An intermediate UAV-capable texture is allocated when required; full graphics/compute state is restored |
| Depth / motion | `main.cpp:150,187,227` `DecodeMotionVectorsPS/CS` and dispatcher; `Shaders/Unreal Engine/Luma_MotionVec_UE4_Decode.hlsl`: real reversed-depth input, encoded dynamic velocity decoding plus depth/ClipToPrevClip reconstruction of static motion, output R32G32_FLOAT pixel motion |
| Projection / jitter | `main.cpp:1344,1372` `OnMapBufferRegion`/`OnUnmapBufferRegion`: match mapped global CB size, validate view-size/inverse-size and jittered projection, take m20/m21. `main.cpp:1062` converts NDC jitter to pixels using width/2 and negative height/2 |
| History reset | `main.cpp:1053`: force-reset, changed output or camera-cut; `main.cpp:1135` derives its camera-cut flag from prior TAA/SR presence. This is heuristic, not a proven BL3 cut flag |
| SR evaluation | `Source/Core/dlss/DLSS.cpp:372` `NGX::DLSS::Draw`: maps real resources, render subrect, exposure, reset, MV scale and jitter into `NGX_D3D11_EVALUATE_DLSS_EXT`; creation is in the same file near line 133 |

Source links: [generic UE adapter](https://github.com/Filoppi/Luma-Framework/blob/91babfe12abe5f7de0c4890f0efc321d8277887f/Source/Games/Unreal%20Engine/main.cpp),
[shader detection](https://github.com/Filoppi/Luma-Framework/blob/91babfe12abe5f7de0c4890f0efc321d8277887f/Source/Games/Unreal%20Engine/includes/shader_detect.hpp),
[motion decode](https://github.com/Filoppi/Luma-Framework/blob/91babfe12abe5f7de0c4890f0efc321d8277887f/Shaders/Unreal%20Engine/Luma_MotionVec_UE4_Decode.hlsl),
[official-DLSS wrapper](https://github.com/Filoppi/Luma-Framework/blob/91babfe12abe5f7de0c4890f0efc321d8277887f/Source/Core/dlss/DLSS.cpp).

The generic adapter explicitly sets `sr_render_resolution_scale = 1.0f` with a
DLAA-only comment (line 908). First validation should therefore be **SR-only
at native resolution / DLAA**, not a claim of lower-resolution rendering. Its
viewport reassignment lines are commented out, the camera-cut signal is
heuristic, and CB offsets are dynamically inferred. These are exact things to
observe in this game before a subresolution or multi-view path is accepted.

## Thin adapter boundary and existing DX11 carrier

The listed extraction functions are not standalone exports: they depend on
Luma's DeviceData, CB tracking, shader detection, state stack and shader assets.
They cannot honestly be copied as a small independent extractor with guaranteed
BL3 inputs. If an external Luma development adapter is used later, the narrow
place to add a read-only frame receipt is just before its existing SR Draw call:
record shader identity/stage, view/epoch/frame, SRV registers and formats/extents,
CB byte size/indices, pixel jitter/MV scale, reset reason and output resource.
Keep the existing original-TAA fallback when detection/evaluation is incomplete.
No new public provider ABI is needed for that first receipt.

For an eventual NR pass, the existing **native** carrier already intercepts the
official D3D11 Create/Evaluate parameter boundary used by Luma's DLSS wrapper.
Its pinned upstream `28aed4099b0fe1c207b20b5fee5364c0773c25c2` `src/bridge.inc`
`BridgeFrame` mirrors real color, depth and decoded motion, forwards pixel
jitter/reset and evaluates on its private D3D12 path, then copies the output
back. Our `scripts/prepare-native-bridge.py` rejects placeholder depth and
failed exposure transport; `scripts/patch-carrier-completion.py` waits for real
completion before DX11 copyback and quarantines failed retirement. The current
`src/interop/nr_native_bridge_forward.inl` preserves native SR on disabled,
unready or failed delivery paths and restores native history after gaps.

That is a source-level compatibility seam, **not** a BL3-tested carrier route.
Do not load the carrier merely to gather a first SR-only observation: its
startup hooks/private-device bootstrap have their own lifecycle history (#153).
First prove the upstream official D3D11 SR call and real inputs with NR absent;
only then evaluate this existing carrier with our Core as the sole NR owner.
No OptiScaler default, duplicate NR implementation or LumaCore import is needed.

Do not route this through `nr_external_provider_feeder_map.h::BuildSyntheticFrame`:
that mapper explicitly marks estimated guides synthetic and expects same-device
D3D12 resources; a D3D11 pointer cannot be passed as a D3D12 resource. Real
game data must retain native provenance and the actual bridge synchronization.

## Missing observation and next authorized evidence boundary

The precise missing item is one instrumented BL3 **DX11 TAA event** matching the
hashed build, including the dynamic candidate hash, source/history identity,
depth/velocity SRV slots, RTV/UAV destination, per-view CB offsets and values,
decode output units/sign, jitter/reset provenance, and corresponding official
DLSS Create/Evaluate result. Installation metadata cannot provide these facts.
Camera cuts/loading, settings changes and original-TAA fallback must remain
observable rather than inferred from API success. No shader hash or offset is
prepopulated in the probe. This slice stops before installation or game launch.

Reproduce the bounded read-only baseline with an explicit new output:

```powershell
python scripts/probe-bl3-input-baseline.py --game-root $GameRoot `
  --steam-manifest $SteamManifest --output $NewPrivateReport
```

Local validation consists of that successful actual-install fingerprint and
`git diff --check`. No compile, hosted CI, game or hardware result is claimed.
