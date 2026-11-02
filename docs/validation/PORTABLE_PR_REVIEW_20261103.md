# Portable gate repair during public-repository PR review

## Source and scope

- Reviewed base: PR #341, `f9ef953faad0a31d280d27adbbe411433a352a47`.
- Repair commit: `e4cb1605` (`fix: keep portable lifetime and diagnostic gates warning-clean`).
- Host verification: Linux, Python 3.12, GCC 13.3.0, existing C++20 fixtures and their unchanged warning policies.
- No Windows build, WARP, NVIDIA NGX inference or game acceptance was performed for this repair.

The five GCC failures previously recorded by PR #346 were reproduced against
the #341 source. They prevented the Linux fixtures from executing their actual
assertions; no assertion or compiler warning was disabled.

| Existing entry | Reproduced failure | Narrow repair |
| --- | --- | --- |
| `tests/test_retirement_evidence.py` | Misleading indentation and `memset` on a class with default member initializers | Split independent statements; value-initialize the diagnostic Snapshot in place |
| `tests/test_retirement_privacy.py` | Misleading indentation in the metadata parser | Split the separator check and `first` assignment |
| `tests/test_menu_trace.py` | Mixed enum/integer conditional operands and rejected aggregate assignment | Use explicit uint32 operands; reconstruct the Snapshot in place |
| `tests/test_compat_route.py` | Misleading indentation in descriptor and recording-device observation | Split independent statements without moving their guards or locks |
| `tests/multi_api/test_rr_post_diagnostics.py` | Bounded `strncpy` truncation warnings | Use size-bounded `snprintf` with explicit NUL termination |

The retirement Snapshot is 86,624 bytes. `ResetSnapshot` uses
`std::destroy_at`/`std::construct_at` so default member initialization is honored
without a temporary full-size Snapshot on the F8 worker stack. The existing
stack-size gate remains enabled. Menu-trace reset uses the same in-place
initialization pattern, preserving nonzero enum defaults as well as zero fields.
Release permission, command-list identity, exact fence checks, route selection,
shader code and saved settings are unchanged.

## Executed verification

Each of the five entries above passed after the repair. The existing retirement
fixture passed 41 normal checks, four reduced-detail-capacity checks and all five
compiled behavioral negative controls. Its maximum-width projection remained
24,011 bytes within the unchanged 24,576-byte reserve. The privacy fixture
passed 36 checks and rejected the old policy at its expected assertion. The
compatibility route fixture passed all four tests, including 36 executed route
checks and the two expected behavioral negative-control failures.

The complete entry was then run on the committed repair source:

```sh
python3 scripts/run-portable-tests.py --verbose
```

Result: **56 file-level PASS, 0 FAIL, 3 top-level SKIP**, exit 0. The three
top-level skips are the two Windows-only files and the missing recorded replay
fixture. Some exit-zero files contain internal unittest skips; the runner's
file-level PASS count must not be described as every contained test executing.
This is portable CPU/source evidence, not a canonical Windows or game result.

An earlier diagnostic sweep, after only the first two repairs, recorded
52 file-level PASS, four FAIL and three SKIP. Three failures were the remaining
GCC issues above; the fourth was an unavailable historical blob in the partial
Git checkout. The final committed-source run had the required historical objects
and passed. The earlier run is not relabeled as successful.

## Independent PR review checks

- PR #337 at `6c6a8865b2366c29eb7d920bcbe3f5726675920b`: existing
  `test_core_ota_package.py` passed 6 tests, `test_mainline_maintenance.py`
  passed 5 tests, and `git diff --check` passed. No runtime DLL is introduced by
  its source patch. Its historical private-handoff wording needs the separate
  public-repository documentation update.
- PR #341's `src` tree and the reported historical canonical source
  `9b2636eb6cd80b540f80194fc6be771d14709c0b:src` were independently compared:
  both are `9d5404da757a8e35e3e14215b2d08d1dd31b2993`. This correspondence does
  not give the repaired source a new Windows or game result.
- Existing successful workflows for #337, #340, #341 and #343 ran the
  self-hosted portable job; their canonical-build and other gated jobs were
  skipped. Their green workflow conclusions are not full-build receipts.
- #343 still needs combined F8 capacity verification with #341's separate
  24 KiB retirement tail. The standalone 337 KiB tail becomes 361 KiB in that
  combination. Its memory observations are process-wide/sampled evidence,
  not a measured memory optimization.
- #342 changes research documents, experimental source and test tooling only;
  it does not modify the product `src/` tree or deliver a new game RR route.

Repository mutations and final integration decisions remain with the main
integration task. These review checks do not publish or replace any player
binary, runtime, Manager component pin or release.
