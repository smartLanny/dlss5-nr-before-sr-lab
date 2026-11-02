# Public-source integration — 2026-11-03

## Scope and retained identities

The owner authorized PR cleanup/merges and UI, performance and functionality work.
The repository was verified public. Main started at
`024f9cc32778c1056db364d76d087ee257da6502`.

Reviewed source inputs:

| PR | Exact source head | Disposition |
|---|---|---|
| #337 | `6c6a8865b2366c29eb7d920bcbe3f5726675920b` | Merged; original Beta14 receipts unchanged |
| #341 | `f9ef953faad0a31d280d27adbbe411433a352a47` | Merged; exact retirement/fence admission retained |
| #347 | `8078ae0ce705137428ec9d7396b2201d05ef4602` | Merged into #345 before #345 entered main |
| #345 | `e0789b35ccf58011d117538b96d8de643b7c8837` plus #347 | Merged as optional SDR source, default unchanged |
| #346 | `dc47485e184930d278eb9e2fd82f36e1c0906fb9` | Retargeted after #341; merged as Beta17 source candidate |

Main after those merges: `6af89478ae0d258f74fcb9583f95c74bd9bda3fb`,
tree `e82c5a556893486c9c40ec10b34bba838640a8c0`.
This change adds the UI, snapshot-copy optimization, portable warning repairs,
public entry documents, license propagation and public CI routing on that base.

## Changes and independent reviews

- UI: Picture retains everyday controls, Models contains per-layer networks and
  model parameters. The current-configuration summary is explicitly not an
  execution or INI-persistence receipt. Active-layer inheritance reset retains
  inactive settings. See [UI review](UI_OPEN_SOURCE_REFRESH_20261103.md).
- CPU: production `CopyNativeCommandState` copies tag/value/mask and only the
  required constants payload. Scalar slots skip 256-byte payloads; short masks
  use a fixed 16-DWORD prefix. Full-state differential tests, negative controls,
  COM/descriptor ownership review and benchmarks are in
  [snapshot evidence](COMMAND_SNAPSHOT_COPY_20261103.md).
- Portability: repair five demonstrated GCC diagnostic failures without turning
  off warnings or assertions. Large diagnostic snapshots initialize in place.
  See [original-head review](PORTABLE_PR_REVIEW_20261103.md).
- Public project: MIT for original source, retained RenoDX/MinHook/Oklab notices,
  updated README/start/status/contribution instructions. Notice export uses
  committed bytes; OTA includes only source-manifest-verified notices.
- Source hygiene: removed six pre-existing binary/raw-feedback payloads from the
  current public tree after checking exact Git blobs and active references. The
  original history is retained and still contains those payloads; this is not
  historical erasure. See `docs/maintenance/PUBLIC_SOURCE_PAYLOAD_REMOVAL_20261103.json`.
- CI: public source/CPU checks use an ephemeral Linux runner. Existing self-hosted
  PR jobs now require same-repository, non-fork, owner author and owner actor;
  their opt-in variables and full-build gate remain. These predicates do not
  replace GitHub workflow approval or runner-access isolation. Manual dispatch
  remains controlled by GitHub's repository permissions.

## Executed integration checks

| Check | Result and scope |
|---|---|
| Full portable suite, including UI/performance and warning repairs | 61 file-level PASS, 0 FAIL, 3 top-level SKIP; exit 0 |
| New public CI event matrix | 6 tests PASS, interpreting production if expressions and removing each gate as a negative control |
| New notice export and actual OTA ZIP path | 6 tests PASS; fixtures isolate PE/Windows services, no real binary claim |
| NR network runner | 48 execution records matched expectations; production policy/Create/caller/consumer/full adapter and 16 compiled behavior negative controls |
| Generic math + actual UI/receipt | 64,120 + 66 checks; six compiled behavior negative controls matched expected failures |
| Picture reset / actual unified UI | 50 reset checks; UI recorder, working-scale and transaction checks passed |
| `validate-043r2.py` | 52 checks and integrated source assertions passed |
| Official DXC v1.9.2609, actual embedded HLSL | 29 SM6 variants PASS, 0 FAIL |
| Core OTA policy | 6 tests PASS; release/canonical gates retained |
| Mainline maintenance | 5 tests PASS; historical documents remain unchanged |
| Diff hygiene | `git diff --check` PASS |

Portable top-level SKIPs are two Windows-only files and the missing captured
replay prerequisite. Some exit-zero unittest files also contain internal SKIPs;
the file-level PASS count is not a count of every case actually executed.
The two new 6-test files were added after the full-suite run and were executed
separately; they are not retroactively included in the 61-file receipt.

Commands (relative to the source checkout):

```sh
CXX=g++ python3 scripts/run-portable-tests.py --verbose
python3 scripts/validate-nr-network.py --compiler /usr/bin/g++ --output <new-directory>
CXX=g++ python3 tests/test_generic_continuity.py
CXX=g++ python3 tests/test_picture_reset.py
CXX=g++ python3 scripts/validate-043r2.py
python3 tests/test_public_ci_policy.py
python3 tests/test_source_notice_export.py
python3 scripts/validate-codec-dxc.py --source src/nr_before_sr.cpp --dxc <official-dxc> --output <new-directory>
```

## Remaining gates

The Windows DevSpace interface failed twice with an internal server error. No
new Windows canonical/SM5, WARP, real NGX, rendered ReShade UI, game capture,
frame-time or VRAM acceptance was performed here. DXC SM6 is not SM5 and does
not produce product shader-cache artifacts. CPU speedups do not establish FPS.
No binary/package was released, no Manager pin changed and no runtime/game file
was replaced. The prior package identities remain available for comparison.

#340 remains draft for full private-SR integration, GPU allocation and net quality
checks. #342 remains a research draft: its 147 files do not modify product src,
and its complete reproducible host/product admission still needs work. #343
remains draft for the combined F8 writer/capacity gate (337 KiB plus 24 KiB of
retirement text), not because a new memory leak or deadlock was proven.
