# Archived scripts

These scripts belong to frozen release lines and are not used by the canonical
build (`scripts/build-unified-candidate.ps1`), any workflow, test or handoff
tool. They were moved here unchanged on 2026-09-30 to keep `scripts/` focused on
current entry points. Git history keeps their full provenance.

| Script | Former use |
|---|---|
| `apply-beta04-r3-audit.py`, `apply-beta04-command-state.py`, `apply-beta04-result-resize.py`, `finalize-beta04-r4.py`, `upgrade-beta04-gpu-harness.py` | One-off 0.4 source/harness updates driven by the retired `beta04-*` workflows |
| `publish-044-test1.py` | Publishing the 0.4.4-test1 release |
| `package-beta03.ps1` | Packaging 0.3 releases |
| `package-perf4-ota.py` … `package-perf12-ota.py` | OTA packaging for D21 Perf4–Perf12 candidates; current packages use `package-multi-api-candidate.ps1` and `handoff/assemble-two-runtime-packages.ps1` |
| `validate-050-acceptance.ps1` | Frozen dline21 acceptance workflow. It requires the old patch generator, exact historical source pins and the former `validate-post-transform.py --negative-control` interface. The original checkout at its recorded commit is needed to reproduce that run; this is not a Beta 13 gate. |

The scripts still resolve the repository root relative to `scripts/`. To run
one again, move it back to `scripts/` first; do not use it to relabel or rebuild
a released package.
