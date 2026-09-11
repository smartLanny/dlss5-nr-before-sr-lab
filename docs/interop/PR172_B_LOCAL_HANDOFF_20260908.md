# PR172 B local implementation handoff — 2026-09-08

## Scope and source identity

- Fixed plan read from `0838ffb549a069dbcef9ac02914907be851d805d`.
- Frozen input was PR172 latest
  `229c908c47bd2cff0e6a464516fb86da725a5c5c`.
- The locally available remote-tracking head for PR183 is
  `origin/codex/work-percent-config-20260908@9622deee269650fd495a147564b5e69477c7e6e5`.
  Its bounded work-control behavior comes from ancestors `38bcbd7` and
  `3411a51`; the later commits are diagnostics/receipts and were not copied.
- No push, PR, release, tag, main merge, package staging, WARP, NVIDIA, game,
  or GPU timing run was performed.
- Creating local branch `codex/172-b-safety-config-20260908` failed because
  this sandbox can read but cannot write the shared worktree Git metadata.
  The edits therefore remain on detached `229c908` in this already isolated
  worktree. No existing ref or worktree was changed.

## Production defects fixed

### 1. Current PR172 source did not compile directly

The current source already called `nr_post_settings` from F8 and the settings
UI, but only the staging transformer injected its definition. A direct
bilingual production compile failed with ten undeclared-identifier errors.

The canonical source now includes `nr_post_compat.h` and
`nr_post_settings.inl`, and polls the settings module from the existing Present
poll point. `patch-post-compat.py` is idempotent for these already-integrated
anchors, so generated staging does not duplicate them. This is wiring only;
post algorithms and routing rules were not changed.

### 2. Invalid work-size requests were silently turned into different work

The previous loader normalized malformed values. For example Custom 25% was
clamped to 50%, changed the real work extent, and triggered history rebuild as
if the unsupported request had been admitted. Partial strings and invalid mode
values also lost their original evidence.

`nr_work_config.h` now provides strict, locale-compatible Win32 INI parsing:

- fresh absent configuration starts at Reference / 1.0, exact `N=R`;
- an existing file without work keys retains the legacy Quality / 0.75 pair;
- every valid explicit old preset/custom pair in 50–100% is retained exactly;
- invalid, partial, nonfinite, overflow, underflow, and 1–49% custom requests
  retain the previous valid pair and remain visible in the INI;
- only `WorkMode=5` plus exact `CustomWorkScale=0` consumes the existing
  master-off control, while retaining the last valid precision;
- both reload and the locked `ApplyConfig` path use the same admission rule;
- failed persistence latches the existing session-only fallback.

The shipped config and aggregate default are Reference / 1.0. Final effect
strength and model strength remain 1.0. The public UI remains 50–100%. All
Lanczos2, Catmull, luma/colour EditField, independent composition, and layered
composition switches remain default-off.

### 3. “Restore recommended picture” retained dev7 experiments

The helper predated five dev7 fields, so an explicit recommendation reset could
leave Lanczos2, luma/colour EditField, Catmull restoration, or independent
composition enabled. It now explicitly restores all five to off/ExactArea.
The historical recommendation button still selects Quality / 0.75; this is
deliberately distinct from the fresh-install Reference / 1.0 default. Master
enable and provider-specific depth/jitter/R8 contracts remain preserved.

## Generator locks

`prepare-multi-api-beta.py` retains exact blob admission and now covers the
new config/parser/reset dependencies. Important final blob IDs are:

| Path | Git blob |
| --- | --- |
| `config/nr_before_sr.ini` | `21afbc59e858f57b2f987fa57bb728a7f67b55b1` |
| `scripts/patch-post-compat.py` | `ed59244e6d8515a8d92a1f125bcc84e70af19e21` |
| `src/nr_before_sr.cpp` | `8cfa13ec90a8b8ea8843efa89aa3ac5017987e4d` |
| `src/nr_settings_ui.inl` | `84dfe33a21bd0dd88a37fd66084b7bf7d7f4f923` |
| `src/nr_recommended_picture.inl` | `76cb0b36303366350ac2c85e26de850697ae27de` |
| `src/nr_work_config.h` | `2621edc3b84ae83cbdb31ea6056f5b6a5dd81ec6` |

The generator intentionally resolves `HEAD:path`. It cannot pass before the
owner reviews and commits these edits. After that commit, run the normal
generator; do not weaken or bypass the pins. This direct dirty-source build has
no new `source-manifest.json` or `validation.json` and is not a distributable
candidate.

## Local dependencies and actual commands

- llvm-mingw cached toolchain: clang 23.1.0, target
  `x86_64-w64-windows-gnu`, UCRT x86_64 bundle dated 2026-08-26.
- ReShade source dependency: `4a50d1eddace85734871d91792ff214f13f66c01`.
- In-tree MinHook tree: `884aca536dbb4c92c6a8dd54512c1378b3ac3170`.
- MSVC: 19.44.35228 from VS Build Tools 2022.
- Windows D3D12/DXGI/D3DCompiler import libraries are supplied by the pinned
  llvm-mingw bundle for the production build.

Set the two local dependency roots outside the repository, then the successful
production build was:

```powershell
./scripts/build.ps1 -ToolchainRoot $Toolchain -ReShadeSourceRoot $ReShade
```

Focused host checks actually run:

```powershell
python scripts/validate-work-config.py --compiler $Compiler --build-dir $WorkConfigBuild
python scripts/validate-recommended-picture.py --compiler $Compiler --build-dir $RecommendedBuild
python scripts/validate-recommended-picture.py --compiler $Compiler --build-dir $RecommendedBuild --negative-control
python tests/multi_api/test_post_patch.py
python tests/multi_api/test_picture_integration.py
python scripts/validate-edit-field.py
python scripts/validate-color-edit-field.py
./scripts/validate-codec-shader.ps1
./scripts/validate-codec-gpu.ps1 -ToolchainRoot $Toolchain -CompileOnly
```

The generated 67-case work-control harness was also compiled with MSVC
`/std:c++20 /EHsc /W4 /WX` and executed successfully.

## Results

- Strict work config: 67 Win32/CPU cases passed under LLVM and the same 67
  passed under MSVC; zero GPU/NR calls.
- Recommended reset: 468 checks passed. The independent negative oracle
  produced the expected 12 wrong-colour-default failures and returned success.
- Post transformer: 8 tests passed with 1 existing skipped fixture.
- Generator pin integration: 4 tests passed.
- Existing luma EditField host wiring passed.
- Existing colour EditField math/wiring passed 19,950 finite/HDR/extreme cases.
- All 11 production SM5 entries compiled. Existing shader warnings were not
  hidden.
- Production codec C++ fixture compiled with `-CompileOnly`; no WARP dispatch.
- Full English/Chinese Core and `nrchain_nvngx.dll` linked successfully.

Local compile-only artifacts (dirty source identity, never publish):

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| English Core | 1,397,760 | `B60C5503B041ECE148543B964B3B65BA28304C881B482E408EBC919FC4F863AB` |
| Chinese Core | 1,420,288 | `075803FEC296D1074C83D244E3E8E75891AA209D8525413B39CB611CBB2CAEF1` |
| `nrchain_nvngx.dll` | 8,192 | `46041A5FF91AE2FD907E310D132AABC3C4A1ECD48DACE511B8672909D5D9C2FB` |

Both Core binaries embed base commit `229c908...` and the build script's dirty
marker. These hashes prove only the local compile/link result.

## WARP command reserved for the owner’s serial GPU slot

Do not run until the parent grants the GPU slot:

```powershell
./scripts/validate-codec-gpu.ps1 -ToolchainRoot $Toolchain -CombinedLowGrid
```

This is the smallest existing combined WARP route that covers all-off fallback,
RG luma field, RG luma plus Catmull, RGBA colour field, Lanczos input, and
invalid-input protection. The present change does not alter shader math, so no
new raw-evidence directory is requested by this handoff.

## Remaining gaps and next action

- Owner source review and a real local branch/commit are still required because
  this sandbox cannot write Git refs or commits.
- After commit, verify every generator pin against `HEAD:path`, generate a clean
  candidate, and create its real source manifest/validation record before
  citing a build source.
- WARP has not run for this dirty source. NVIDIA Feature 18, real SR/RR,
  resource-allocation failure, dynamic-resolution/history behavior, and native
  fallback remain untested here.
- No image-quality, 5% median/P95 GPU-cost, VRAM, long-tail, game, HDR, reset,
  or lifecycle result was produced. These remain owner/GPU/game acceptance
  gaps, not passes.
- No production algorithm, extra NR pass, cascade, face mask, or public
  sub-50% mode was added.
