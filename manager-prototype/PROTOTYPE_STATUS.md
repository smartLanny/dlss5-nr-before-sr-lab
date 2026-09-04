# Prototype status

## Implemented in this branch

- Electron shell with context isolation and sandboxed renderer
- independent Windows AppUserModelID
- independent future overlay/profile namespace
- Simplified-Chinese beginner navigation
- mock game card and recommendation presentation
- NR→SR / SR→NR intent-level mode selection
- 0.8 / 1.2 / 1.5 AI intensity presets plus custom slider
- style and face-protection controls
- one-click recommended settings
- diagnosis UI and copyable support report
- advanced-mode disclosure
- upstream attribution boundary
- NR Control Contract v1
- desktop Entitlement Contract v1

## Deliberately mocked / not implemented

- real Steam/Epic/GOG/Xbox discovery
- game executable/API/DLSS inspection
- ReShade installation
- transactional file backup/restore
- Feeder/OptiScaler integration
- actual `nvngx_dlssnr.dll` presence/version checks
- real NR runtime state readback
- real F8 Chromium overlay transport
- real account/auth server
- QQ group link/number
- updater

The UI must continue labeling these paths as prototype behavior until the underlying adapters exist.

## Next safe implementation target

Import/adapt DLSS5-Swapper's scanning + file-journal + native ReShade deployment infrastructure into a future standalone private Manager repository, preserving upstream MIT and third-party notices. Do not connect the prototype install button to ad-hoc file copies.
