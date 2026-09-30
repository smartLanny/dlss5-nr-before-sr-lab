# 0.4.3 candidate: feedback integrity

This is a source-only candidate on top of PR #56, which depends on PR #48.
The repository owner chooses the final release commit and version identity.
No parser result is a substitute for game/runtime verification.

## Player-facing use

For a build containing #56, use **F8 first for a small core feedback package**.
Use **F7 for an image supplement** when the problem concerns colour, lighting,
faces, detail or temporal artifacts. F7 produces a new paired core package;
send that core with its image supplement, not an older F8 ZIP saved elsewhere.

Current released builds may still have the previous F8 behavior. Identify the
loaded addon by its source commit and measured SHA256, not only its filename,
the version typed into a feedback sheet, or the presence of a ZIP.

Neither action uploads automatically. The standalone CMD collector remains a
separate offline rescue path; this change does not relabel its old format as the
8 MiB runtime core format.

## Analyst commands

```sh
python scripts/parse-core-feedback.py path/to/core.zip
python scripts/parse-core-feedback.py path/to/core.zip --image path/to/image.zip
python scripts/parse-f8-bundle.py path/to/legacy-or-standalone-image.zip
```

Analyze completed, immutable ZIP files. Do not modify or replace an archive while
it is being inspected. The parser does not extract archive entries to disk or
execute their contents. JSON on stdout is a diagnostic result, not an upload.

When identity is unavailable, inspect the core alone. Evidence of a cancellation,
missing log, invalid runtime snapshot or zero NR submissions remains useful; it
must not disappear just because the package is partial. A failed pairing check
means the two supplied archives cannot establish the requested pair, not that
all information in the core has become unusable.

## Meaning of the fields

| Field | Meaning | Does not prove |
| --- | --- | --- |
| `manifest_verified` | Manifest/member listing, metadata bytes, CRC/SHA and bounded core schema checks passed. | Trusted authorship, signature validity, truthful source text or a working game. |
| `required_complete` | Required collector evidence is present and does not contradict cancellation, snapshot validity, metadata truncation or required log scan coverage. | NR ran successfully or a visual bug is fixed. |
| `complete` | The archive declares completion without missing records, incomplete scans or rejected inputs. | Completeness of all possible diagnostics or game runtime validation. |
| `pair_verified` | The recorded core session, source SHA, addon SHA256, dirty state, core configuration and required-complete reference agree. | Same frame, same final SR image, complete ROI sequence, clean source or trusted origin. |

The image's `core.complete` refers to required core evidence, **not** the core
manifest's overall `complete`: an optional previous-session log may be absent
without invalidating required core evidence. Explicit comparison modes may
change the later image configuration; matching `core.config` refers to the
original core snapshot, not to every image group's requested settings.

## Rejected contradictions

The current NR log must be declared required. It may be included or explicitly
missing, but silently omitting its record cannot yield a complete core. All
completion and required fields are actual JSON booleans; `"true"`, `1` and
missing fields are not equivalent. Missing reasons are nonempty strings.

A cancelled capture, invalid snapshot or truncated metadata cannot claim required
completion. A required summary with incomplete scan coverage cannot claim full
coverage. Optional missing evidence remains a valid partial core.

For verified pairing, source commit and addon SHA256 must be full known hashes,
not two matching empty/unknown values. The same source commit with a different
addon binary is a mismatch. A matching dirty build can pair with itself, but
that does not certify clean or released source.

The existing budgets and member allowlist remain. Silently NUL-normalized ZIP
names, nonregular members and non-JSON numeric constants are rejected.

## Regression evidence and integration

The regression suite uses real ZIP fixtures and the production parser. Five
specific tests are also executed against production revision `4498e47` as a
negative control; all five must fail assertions without import/runtime errors.
The current implementation must pass the same tests plus the rest of the suite.

Run:

```sh
python -m unittest discover -s tests -p test_core_feedback.py -v
python -m unittest discover -s tests -p test_f8_parser.py -v
```

CI covers Python 3.11/3.13 on Linux/Windows. The full addon workflow also runs for
PRs targeting the #56 branch, so the stricter parser is exercised with the actual
Windows core collector, file I/O and ZIP writer, alongside existing shader,
resource/command-state/WARP and English/Chinese build checks. Refer to the exact
latest run for its status; this document does not predeclare passing results.

No NR/SR shader, GPU resource format, queue/fence policy, RR ownership, null-PSO
admission or HDR colour protection is changed by this incremental PR. #49 crash,
#38/#50 null-PSO bypass, and #51/#53 version-dependent reports remain separate
runtime investigations. Do not close them because feedback tooling passed CI.
