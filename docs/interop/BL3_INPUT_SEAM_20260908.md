# #14 Borderlands 3: read-only input investigation, no game support claim

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
