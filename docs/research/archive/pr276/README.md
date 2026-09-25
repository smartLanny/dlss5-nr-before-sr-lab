# Historical PR #276: preserved research and reproductions

These four original files are preserved from
`ebb74927d48c440c525d90f446920a3bdb9b9372`, with original paths below this
directory. `provenance.json` records their original Git blobs and SHA256 values.
The original branch and discussion remain available. This archive does not
merge an old Core tree, change settings, or enable a runtime feature.

The continuing implementation is [unified #300](https://github.com/smartLanny/dlss5-nr-before-sr-lab/pull/300),
coordinated by [#294](https://github.com/smartLanny/dlss5-nr-before-sr-lab/issues/294).
Use [the current handoff](../../../interop/UNIFIED_HANDOFF.md) and
[unified4 delivery](../../../interop/UNIFIED4_DELIVERY_20260921.md), rather than
this historical plan's layer dimensions, defaults, PR states or claims of what
was not implemented on its original date.

## How to interpret the old diagnostics

- `tests/private_sr_storage_audit.py` deliberately reproduces the old B=4 FP16
  loss. Default exit 0 means the old defect reproduced; it does not mean a fix
  passed. `--require-small-edit-survival` is expected to exit 1. Both outcomes
  were rechecked when archiving. Optional production-header compilation remains
  pinned to Git blob `740e4a943866622fea0b354b6f80e73ec9b4fb6f`; it was not rerun
  in this archive operation.
- `tests/d21_colour_budget_probe.cpp` describes the old D21 producer/consumer
  mismatch. It requires the historical header and explicitly reports its
  limited scalar scope. It was preserved, not rerun against the newer budget
  consumer as an invented regression gate.
- Neither diagnostic is added to automatic CI or represented as current Core,
  shader, private-SR, AMDOF or game validation.

The research PR can be archived as superseded after this preservation is
published. The implementation and game issues remain open according to their
actual acceptance status, including the private-SR and temporal work.
