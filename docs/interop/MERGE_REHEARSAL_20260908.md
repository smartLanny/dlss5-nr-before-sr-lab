# Priority merge rehearsal — 2026-09-08

The retained candidates were merged in an isolated checkout starting at current
`main@1bb20213db7dc3a3866d4a78108e4161823fa0b9`. The frozen rehearsal source is
`5922df15d9ff3ed12d3ad3aa7134fc178f154a6c`; any later commit adding this document
is documentation only. This is preparation for main integration, not a main
merge, release, or game installation.

## Executed order

| Order | PR and frozen head | Rehearsal merge |
| --- | --- | --- |
| 1 | #149 `2ad2b46792f709db3254051f00f04b2297aad81a` | `fdd631c` |
| 2 | #150 `d5ebc930ad5945819ee5b7f9ddda882bbcaff6b0` | `6205e76` |
| 3 | #151 `bd9dacb0bdde3698a4c41e6e88fc49802de1d364` | `ffd57bf` |
| 4 | #156 `7ccbc8fb576ee4389119b9bc561e8015d612758f` | `9fc44e0` |
| 5 | #155 `873472b22b66e90dba7acca383df492bfc5901fd` | `afa0226` |
| 6 | #157 `7aa3a55e79cb8f6f70cb407bd3dbdbf88880799e` | `a7efaf7` |
| 7 | #160 `10dd845c015dd8403f2b5f270e90dde6de271346` | `5922df1` |

#154 is closed as superseded; no separate merge was performed. Its ancestry and
complete finite-blend regression coverage are already included in #149. #150
currently targets the #149 branch to expose only RR diagnostics. After #149 is
merged into main, retarget #150 to main before merging it. Preserve ordinary
merge ancestry for this dependent pair; squashing #149 without a corresponding
#150 rebase would leave duplicate ancestry in the second PR.

## Actual conflicts and final pins

Only #156 conflicted, in `scripts/prepare-multi-api-beta.py` and
`tests/multi_api/test_format_patch.py`. The production C++ merged automatically.
Keep the combined core hash in both locations and retain the #156 feedback,
settings and runtime-load hashes; choosing either PR's whole conflict side would
discard part of the correct source identity.

| Pinned input | Final Git blob |
| --- | --- |
| `src/nr_before_sr.cpp` | `52a2782b29e8a9f43cec2c03cf69ec01143cd6d8` |
| `src/nr_command_state.inl` / `STATE_CURRENT` | `f0f13b0a8ac243f01cdf0f93a517440ac65f1394` |
| `src/nr_feedback_capture.inl` | `57f0455bd585207dd2f9e144e0f54616c8182e56` |
| `src/nr_runtime_load_evidence.inl` | `78e491e9465f4fd3cb453dbcf12a0cfd976ae514` |
| `src/nr_settings_ui.inl` | `cfaaf23c3b793fb79d3808bc9faf5aecfaa3d0ae` |

#155 already contains the three-name `NAMES` fixture correction. Do not replay
the old integration fix or restore the obsolete one-name fixture. #157 retains
only its source-off bootstrap guard. #160 adds configuration tooling, not carrier
runtime changes. Manager's independently published `8acf31f` is not a core merge
input; its bundled core remains the published `dccb5b3` baseline.

## Performed verification

- The entire Git `src` tree is `95fa5cb0492985c52020e9ad0538d121f83e51d3`,
  identical to the previously built and WARP-tested canonical `a2a3380` source.
- Relative to `a2a3380`, existing `src`, `scripts`, `tests` and `config` files
  are unchanged. The only additions in those directories are #160's five
  configuration/fixture files.
- Exact staging succeeded. All 102 generated files match their new manifest;
  101 are byte-identical to canonical staging. The only difference is the
  embedded `SourceCommit` string in `core/scripts/build.ps1`. Replacing that
  one source ID makes it byte-identical too. The manifest SHA256 is
  `45f574090841c3f50c5d30ec19a68c81b75163ddbc90e52f2e163a7e1f9e5c1c`.
- 28 focused Windows host tests passed, including actual source/pin staging,
  693 null-guard predicate/reason checks, RR production includes at O0 and O2
  with 50 cases each, and 12 exclusion-tool cases. Log SHA256:
  `1bc062237e4861ea5cec001b28c50085da47e5632d6395d55a510ffa5541675b`.

The prior `a2a3380` full compile/WARP evidence remains evidence for the unchanged
production source. The rehearsal itself did not repeat the full DLL build or
WARP run and is not described as a newly compiled binary. The maintainer is
running a separate 2077 RR game test; no game files or process state were changed.

## After main integration

Fetch the actual main head, verify every retained PR ancestor and the above
source pins, and use a clean isolated checkout. If any production file differs,
investigate before reusing canonical evidence. Run the existing complete build
entry point with a new output directory; do not create another build framework.

```powershell
& ./scripts/build-multi-api-candidate.ps1 `
  -UpstreamRoot '<bridge checkout at 28aed409>' `
  -ToolchainRoot '<llvm-mingw-20260826-ucrt-x86_64>' `
  -ReShadeSourceRoot '<ReShade checkout at 4a50d1e>' `
  -DirectXMathRoot '<DirectXMath checkout at 28929ac>' `
  -OutputRoot '<new exact-main build directory>'
```

That existing entry point includes the production staging, Windows compilation,
shader/WARP, RR diagnostics, source-identity and packaging prerequisites. Also
run the unchanged `tests/test_null_guard_reasons.py`, the #160 Python tests and
`scripts/validate-reshade-carrier-exclusion.py` as documented in
[the exclusion record](RESHADE_CARRIER_EXCLUSION_20260908.md).

Once all checks pass, use `scripts/package-multi-api-candidate.ps1` with the new
BuildRoot and an unused OutputRoot. Verify `validation.json` and source manifest
name the actual build commit, then check all binary and inner ZIP hashes. CI
billing failures remain "not started". Compile, WARP, NVIDIA and individual
game acceptance remain separate results; no release or main merge is implied
by this preparation.
