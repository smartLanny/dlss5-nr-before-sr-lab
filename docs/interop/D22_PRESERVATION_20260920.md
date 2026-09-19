# D22 dirty-candidate preservation — 2026-09-20

Continuation baseline is the verified remote commit
`f0ca84c64a76c3b3e9fe02f83b424900a86c2407`, not the older uncommitted candidate
based on `8bfd857c313dba24ca40da1be386a554e2800bb3`.
All original dirty/ignored/untracked bytes, index and history have an independently
verified private recovery archive. No old candidate file was overwritten in place.

## Per-file disposition

- Patcher, Windows validator and four Windows fixture files already match coreentry2
  (one contract header differs only by line endings).
- The current runtime tests lifecycle readiness before symbol string handling;
  retain that earlier shortcut. Retain `coreEntry.schema=1` in feedback.
- The old `build-d22-candidate.ps1` is superseded by `build-d22-coreentry2.ps1`.
  The latter additionally rejects the withdrawn speculative Evaluate alias helper,
  verifies generated worker/helper/feedback wiring, and binds the coreentry2 identity.
  Do not restore the old builder or its entry1 label.
- Generator pin/patch/copy/PE wiring is already present. The sole useful remaining
  metadata difference is preserved: the manifest describes the cold resolved Core
  Create boundary and explicitly states no handle inference or fault reset.

## Local validation and limits

The retained production helper passed the actual Windows GetProcAddress/MinHook
fixture: baseline negative control exits 11 and candidate exits 0, using isolated
fake NGX DLLs. This is not NVIDIA inference or game evidence. No complete bilingual
Core/WARP build or game deployment was performed for this metadata-only migration.

Continue with the existing coreentry2 builder in a new output directory before any
candidate delivery. Preserve lockperf state and fail-closed lifetime behavior.
A resolver cached before add-on installation or a non-Win32 lookup path is still
outside the demonstrated boundary. Issue #223 remains open pending actual adoption
and visible result in a fresh game process.
