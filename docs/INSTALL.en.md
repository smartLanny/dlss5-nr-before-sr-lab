# beta0.3.1 startup-stability hotfix quick install

Add-on: **DLSS 5 AI Rendering Upscaling Edition beta0.3.1**

Author: **@野生的装机宅 Bilibili**

Only official release location: [野生的装机宅 on Bilibili](https://space.bilibili.com/941799). Verify the signed manifest before trusting a copy from anywhere else.

## Scope

- 64-bit D3D12;
- a native DLSS Super Resolution call (NGX Feature 1);
- ReShade/RHI configured to load `.addon64` files;
- offline, non-anti-cheat use only.

DX11, Vulkan, games without native DLSS SR, and paths fully replaced by a
translation layer such as OptiScaler are outside this add-on's direct scope.

## Install

1. Exit the game completely.
2. Open the Addon search directory actually used by ReShade/RHI. With no custom
   path this is commonly the directory containing the real 64-bit game EXE and
   the ReShade DLL.
3. Remove old `nr-before-sr*.addon64` builds, the other language variant, and
   any second DLSS5/RenoDX/ShortFuse add-on that would own NR at the same time.
4. Keep these files **together in that same directory**:
   - `DLSS5-AI-Rendering-Upscaling-beta0.3.1-@野生的装机宅-Bilibili.en.addon64`
   - `nrchain_nvngx.dll`
   - `nr_before_sr.ini`
   - `nvngx_dlssnr.dll` from the RTX 50 package
5. Start the game and verify that the ReShade Add-ons page shows beta0.3.1, the
   author and official release address at the top of this add-on's panel.

The add-on loads its configuration, bridge and NR runtime from its own folder.
If ReShade can see only the `.addon64` while the sibling files are elsewhere,
the panel may open but every option will appear ineffective.

## Quick use

- `F6`: master toggle. Off must be native game SR with zero Feature 18 calls.
- The default `render-resolution NR -> SR` option follows the game's real DLSS
  render extent, then passes the result into the game's native SR.
- Turn that option off for the full-output-resolution `SR -> NR` quality and
  performance reference path.
- While Ray Reconstruction owns a live feature, the release candidate reports
  `RR safe bypass` and does not stack a second NR pass.

For a report, include the displayed version and bypass reason plus the smallest
matching window from `nr-before-sr.log`. beta0.3.1 logs state changes, the first
few diagnostics and one summary per 600 frames rather than writing every frame.
Remove user names, absolute paths, account data, process addresses and runtime
binaries before sharing.

The RTX 40 community package does not redistribute the ShortFuse runtime. Supply
a lawfully obtained compatible runtime and keep it beside the add-on, bridge and
configuration. RTX 40 family-wide compatibility is not claimed.

## Verify official files

Open PowerShell in the extracted directory and run:

```powershell
.\Verify-ReleaseManifest.ps1
```

`VALID` means every listed file exactly matches the manifest signed by the release key for 野生的装机宅. This is a provenance and tamper-evidence signature; the add-on DLL itself does not yet carry a commercial Authenticode certificate.
