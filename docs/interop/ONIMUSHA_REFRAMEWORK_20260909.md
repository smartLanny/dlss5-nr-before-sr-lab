# Onimusha REFramework compatibility evidence

This is an owner-game comparison, separate from the dev14 menu ABI fix and
the dev15 first-recording change. It is not a blanket claim about all Capcom
games or all RE Engine builds.

| Controlled configuration | Owner result |
| --- | --- |
| dev14 Core, NR disabled from startup, DXGI ReShade 6.8 | Scene-entry failure |
| Same ReShade, Core not loaded | Scene-entry failure |
| ReShade also not loaded, native DLSS FG retained | Scene entry works |
| Same ReShade through DirectInput8 with official HookStreamline option, no Core | Earlier failure; reverted |
| Original 0.4.6 Core, original DXGI ReShade route, native FG retained | Scene-entry failure |
| Same 0.4.6/Core/ReShade/FG with official REFramework added | Owner enters scene; F6 changes the image |

The last comparison is supported by current NR evaluation/composition logs and
normal fence retirement. Earlier dev10/dev14 reports contained the same game
UD2 record with an all-zero CPU context, insufficient to recover the initial
fault. The additional comparisons establish a usable dependency combination
without claiming the exact upstream internal failure has been reconstructed.

## Fixed framework source

The installed non-VR component came from the [official nightly release](https://github.com/praydog/REFramework-nightly/releases/tag/nightly-01417-b6baf6b406efc65e077b99cb4d9ad25b0a0a9095),
source `b6baf6b406efc65e077b99cb4d9ad25b0a0a9095`. The ZIP's SHA256 matches the
GitHub asset digest: `ae8208c299422ae88ec520082ac1721fc7c06f41ef163ca1eeef10650fee0e7c`.
The extracted `dinput8.dll` is 23,055,872 bytes, SHA256
`504f2eb1cf98fad0b43ed95bcc4f457afeea676912b90889ac03f797c5acf23c`.
Its runtime log identifies the same commit and `onimusha_wots`.

Original 0.4.6 Core source is `9087a9efbc7bb53a3c79e7766a174534f49c412c`,
SHA256 `b68f2709a131c9ce0513b6366dbcc2e7d551bef5bcd41934075407378a48c090`.
ReShade remains 6.8.0.2155, SHA256
`0cee63f9c9f13f3ac909c5b4903f4dbb4b719a7ab3b4f13b0deaf83c814b94f7`.
No NVIDIA runtime was replaced for the successful comparison.

The supplied community OptiScaler archive was inspected statically, not executed.
It contains another REFramework DLL and an independent NR frontend without
ReShade. Its exact experimental source and successful installation entry are
not authenticated by the archive. Public NR state restoration is similar to the
existing RE9 envelope in this project and does not justify reimplementing it.

## Installer and configuration contract

The owner requested framework dependency handling in the existing Manager.
Use concrete game profiles, a trusted pinned component and transactional
ownership. Keep existing or externally modified loaders; publisher name alone
does not establish compatibility. Upstream-required and owner-tested profiles
are separate evidence levels. The [RHI manifest](https://github.com/RankFTW/RHI/blob/main/manifest.json)
also documents framework requirements for specific titles, including SF6, DD2
and Resident Evil Requiem; it is supporting evidence, not a test of this package.

For the fixed framework, startup copies root DLLs into `_storage_` on the
documented newer-engine path. Loaded root modules, including addon64 files,
can also acquire a copied and reported storage path. Core resolves INI/log
siblings from that reported path. A root INI remaining unchanged therefore does
not prove the effective settings were retained: a missing storage INI can cause
new defaults. First dependency installation must seed only an absent effective
INI from the existing configuration, preserve an existing effective INI, and
collect feedback from the current runtime directory. ReShade's own runtime INI
is independent and must be located separately.

Update/restore only exact owned root and mirrored Core files, preserving user
settings, adopted framework versions and unknown mods. Do not delete the whole
storage directory. The subsequent dev15 installation preserves both current
configuration locations and updates both verified Core copies; its restore
returns to the owner-tested 0.4.6 combination while retaining the framework.
The owner subsequently confirmed dev15 scene entry, menu and F6 all pass. The observed counters show 26,981 NR successes and 26,953 submissions with zero lifecycle bypasses or actual queue mismatches;
both recorded routes retired through completed fences on normal exit. This remains one owner/game configuration rather than a wider performance matrix.

The owner also reported dev16 testing passed after the route-block/defaults update.
Both installed Core copies match `d38400472424cc52883a154e0995a4ddd552ab3d90afa4aa5ccf19693c34e8af`
and the framework identity is unchanged. No new per-route counters or performance
measurements were supplied with that report; see the [dev16 record](../history/interop-0.4.7-d21/DEV16_ROUTES_DEFAULTS_20260909.md).
