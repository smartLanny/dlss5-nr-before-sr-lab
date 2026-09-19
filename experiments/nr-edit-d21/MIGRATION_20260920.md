# Preserved D21 residual adapter — 2026-09-20

This is an independent historical experiment, not part of unified1 or a tested game candidate.
The ten original untracked source files are preserved unchanged. Product baseline remains
`cbfb33919696a2840ed5154c91abaea56228b661`; the exported execution backend remains exactly
`04eda2e4601ac6c57a2be85149d9c817d8dbf8a0:experiments/nr-edit`.
Both histories remain available; the private recoverable archive includes the backend history.

## Validation actually performed for this migration

- Six Python precision/entrypoint tests passed.
- The actual chain-history header and D21 adapter compiled with LLVM-MinGW C++20,
  warnings-as-errors, and the executable passed on Windows. Core types are explicit
  fixtures, not actual GPU/NGX execution.
- `prepare_d21.py --prepare-only` exported the fixed backend and applied the real
  upgrade, including Python grammar checks and an input/hash receipt.
- This is **prepared-only, not staged or compiled**. The exported integration test
  requires `--generated-root`; generic unittest discovery is not its supported entry
  and was rejected. No generated Core/Windows full build/WARP/NVIDIA/game result is claimed.

On this committed descendant, use `--allow-descendant` (PowerShell `-AllowDescendant`)
in addition to the existing documented launcher arguments. Do not weaken clean-source,
fixed-backend, output-preservation or anchor checks.

## Before adapting to the unified successor

The adapter reads the old `dual_selected/dual_owner/full_instance/full_adopted` contract.
Review against the current per-layer instances, source owner, common working grid,
recording, queue and fence lifetime; do not copy it into current Core unchanged.
Preserve independent original-NR versus edit history, native Reset/warmup, exact motion
reference and exposure-texture fallback. Mid-chain recreation and adoption changes must
invalidate edit history without fabricating adopted output. Deferred residual SR,
pre-RR carry and post-RR clean-image reconstruction remain separate experiments.

No AMDOF implementation or new algorithm was introduced in this preservation step.
Existing #258/#259 compatibility, GPU and game acceptance remain outstanding.
