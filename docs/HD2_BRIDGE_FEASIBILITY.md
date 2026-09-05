# Helldivers 2 bridge feasibility

Status: bounded Feature 11 lifecycle experiment; **no successful Helldivers 2
native-SR/NR integration or compatibility claim has been established**.
This checkout's formal diagnostic identity is `beta0.3.8-hd2-exp1` with edition
label **HD2 Feature 11 observer experiment exp1; compatibility unverified**. It is compiled and packaged
separately from ordinary beta0.3.8 so logs cannot confuse the two editions.
Updated on 2026-09-05.

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

The earlier diagnostic addition records each registered candidate target's name,
address and `VirtualQuery` memory information after initial enable failure.
It also records operation, published/queued state, and tracked native
Feature 1 Evaluate calls that reach feature lookup. Master-off, another NR
owner, observer-installation and lifecycle fast bypasses return before this
counter. A positive count proves an SR boundary observation; zero in those
conditions cannot establish that native SR is absent. It explicitly calls enable entries
**candidates**, because `MH_EnableHook(MH_ALL_HOOKS)` does not identify which
target failed. This improves the next investigation; it is not a workaround,
a successful hook installation, or a per-target proof.

The current source also carries the common relay-allocation fix for MinHook
status 9 (`MH_ERROR_MEMORY_ALLOC`) when an NGX module is loaded at an unusually
low address. The supplied HD2 logs instead fail later with status 10
(`MH_ERROR_MEMORY_PROTECT`). Expanding the signed-rel32-safe relay search does
not bypass page protection and does not establish that the demonstrated HD2
failure is fixed.

## Feature 11 observer experiment in this branch

The current experimental edition also observes the D3D12 Create, Evaluate and
Release exports of `nvngx_dlssg.dll`. It records Feature 11 only after an
authoritative successful Create returns a non-null handle. A module name or
mutable Evaluate parameters are never used to adopt an unknown handle. Null
handles remain native invalid-call paths, while an unknown non-null handle still
activates the existing fail-closed lifecycle gate.

The hook registries now reserve space for the additional lifecycle entry points.
Initial and late DLSSG hooks are published in an optional transaction after the
ordinary SR and RR-capable observers. This publication does not advance the RR
observer epoch. Failure to create, queue, pin or enable only the Feature 11
observer is logged as incomplete and does not deactivate an otherwise complete
ordinary SR observer. This boundary matters for the earlier HD2
`MH_ERROR_MEMORY_PROTECT` evidence: adding an optional observer must not recreate
the same all-hooks failure for the main path.

This experiment does not change resource formats, create SR, replace the game's
injection path, or use the supplied bridge's COM/vtable substitution. It can
only address the specific case where a real Feature 11 wrapper-to-core sequence
previously arrived as an unknown non-null handle.

The latest HD2 feedback proves a narrower startup race: `_nvngx.dll` and
`nvngx_dlssg.dll` appeared after the primary SR hooks, but their first unknown
Evaluate arrived inside the normal one-second Present rescan interval. The new
startup watcher checks only for those optional module arrivals every 2 ms for
the first 10 seconds, then every 10 ms for another 20 seconds. Hook publication
still uses the existing serialized transaction. This reduces the measured
window but cannot mathematically eliminate a DLL-load-to-Create race, so the
edition remains experimental.

If an unknown non-null handle still arrives, the first three occurrences now
record a bounded read-only fingerprint: standard SR resources and dimensions,
DLSSG discriminators, and caller module names with RVAs. The handle is still
passed to the native runtime unchanged and the add-on still fails closed. These
records are evidence for the next decision; they do not classify the handle.

## beta0.3.8-hd2-exp1 confirmation procedure

Use the `beta0.3.8-hd2-exp1` addon and its matching log on a clean Helldivers 2 launch,
with `zz-hd2-dlss5-typeless-bridge.addon64` absent. Preserve the supplied
bridge only as an inspection reference; its confirmed SHA-256 is
`6296E34825196AD5EBF4FAFD86F0089E06C09FF5C57BB2D9748728157C57E5E2`.

Before judging image quality, the log must show `MH_EnableHook all status=0`,
an `[HD2-startup-watch]` line,
an initial or late `DLSSG Feature 11 lifecycle` coverage line, and a nonzero
`F1-evaluate` count in the panel or `[module] lazy rescan` line with this addon
enabled and early bypasses resolved. A loaded DLL, menu setting, Feature 11
observation, or successful hook creation alone is insufficient evidence of
native SR visibility. If enabling still fails, preserve the per-target candidate
lines, exact status code, renderer/API, addon and runtime hashes, then stop the
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
