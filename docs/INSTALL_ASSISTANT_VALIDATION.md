# Installation assistant qualification — 2026-09-05

Scope: Windows x64, two bounded native D3D12 game profiles, official ReShade 6.8.0 Full Add-on host, signed offline payloads. This is source/build/automated fixture qualification, not an in-game acceptance result.

## Reproduced defects

The actual original `Deployer.Install/Recover/Uninstall` interfaces were exercised before changes. The red tests reported:

- `OLD ROLLED-BACK JOURNAL DELETED LATER SUCCESSFUL INSTALL`
- `MISSING BACKUP DELETED DESTINATION WITH ORIGINAL OWNER`

Both passed after the minimal corrections. The expanded regression suite retains those behaviors against explicit transaction IDs, terminal states, active pointers and original-owner backup mapping.

## Automated results

`scripts/test-install-assistant.ps1 -VerifiedHostPath <official headless-produced dxgi.dll>` completed **53 scenarios, zero failures**. WPF and helper Release builds completed with zero warnings/errors.

Coverage includes signed envelope/payload rejection, fixed trust anchor, flat signed ZIP import, nested/unsigned ZIP rejection, artifact/version separation, x64/NGX discovery, unsupported profiles, fresh official-host deployment, default/existing ADDON path, four sibling payloads, four fixed-name optional feedback/crash scripts, wrong crash-script name rejection, user config preservation, same-byte reuse, known/unknown aliases, update ownership, six injected interruption points, missing/corrupt backups, user edits/deletions, changed plans/sources, cancellation, target/state/dangling junctions, pinned directories, actual second-process locking, actual process exit at two transaction stages, running-game exclusion, ownership path tampering, helper result/operation rejection, and runtime v2 PID/start/path/SHA identity. Chinese first-install coverage additionally verifies YaHei preference, SimSun fallback, existing custom-font preservation, no-font and English-package no-op behavior, exact ReShade.ini restoration after uninstall or injected failure, and no font mutation during later managed upgrades.

The former dirty junction test used a different path from the actual planned destination. Its replacement creates a junction at the actual addon destination after planning and verifies the outside sentinel remains unchanged. Directory pinning is separately tested during the move checkpoint.

The game/NGX executables are synthetic PE fixtures. The ReShade host is **not synthetic**: every installation plan verifies and deploys the actual official setup output. No real game process is launched by these tests.

`tools/install-assistant.HostTests` independently completed **7 scenarios** through production `ReShadeHost`: wrong setup rejection, local official import, official headless generation, cache/concurrent reuse, corrupt host rejection, repair from pinned setup, cancellation.

Pinned official identities:

- Setup URL: `https://reshade.me/downloads/ReShade_Setup_6.8.0_Addon.exe`
- Setup SHA256: `AFE4C8F13048306307983B8B3D41D5BF00A86820440B0E57DEA10950E1176445`
- Generated `dxgi.dll` SHA256: `0CEE63F9C9F13F3AC909C5B4903F4DBB4B719A7AB3B4F13B0DEAF83C814B94F7`
- Actual official invocation uses `--headless --api dxgi` in a unique short temporary sandbox. It does not run against a real game directory. Official `--api d3d12` would select a different proxy filename and is not the invocation tested here.

Local transcripts are in `test-output/installer/transaction-tests.txt` and `test-output/installer/official/host-production-tests.txt`. Initial red harness copies are preserved under the same ignored test-output directory.

## Evidence boundary and pending acceptance

The runtime v2 producer is supplied by the separate final plugin build. The installer consumes exact addon path, SHA256/artifact ID, process ID/start time, fresh state and counters; a shared display version does not qualify a different binary. The automated consumer tests cover the protocol; final signed payloads must still be verified with `--verify-production-package` after packaging.

Final packaging must record both installer source commit and payload source commit, verify explicit input hashes, and sign both the app directory and each offline component directory. This document does not attach earlier gameplay qualification to a new payload hash.

Pending user acceptance: Cyberpunk 2077 installation/upgrade through the final signed assistant, ordinary-permission launch, actual v2 loaded identity match, active NR submissions, F8 feedback output, and optional uninstall/restore check. Witcher 3, broader GPU/driver combinations, HDR and other games remain unverified for this final installer candidate. UAC display behavior and visual UI acceptance also require Windows interactive acceptance; automated helper tests cover result semantics without requesting elevation.

Current constraints are explicit: first two D3D12 profiles only; unknown hosts/NR identities/shared external addon folders are refused; already edited plugin INI is preserved; the installer accepts a flat signed component ZIP or its extracted `manifest.json`; per-game addon file paths must stay below the plugin's existing MAX_PATH limit. A lightweight addon-only update archive is not a complete installer payload.
