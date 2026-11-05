# F8 GPU fixture descriptor-table repair — 2026-11-05

## Observed failure, not an inferred game bug

PR #349 source `1c06d3cf2ed3bd2be1701e1a03226a2124469dca`,
[Windows run 41902231464](https://github.com/smartLanny/dlss5-nr-before-sr-lab/actions/runs/41902231464),
job `126828395936`, failed the F7 GPU controller after compiling successfully.
The retained Actions log at `2026-11-04T20:30:59Z` reports an uninitialized
SRV at descriptor offset **10**, followed by `D3D12 debug layer error`.
This is actionable evidence; it supersedes guesses about the earlier generic
`512 ROI production F7 GPU/stream validation failed` wrapper message.

Current production declarations use 13 SRVs and 8 UAVs. `t10`, `t11`, `t12`
are the three `Texture2D<float4>` low-resolution dual-layer inputs. The F8
fixture recreated a CPU descriptor mirror but did not initialize these slots.
Source review also found that the F8 mirror omitted the Lanczos `u7` null UAV.
The actual observed first failure is t10; the other omissions are source findings.

## Repair

`tests/f8_gpu_integration.inl` initializes these four otherwise-unused slots
immediately after each of its two CPU-only heap creations. The existing source,
proxy, neural, output and capture bindings, resource lifetime, exact fences,
recording tokens, pixels, five-plane checks, PNG/ZIP assertions and budgets are
unchanged. Static assertions require reviewing this fixture when the table grows.
No production C++, HLSL, model, default, timeout or debug-layer setting changes.

`tests/test_f8_descriptor_coverage.py` compiles the actual helper with a recording
CPU seam. All four omissions and an incorrect but valid slot are independently
rejected. This is not a D3D12 test; the unchanged canonical GPU controller must
still prove the real binding and image contracts on Windows.

## Durable evidence and offline review

The old artifact globs retained only `build/**`, while the F8 logger wrote to
`test-output/f8-{controller,gpu}-v4/**`. Recursing within build was insufficient.
`collect-f8-evidence.py` now preserves the two exact test namespaces under
`build/cloud-evidence/f8`, without changing the artifact's established root.
Raw logs and JSON retain their bytes; generated C++/helper source receives a
`.txt` suffix so the existing text uploader includes it. Size omissions and
missing outputs remain explicit, never passes. Runtime binaries, PNGs and ZIPs
are not collected. Linked sources are rejected.

A separate early `Source-review-*` artifact contains **committed project blobs**
and their Git/SHA256 identities for offline failure review. It has no Git history,
untracked files, downloaded build dependencies or build outputs. Git EOL/export
attributes cannot change the archived bytes. Gitlinks remain explicit external
commit references, not bundled dependencies. This artifact is not an installable
candidate, and publishing it does not bypass any Windows gate.

## Local validation of this change

- 4 descriptor tests passed using g++: exact helper, both call sites, all four
  independent omission controls and wrong-slot control.
- 5 source/evidence tests passed, including actual Git export with dirty/untracked
  files and CRLF attributes, raw-byte retention, size/missing receipts and links.
- Previous 22 attempt-isolation and 14 process-evidence tests still pass.
- Fetched baseline blobs matched: F8 fixture `a508fa804729ceebd12d1d464227c929cd8854e4`,
  workflow `a8c2952a4ca06cb3583c60e414a31571670d6efd`, cloud routing test
  `1f529f29bd99c2b4a5d13bafded02207a6dc74bc`.
- Full cloud routing and production compilation require the complete checkout;
  this initial local validation used the exact fetched subset, not a full clone.

## Remaining acceptance

Re-run the current Windows canonical, all original CPU/GPU negative controls,
face and temporal gates, and packaging. Check adjacent codec fixtures for the
same table-growth drift. No full-canonical pass, inference, game performance,
image-quality benefit or release readiness is claimed by this source repair.
