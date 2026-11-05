# F8 validation attempt isolation — 2026-11-05

## Source and scope

This is a validation-driver repair for PR #349, not a rendering, image-quality,
frame-rate, memory-saving or game-compatibility claim.

Base: `a33b94a84b93793a697c9afb1010f4eb5b715b2a`.
Original `scripts/validate-f8-controller.py` Git blob:
`bfb515fa491ad60c0ff91399d67881dc5c0461f1` (reconstructed bytes independently
checked with `git hash-object`).

## Problem and change

The old driver shared one directory per CPU/GPU mode and selected the newest
`run-*` by mtime. Repeated or overlapping runs could overwrite generated hosts
and executables or inspect a different invocation's result. Its C++ fixture
names result directories by PID, so negative controls sharing the positive
output root could collide before reaching their intended behavior.

Each invocation now uses an atomically created `attempt-*` directory beneath
the existing artifact root. Result selection accepts exactly one non-symlink
real `run-*` directory from that attempt. Missing or ambiguous output fails;
there is no old-output fallback. Each negative executable receives a separate
output directory and the exact same screenshot fixture bytes. Positive output
and failure evidence are retained. Negative receipts identify their own roots.

All 17 pre-existing Python assertions have identical ASTs. All nine C++ mutation
pairs, their expected exit/FAIL requirements, compiler commands, PNG checks,
archive checks, shaders, production coordinator and product defaults remain.
The new test is discovered by the existing portable `tests/**/test_*.py` entry.

## Executed validation

Run from the source checkout:

```sh
python tests/test_f8_attempt_isolation.py -v
python -m py_compile scripts/validate-f8-controller.py tests/test_f8_attempt_isolation.py
git diff --check
```

Linux results: **22 passed, 0 failed, 0 skipped**. Tests execute the driver's
actual AST blocks and real filesystem operations, including 32 concurrent
setups. Compiler/renderer subprocesses are controlled doubles. This is not a
Windows compile or C++/WARP execution result.

The same 22 tests were also executed against the exact original driver via
`NR_F8_DRIVER_UNDER_TEST`: **13 failures and 4 errors**, exit 1. Independently
restoring each old behavior in the repaired driver was rejected:

| Restored behavior | Failures | Errors | Exit |
|---|---:|---:|---:|
| Shared top-level output directory | 8 | 1 | 1 |
| mtime result selection | 3 | 3 | 1 |
| Shared negative-control output directory | 1 | 1 | 1 |

These are Python orchestration counterexamples, not the nine compiled native
behavioral negative controls. Those must still run on Windows.

## Latest existing Windows evidence and remaining blocker

[Run 41581790594](https://github.com/smartLanny/dlss5-nr-before-sr-lab/actions/runs/41581790594)
at the base source failed canonical validation. Its evidence artifact
`13207646201` was downloaded and independently verified:

- bytes: `232560`
- ZIP SHA256: `8845093b1f3262c462d28e8b7e5dc943b4de1f2a52840759c5e318db9cea7630`
- frozen manifest: source commit equals the base above; `source_dirty=false`
- retained transcript ends with `512 ROI production F7 GPU/stream validation failed.`

The actual current canonical entry runs the controller normally and then with
`--gpu`; this error belongs to the latter invocation. The retained transcript
and artifact do not contain enough child stderr to establish the underlying
assertion. Conflicting expanded job-log tails are not used to assert a root
cause. Attempt isolation is a reproduced defect, **not proof that this GPU
failure is fixed**. A new exact-source canonical run is required; no test is
removed, and no failed or skipped gate becomes a pass.

## Review and acceptance

Risk is limited to validation output layout: external consumers must find the
new attempt directory rather than assume `f8-*-v4/controller.exe`. The existing
cloud evidence collector recurses under `test-output`; executables, captures
and vendor payloads remain excluded. Attempts intentionally accumulate locally
for diagnosis; no automatic deletion of prior evidence is introduced.

Required next checks: portable CI; real Windows CPU and GPU controller runs;
all nine native negatives; full canonical, face and temporal gates; verified
Core-only packaging. NGX/hardware/game image quality, FG and whole-frame cost
remain separate acceptance. Do not merge main, publish a Release or change a
Manager pin on the strength of this Python-only result.
