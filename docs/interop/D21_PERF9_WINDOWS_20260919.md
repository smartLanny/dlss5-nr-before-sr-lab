# Perf9 Windows build and owner test delivery

## Frozen build identity

Source commit: `e59649c5acbd1a67f5ddd741c76725f1cae97024`.
Version: `beta0.5-dline21-perf9`.
Incremental baseline: Perf8 `43081138a6f53c487fe1204bfddb89f0ff1933a9`.
The later handoff commit changes documentation only, not compiled production code.

The complete `build-perf9-candidate.ps1` ran from start to finish and returned
exit code 0. Original process status, console log, all gate receipts, source
manifest and linker maps are retained. No game folder was modified, no game or
NVIDIA inference was started, and no Release or stable merge was performed.

## Verification actually completed

| Area | Result |
|---|---|
| Chinese and English Core | Full compile/link passed |
| Canonical generation/build validation | 46 groups completed |
| LLVM Core static analysis | Both languages: zero analyzer diagnostics |
| Prior SRW, root-state and Perf3–8 gates | Re-executed and passed |
| New complete Lineage / exact snapshot gate | Baseline and candidate semantics passed; old budgets fail as expected, current budgets pass |
| Source ownership regression | 399 checks passed |
| Full Core descriptor CPU / WARP | 2225 / 669 checks passed |
| Source-transport WARP | 1700 checks passed |
| Native state restoration WARP | 24 scenarios plus wrong-state negative controls passed |
| Post-SR snapshot WARP | 1045 checks, 3 scenarios and wrong-pixel negative control passed |
| Capture preflight | Five mocked cases passed; real recorder never started |
| Generated source manifest | 229 actual files rehashed |
| Fixed Core inputs | 164 pinned inputs verified by generation |
| Shader identity | All 26 final CSO files equal the tested Perf8 counterparts |

These are separate facts: CPU-double semantics, native Windows synchronization,
WARP pixel readback, compilation and analyzer checks. They do not establish new
NVIDIA Feature18 inference, game FPS, weak-CPU gains, long-running game stability
or Manager UI import. Hosted CI is reported separately from actual PR check
annotations, never inferred from the local build.

D3D12 debug-layer checks are unavailable on this host. The existing explicit
`AllowMissingDebugLayer` mode ran real WARP pixels and negative controls, with
`snapshot_debug_layer_verified=false`. Ordinary inherited HLSL/unused/novtable
compiler warnings remain in the log; zero analyzer diagnostics is not a claim
that every compiler emitted zero warnings.

## Exact source provenance and the line-ending check

Independent verification re-read all generated files and both changed runtime
inputs against the frozen Git source. `nr_post_boundary_native.inl` is byte-
identical. The `.hpp` has the repository's existing `text=auto` / `autocrlf=true`
checkout expansion: 431 LF endings become CRLF. Raw byte equality initially
rejected it; verification then required exactly this LF-to-CRLF transformation,
no other character difference, and the exact filtered Git blob. This was a
verification-script correction, not a bypass or a change to the compiled file.
Both actual staged SHA256 values remain recorded in the source manifest and
`PERF9-FINAL-VERIFICATION.json`.

| Production input | Frozen Git blob | Actual staged SHA256 |
|---|---|---|
| `nr_source_frame.hpp` | `d6f7178e96958f12b00f3bd3a66473ba1686ccac` | `83c7f8e66585a0a490ebe45f7d8ef880428442b04eb94d754c272cabc12c55e4` |
| `nr_post_boundary_native.inl` | `7e2309ee4565385eacef2ce4bccf121464db3694` | `1e4ce6122e56559f6782c1edf94f617cc0045801bac88c1d6e36f158c237d3af` |

## Test package

`DLSS5-0.5-D21-Perf9-zh-CN-OTA.zip`, **1,651,112 bytes**.

| Artifact | SHA256 |
|---|---|
| Chinese OTA ZIP | `95214b6f01f23d4c92c1951e05da53184ab3d7b5204735e8acde6f670a19a14e` |
| Chinese Core | `ecc45017eee5921c700d26e5fc2e7f91c7fa6f88a3a2d2cb890e58d81fea362f` |
| English Core | `91c864a9c4a24ff4e17ba69d0f0652ac215bceb449109c98d8fa0eba47f64ec7` |
| Owned nrchain, unchanged from Perf8 | `1acf3cbe509a031be1763a8231cd81e6019aa3532368cd0b08a6c17bc94b70a2` |

The Chinese OTA contains one Core, its matching owned companion, instructions,
licenses and an `nr-branch-ota-v1` manifest. It contains no INI, model, NVIDIA
game runtime, capture, debug-symbol or unrelated bridge replacement. The
packager and independent verifier checked gate receipts, binary/source/version
identity, ZIP CRC, file counts, sizes and each archived payload hash. Package
creation does not imply it was installed or imported by Manager.

Two adjacent convenience entries, `Record-Perf9-NRSR.cmd` and
`Record-Perf9-SRNR.cmd`, use the checked recorder preflight and the owner's
existing recorder. The common script and both wrappers were syntax-parsed only;
no actual recording, F6 input or configuration write was performed. The compiler
check happens before capture, not continuously; avoid starting a build during
recording and inspect ETL background load again afterward.

## Next owner acceptance

The submitted Perf8 pair is useful for hotspot diagnosis but not a clean FPS A/B:
both runs contain about two CPU cores of shader compilation, while AFTER NR
extent and UI correction also differ from the earlier Perf7 comparison. See
[D21_PERF9_20260919.md](D21_PERF9_20260919.md) for the correction and actual values.

A minimal isolated performance check is one clean BEFORE-SR Perf8 recording and
one matching Perf9 recording, then a Perf9 AFTER-SR compatibility run. Keep the
latest UI correction setting (1), one layer, 2/2/2 controls, same scene and actual
work extents (BEFORE 2228x1253; AFTER 3840x2160). No D13/original-D21/Reno matrix
is needed. Each capture is OFF 15s -> ON 15s -> OFF 15s, followed by F8. Do not
change the selected settings to make the candidate look faster.

For an isolated AFTER-SR FPS claim a clean same-workload Perf8 AFTER baseline is
also required; the first AFTER candidate run can establish compatibility without
that additional performance claim. Verify actual NR successes during ON and
separate cold creation frames from steady windows. New candidate long-run and
weak-CPU validation remain outstanding.

Exit the game before replacing the Core/companion; back up old files outside all
addon search folders and keep exactly one Core. Persistent bypass, missing
effect, flicker or crash requires rollback and F8/Core log/build-hash bug feedback.
Bypass-induced high FPS is not acceptance. Issues #277/#272/#273 remain open.
