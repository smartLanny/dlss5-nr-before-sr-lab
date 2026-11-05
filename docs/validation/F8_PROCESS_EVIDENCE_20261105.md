# Retained F8 child-process evidence — 2026-11-05

Follow-up to `3127d8b72cb26d5ed86f3f082638c000ffd829a8` in PR #349.

## Observed need

The SHA256-verified artifact described in
[F8 attempt isolation](F8_ATTEMPT_ISOLATION_20261105.md) ends at the F7 GPU/stream
gate but does not retain the child's diagnostic stderr. Capturing a generic
PowerShell transcript is not sufficient to distinguish a compiler failure,
assertion, process launch error, or incorrect generated host.

## Implementation

`scripts/f8_process_evidence.py` records each compiler/positive/negative child
in its existing isolated attempt. Raw stdout/stderr stream directly to files.
A JSON receipt retains argv, cwd, input SHA256 values, executable SHA256 when
available, exit code and output hashes. Separate 64-KiB tails remain available
when a full log exceeds the cloud collector's size allowance. Existing evidence
files are opened exclusively; repeated labels cannot truncate an older result.

Receipts distinguish prepared, completed, launch-error and interrupted states.
They deliberately do not contain a `passed` field. Exit zero is not archive,
shader, inference or game acceptance. Original assertions still decide success;
positive and compile failures still raise, and negatives still require their
original exact exit/FAIL checks. No native timeout, environment, cwd, compiler
argument, shader or product default is changed. Console decoding errors cannot
hide the retained raw evidence.

The controller invokes this helper at all four existing child call sites. The
positive execution binds its generated harness, driver, Core and coordinator;
negative executions bind the exact mutated generated source. This helper runs
from the checked-out validation scripts, not inside the staged product compiler
inputs; no runtime payload or source-export allowlist expansion is needed.

## Actual checks

```sh
python tests/test_f8_process_evidence.py -v
python tests/test_f8_attempt_isolation.py -v
python -m py_compile scripts/f8_process_evidence.py scripts/validate-f8-controller.py tests/test_f8_process_evidence.py tests/test_f8_attempt_isolation.py
git diff --check
```

Linux: **14 process-evidence checks and 22 isolation checks passed**, no skips.
Process tests launch real Python subprocesses for byte retention, nonzero exit,
expected-negative capture, newline translation, large output and input hashes.
Explicit doubles cover interruption, forbidden relaunch and console failure;
one AST contract checks all four actual driver call sites in CPU and GPU modes.
Compiler, C++ host, GPU, WARP and NGX execution are not claimed. All 17 original
driver assertion ASTs remain equal to the prior source.

Prior commit `3127d8b7` had public portable CI **74 passed / 0 failed / 4 skipped**
in [run 41900743008](https://github.com/smartLanny/dlss5-nr-before-sr-lab/actions/runs/41900743008).
That job checked out PR merge `e7a8c4c3666970e102ee7fd2f099c2670acb0de3`, not a
new Windows binary. Its g++ and clang++ host jobs also succeeded; clang++
sanitizers succeeded. These results do not transfer to this follow-up source.

## Remaining checks and risks

The full new exact-source Windows canonical run, CPU/GPU controllers, native
negative controls, face/temporal gates and candidate packaging remain required.
The original F7 GPU failure has not been declared fixed by adding diagnostics.
No Release, main merge, Manager pin or game installation is authorized here.

Test-only logging adds disk IO and file hashing; child output reaches the console
after the child returns, while raw logs are written during execution. Receipts
have no acceptance authority. Local attempts retain full output and may grow;
they are not game telemetry. The existing cloud collector's recursive extension
and size filters remain unchanged, and public payloads still exclude executable,
raw screenshot and vendor bytes.
