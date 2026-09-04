# From prototype to standalone downstream product

## Phase 0 — current branch

Done in `product/china-manager-prototype`:

- isolated Electron product shell
- Simplified-Chinese beginner information architecture
- intent-level NR controls
- diagnosis/copy-report interaction
- advanced-mode hiding
- independent product/app/overlay namespace
- desktop-only entitlement boundary
- stable NR Control Contract v1
- explicit upstream attribution boundary

No real game files are modified in Phase 0.

## Phase 1 — bring in proven manager infrastructure

Create the eventual standalone private Manager repository, then selectively import/adapt the MIT-licensed DLSS5-Swapper infrastructure while preserving its original license and third-party notices:

1. game discovery/library metadata
2. safe target validation
3. file journal / transactional install and restore
4. ReShade native route
5. Feeder route only where it materially extends compatibility
6. component integrity/hash verification
7. install history and recovery

Do not copy the upstream beginner-unfriendly navigation as the product information architecture. Infrastructure should sit behind the new Chinese UX.

## Phase 2 — make NR→SR a first-class feature

Package the NR feature as a managed bundle rather than a generic custom add-on:

- NR add-on binary
- `nrchain_nvngx.dll`
- `nr_before_sr.ini`
- externally/lawfully obtained runtime dependency checks

The Manager owns install/restore bookkeeping. The rendering add-on owns rendering safety.

## Phase 3 — product overlay

Reuse/adapt the MIT upstream Chromium-to-ReShade overlay transport if desired, but replace the version-specific RenoDX private bridge with `NR_CONTROL_V1`.

Requirements:

- F8 beginner overlay
- same HTML/CSS visual system as desktop
- bounded local IPC
- Manager may disconnect without breaking NR rendering
- ReShade native NR page remains available as fallback

## Phase 4 — entitlement and update service

Implement desktop sign-in/redeem/update flows. Server returns signed short-lived/offline-tolerant entitlements. Game process verifies locally only if gated features require it.

Never ship signing private keys or service secrets in Electron or the ReShade add-on.

## Phase 5 — Chinese support workflow

- public QQ group / support entry
- one-click diagnostic copy
- compatibility database
- recommended profiles by GPU/output resolution/game
- clear Chinese error explanations and one-click repair where safe

## Migration criterion

Split to the standalone Manager repository before importing substantial upstream source or shipping binaries. Keep the parent NR research repository focused on rendering-core development and evidence.
