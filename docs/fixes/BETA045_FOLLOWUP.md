# beta0.4.5-fix1: non-DX11 compatibility and UI candidate

This D3D12 branch builds on the published beta0.4.5, PR #117. 不含 DX11、0.5算法。
Not a Release; do not overwrite the existing beta0.4.5 tag or files.

## Runtime changes
- #121: start observation before a real Reset on cold members of an already
  verified device/vtable/interface family. No binding is guessed or copied
  between lists. Fresh successful Reset is still mandatory. A bounded cache
  evicts only records NEVER used as NR carriers, never queued plugin work.
- #123: admit only fully observed known-null viewport/scissor/blend/owned
  RTV/DSV state. Existing private CPU descriptor copies are deep-snapshotted
  and replayed; recycled application descriptor slots must not affect replay.
  Unknown/tainted/meta-command/queue/resource contracts remain fail-closed.
- #121 UI: fixed-height live status area stops checkbox/slider movement.
  F6/F7/F8 recognize a physical down edge once; low-bit taps are best effort.
  Mouse Enable commits through the normal generation/capture guard without
  waiting for unrelated active controls. No global font/focus/layout changes.
- Recent process SR/NR/submission counts explain actual activity separately
  from Enable. They are historical counts, not FPS or visual quality proof.
- Existing F8 independent TXT/ZIP and explicit HDR colour opt-in remain; fix
  the contradictory HDR caption. No proprietary runtime or INI is replaced.

## Verification
Production policy and shortcut tests, real native Reset hooks, rotating-pool
capacity/eviction/failed Reset/device/vtable/interface controls, and WARP
RTV/DSV recycled-handle pixel fixtures. Old lazy-only and wrong RTV replay
are explicit negative controls. Full Windows/shader/static analysis/dual-
language build checks must pass on the PR head before a test package is used.
No actual game, NVIDIA model, HDR quality, FG cadence or performance result
is claimed. Prospective observation adds bounded CPU/memory/descriptor work.

## Minimal player test
Exit game, back up the old addon/companion outside its directory; install ONE
language and matching nrchain_nvngx.dll, keep INI/NVIDIA DLL/ReShade/providers.
ACShadows RTX5060: old scene, open Home, click Enable once and briefly hold F6;
check visual flicker, not just label changes. Starfield RTX5070Ti Laptop: same
scene, F6 compare. Use F8 TXT/ZIP; F7 only for convenient visual evidence.
Stop and revert on persistent black screen or crash. No driver swap/debugger.

## Unresolved
#122 NBA2K26 has no observed Feature1 carrier; this is NOT NBA2K27 #68 and is
not fixed by changing null-state policy. #82 full independent NR precision
requires actual post-SR resampling/guide/history work; no fake post slider is
added. Other game flicker, #49/#18 crashes and #63 external MFG ownership are
not declared fixed. All game issues stay open pending exact-build evidence.

## Public API contracts
Microsoft documents RTV/DSV CPU descriptors as reusable immediately after
OMSetRenderTargets. This is why a copied raw application handle is insufficient:
https://learn.microsoft.com/en-us/windows/win32/direct3d12/non-shader-visible-descriptor-heaps
GetAsyncKeyState documents bit 15 as physical-down and bit 0 as unreliable;
short unobserved taps remain best effort, not a promised lossless key stream:
https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate
