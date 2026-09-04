# Helldivers 2 bridge feasibility

Status: bounded investigation; **no Helldivers 2 compatibility implementation
or successful native-SR/NR integration is claimed by beta0.3.7**.
Prepared on 2026-09-04. This task remains lower priority than the confirmed
lifecycle fixes and Spider-Man image-quality investigation.

## Supplied artifact

The user supplied `zz-hd2-dlss5-typeless-bridge.addon64` for inspection. Its local
file was re-hashed on 2026-09-04:

- Size: 162,304 bytes.
- SHA-256: `6296E34825196AD5EBF4FAFD86F0089E06C09FF5C57BB2D9748728157C57E5E2`.
- Prior static inspection identified an x64 PE with no embedded version
  information and no Authenticode signature. These facts are artifact identity,
  not evidence of behavior or authorship.

The inspected path waits for a RenoDX DLSS5 addon and intercepts NGX Evaluate
and Evaluate_C. Its format-specific path targets a Texture2D Output with
`R16G16B16A16_TYPELESS` (DXGI value 9), considers the compatible FLOAT format
(value 10), and temporarily substitutes the Output resource's GetDesc behavior
through a copied vtable so the downstream caller sees FLOAT. It restores that
substitution after the call.

This is a limited static finding about the inspected path. It is evidence for
a possible typed-view admission issue, not proof that every game path is safe,
that the artifact supplies a complete SR/NR pipeline, or that its runtime
behavior matches this project. No native SR creation/integration implementation
was identified in that bridge path. The bridge alone therefore cannot answer
whether native SR remains effective when combined with this addon.

Do not bundle, load automatically, or copy the vtable substitution into this
project. No source license or complete concurrency/lifetime contract was
provided with the binary. The independent experiment below uses ordinary
D3D12 views and does not change what a game's resource reports itself to be.

## The supplied logs fail before resource admission

The feedback archive contains duplicate attachment names. The two identified
Helldivers 2 NR logs are ZIP central-directory entries **2** (9,721 bytes) and
**10** (2,520 bytes), both named `附件/nr-before-sr.log.txt`; identify them by
entry ordinal and size rather than extracting by filename and overwriting one.
Both identify `beta0.3.3-dev-r1` on line 1. In each:

- Lines 6-8: Create/Evaluate/Release hook creation returns status 0.
- Line 9: `MH_EnableHook all status=10`.
- Line 10: `Hook installation failed; observer is inactive.`

The vendored MinHook enum defines status 10 as `MH_ERROR_MEMORY_PROTECT`, failure
to change memory protection. These logs do not show a successful observed
Feature 1/Feature 18 chain. A typeless-view adapter cannot fix the demonstrated
hook-enabling failure. The logs alone also do not identify the exact failing
target or the reason Windows rejected that protection change. Do not attribute
it to a protection product or anti-cheat without evidence.

The beta0.3.7 diagnostic addition records each registered candidate target's name,
address and `VirtualQuery` memory information after initial enable failure.
It also records operation, published/queued state, and every observed native
Feature 1 Evaluate boundary. It explicitly calls enable entries
**candidates**, because `MH_EnableHook(MH_ALL_HOOKS)` does not identify which
target failed. This improves the next investigation; it is not a workaround,
a successful hook installation, or a per-target proof.

## beta0.3.7 confirmation procedure

Use the beta0.3.7 addon and its matching log on a clean Helldivers 2 launch,
with `zz-hd2-dlss5-typeless-bridge.addon64` absent. Preserve the supplied
bridge only as an inspection reference; its confirmed SHA-256 is
`6296E34825196AD5EBF4FAFD86F0089E06C09FF5C57BB2D9748728157C57E5E2`.

Before judging image quality, the log must show `MH_EnableHook all status=0`,
per-target published lifecycle hooks, and a nonzero `F1-evaluate` count in the
panel or `[module] lazy rescan` line. A loaded DLL, a menu setting, or a
successful hook creation alone is insufficient evidence of native SR
visibility. If enabling still fails, preserve the per-target candidate lines,
the exact status code, renderer/API, addon and runtime hashes, then stop the
experiment. Do not load, bundle, imitate, or bypass the supplied bridge and do
not change memory protection or hook protection policy.

## Bounded next experiment

1. Request a clean current-candidate run using the player's intended native
   DLSS configuration and without the external bridge. Record addon/runtime
   hashes, renderer/API, game settings, hook installation results and the full
   NR log. First require successful lifecycle observation and a real Feature 1
   Create/Evaluate sequence. Do not infer successful SR from a loaded DLL or
   menu setting alone.
2. If memory-protection failure persists, retain the failure diagnostics and
   defer this task. Do not add memory-protection bypasses, protection-disabling
   steps, aggressive hook retries or a replacement rendering integration.
3. Only after hooks and native SR work, inspect the actual Color/Output resource
   dimensions, formats, flags, active subrects, view capabilities, exposure and
   resource-state contract. Verify the game's SR result and native quality
   setting are preserved before introducing NR.
4. If the sole remaining incompatibility is a proven FP16 typeless resource
   with valid FLOAT view compatibility, prototype an explicit
   `R16G16B16A16_FLOAT` SRV/UAV or the existing owned FP16 snapshot path. Require
   the appropriate shader-load/UAV support and usage flags; typeless format
   membership alone does not authorize a view or establish resource state.
   Apply admission by verified resource contract, not by executable name.
5. Validate native SR alone, full-resolution SR->NR, and NR->SR separately.
   Exercise ship, hellpod transition, mission gameplay and return/loading,
   including graphics-setting changes. Check correct SR extents, useful NR
   output, F6 zero-NR behavior, RR exclusion where encountered, exact-fence
   lifetime and D3D12 validation. Capture failure logs if the game exits.

Proceed only if this remains a small resource-view adaptation using the
existing observed native Feature 1 chain. If it requires a special feature
carrier, broad command-list state tracking, a replacement SR implementation,
or changes to hook/protection mechanisms, park it as requested. The existing
binary is a feasibility clue, not a dependency or acceptance baseline.

## Evidence boundaries

The r1 reports include no-effect, flicker and crash complaints, but the logs
above establish hook enable failure rather than a crash stack or a GPU fault.
No current-build game test was performed for this document. No redistribution
of the supplied bridge or proprietary runtimes is part of this investigation.
