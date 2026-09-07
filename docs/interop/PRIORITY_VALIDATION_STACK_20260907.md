# 2026-09-07 priority validation stack

## Purpose and boundary

This temporary stack integrates already reviewed priority fixes so the owner does
not have to recreate their interactions by hand. It is based on
`origin/main@db0577c565d0a81a6f5c010b35698e078adf1205` and does not merge main,
publish a release, install a game, or start a second long-lived development line.

The canonical build and package source is
`a2a338012a97f98f9327c141f32e274989602eb1`. Later PR documentation and this
report are documentation-only descendants. The package's manifest correctly
retains the build source; a later branch HEAD must not be presented as compiled
code.

## Included ancestry and order

The integration used ordinary non-fast-forward merges and preserved every PR
ancestor. The effective order was:

1. PR #154 `ac86a1d6a543c830ab55d44bc5fb442a894d43a0` — finite blend/null-guard
   Windows record (`44a3e10` integration merge).
2. PR #151 `bd9dacb0bdde3698a4c41e6e88fc49802de1d364` — LightingLock edge weights
   (`8c2ecbd` integration merge).
3. PR #156 `7ccbc8fb576ee4389119b9bc561e8015d612758f` — runtime-load 126
   classification (`c6a10e6` integration merge).
4. PR #155 code/docs through
   `a8fea9cfd6c27b69c0692581a0aa335f434fb904` — matching published carrier
   names (`51be8ec`, later documentation merge `0478eeb`).
5. PR #157 `3b813b861e92dcdba386233315a5daf67158018b` — configuration source-off
   carrier lifecycle (`55f1954` integration merge).
6. PR #149 `2ad2b46792f709db3254051f00f04b2297aad81a` — null-guard reason detail,
   including the #154 ancestor (`58f55e3` integration merge).
7. PR #150 code through `b2566e6c09ba096ac7b92c25828bb0249774cc24` — RR boundary counters
   (`544dca4` integration merge); its final Windows documentation
   `d5ebc930ad5945819ee5b7f9ddda882bbcaff6b0` was merged after packaging as
   `294c899`.

## Integration fixes

PR #156 conflicted with PR #151 only in two deliberate source-identity checks:
`scripts/prepare-multi-api-beta.py` and
`tests/multi_api/test_format_patch.py`. The combined production
`src/nr_before_sr.cpp` merged automatically and has Git blob
`52a2782b29e8a9f43cec2c03cf69ec01143cd6d8`; both checks were repinned to that
exact blob. Runtime-load feedback/settings pins from #156 and the LightingLock
shader change from #151 are both retained.

PR #149 produced the final `src/nr_command_state.inl` blob
`f0f13b0a8ac243f01cdf0f93a517440ac65f1394`. `STATE_CURRENT` uses the same value;
the older `47598e4` #154-only pin was not restored.

The first complete build attempt stopped before compilation because PR #155 had
updated the generator's carrier identity anchor to three published names but had
left `tests/multi_api/test_integration.py::NAMES` at the old one-name fixture.
Commit `a2a338012a97f98f9327c141f32e274989602eb1` makes the fixture match the
production input. It does not loosen the finalizer: a missing or changed identity
anchor is still rejected. The failed log is retained with SHA256
`e0a473252d97c9e540eb3ae6b6b22145c1418a8c332280d8ad718a2bba362e04`.

## Focused combination validation

An exact staged source was generated from the combined tree before running the
focused Windows checks. The focused log SHA256 is
`d69000f8bc3d139c1571a3b94c43c6ff535d91a9ed041abeda1a0bff42571567`.

- Production null-guard reason extraction: 693 checks passed. Compatibility
  forwarding/null-raster checks: 38/172 passed.
- Native command-state: 66 checks, default-null 718 checks, 64 KiB worker and
  198-function stack gate passed.
- Command-state WARP: 24 scenarios and three paired finite out-of-unit native /
  restored blend pixel cases passed. The deliberately wrong blend negative
  control was rejected.
- Runtime-load Windows DLL fixture distinguished loaded, absent target and
  present target with missing dependency while both failure forms returned
  Win32 126.
- RR production seam ran both O0 and O2 harnesses; each passed 50 control-flow,
  counter and F8 cases.
- Lighting host extraction passed 7,324 checks. Production `CombineV2CS` WARP
  passed flat-field protection, strong-luma edge rejection and the documented
  equal-luma color-boundary limit without injecting chroma into a gray source.

The focused source and successful full build have identical production core
blob `52a2782b29e8a9f43cec2c03cf69ec01143cd6d8`. The later `a2a3380` change affects
only a staging test fixture, so the focused GPU/source cases were not repeated
solely to change the recorded Git commit.

## Canonical Windows build

The successful command was the repository's documented
`scripts/build-multi-api-candidate.ps1` with pinned Bridge
`28aed4099b0fe1c207b20b5fee5364c0773c25c2`, ReShade
`4a50d1eddace85734871d91792ff214f13f66c01`, DirectXMath
`28929ac6f3c1649a7641741b689ebecccb17aa8d` and LLVM-MinGW clang 23.1.0.

It passed 68 multi-api tests, 16 native-bridge tests, 36 recorded canonical checks,
runtime-load DLL fixtures, six actual production HLSL entries, typed-format and
baseline WARP, 48 EncodeCS/CombineCS scaled-work WARP scenarios plus focused
LightingLock `CombineV2CS`, RR retirement and negative control, command-state
native/WARP/negative control, static analysis, bilingual core/nrchain and carrier
compilation/linking.

Evidence hashes:

| Evidence | SHA256 |
| --- | --- |
| successful full build log | `02c284acdcd843b378f46dbc6f59981a824561ec172eb1f4e6785a3c2f07ca63` |
| `validation.json` | `aad9ee7f8d632ad17365314d91fd3c569400890dd8b75616b97bee096c480df9` |
| `source-manifest.json` | `68ebc1545e76076fb00abef798cab401ba3a1f35fcd48e6f79c7e62601c9728c` |

The manifest and validation both report
`source_commit=a2a338012a97f98f9327c141f32e274989602eb1`,
`source_verified=true`, `compile_verified=true`, `warp_verified=true`,
`nvidia_feature18_verified=false`, and `game_runtime_verified=false`.

| Built binary | SHA256 |
| --- | --- |
| English core addon | `81593e662ee36f9b150d5f12a8536075ed00e1778e53eecdecf9f6eabc976e5d` |
| Chinese core addon | `81bb559e83a9ac4c4d43c6ba70b1d678e2c3984cffd97ae909a373c6c54481fc` |
| `nrchain_nvngx.dll` | `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb` |
| canonical carrier | `c4011b30577a10d033c0da039407a9a1c47eab78bd12e609a8e0e16e39dcb952` |

## Local package and installation boundary

`scripts/package-multi-api-candidate.ps1` passed without touching a game or an
existing INI. The local Chinese ZIP is
`DLSS5-AI渲染超分版-0.4.5-DX11-兼容增强-@野生的装机宅-Bilibili-OTA.zip`,
671,468 bytes, SHA256
`2868ec8b2cda14684ec26a3f604267909664a3012d5b91aa38bb89ab5a7cda5c`.
The package log SHA256 is
`03f2ac6b35f574c854a184d3d3f76480c7c351a674a53c57c75b9ae02262796e`.

This is a local integration handoff, not a publishable general OTA. The canonical
archive still includes the carrier implicated by unresolved #153 game crashes.
Do not install it automatically. D3D12 games do not need the carrier and must omit
it under the existing manual-install rules. The package uses the same filenames
and PE display version as the prior candidate, so only the manifest/source and
SHA256 values identify this build. The canonical inner manifest was not edited,
and no new installer was created.

## Remaining evidence boundary

No NVIDIA Feature18, Wuthering Waves/HDR, #99/#15 RR game path, #64/#38 post path,
DX11 game, motion, performance or long-run acceptance was run. Source/compile/WARP
results do not close those Issues. PR #157 proves the bounded source-off behavior;
automatic API activation, Manager ownership and the #153 crash architecture await
the owner's decision.

Hosted CI jobs for these PRs did not start because of the repository account's
billing/spending-limit condition (`steps=[]`). Local success is not a hosted-CI
green result, and no blind rerun was made. Repository rules could not be read
through the plan-limited API (403), so this report does not claim a specific main
branch-protection exemption.

Additional lower-priority evidence remains separately bucketed: #148's form says
plain 0.4.5 but its attachment identifies exp1/feed-only; #139/#140 show native
Feature1 activity but exp1 null-PSO or Feeder+DFC-specific evidence. They are not
treated as regressions or acceptance of this stack. #121 was not sent through an
unproductive old-version bisect.
