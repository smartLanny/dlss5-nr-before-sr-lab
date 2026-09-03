# beta0.3.3-dev-r4 validation

This revision continues from r3 commit
`5c50baad8457535fc3dc49784582ab6beecc8e83`. Intermediate builds that still print
the r3 identity must be distinguished by their recorded SHA-256. The tested
8E intermediate is frozen under local `test-output/r3-8e-validation/`.
The final candidate below is identified by Chinese addon hash **2869**;
the shared r4 version banner does not make earlier candidates equivalent.

## Changes under validation

- F7 uses a deduplicated key transition. A capture binds a fresh NR context,
  waits 64 successful evaluations, then retires at the exact queue fence to
  export its four same-frame stages. The settling interval is not a claim of
  temporal convergence.
- Screenshot names use a bounded ASCII suffix; complete requested parameters
  and actual instance identity remain in the log. This avoids the reproduced
  long-path failure in Cyberpunk 2077.
- Experimental HDR peak-shoulder mapping is off by default. The formula and
  input-domain restrictions are documented in
  [the experiment note](EXPERIMENTAL_HDR_PEAK_SHOULDER.md).
- A native command-state envelope passed the bounded RE9 reproduction on
  candidate 2869. See [the investigation](RE9_COMMAND_STATE_INVESTIGATION.md).
  The original SR call must receive its entering bindings after pre-SR plugin
  work. Post-SR plugin work must restore the bindings left by native SR.

## Evidence already established before the state fix

The 8E intermediate passed the existing CPU contract/capture tests, four
`cs_5_0` shader compilations, 32 actual WARP codec scenarios, all three LLVM
analyzer targets and two byte-identical clean builds. Those checks do not
validate later command-state edits.

Cyberpunk 2077 produced five complete I1/I2 and peak-shoulder stage sets, then
four complete colour-follow stage sets with matching independent screenshots.
Both sessions exited normally. The capture and screenshot failures are resolved
in that tested intermediate. The raw NR I1/I2 difference remained near A/A;
colour following worked but produced only modest chroma changes. These are
diagnostic findings, not evidence that the broader weak-detail report is fixed.

RE9's same-session disabled control rendered normally, while enabling either
processing order caused the same driver null read on a subsequent game
`SetComputeRootDescriptorTable(index=2)` call. Both runs had returned success
from the first NR evaluation. The pre-fix candidate therefore fails RE9.

The later **BFF** candidate also failed active-NR acceptance: both modes
repeatedly reported `unknown-indirect-signature`, with zero successful
Feature 18 and submitted-transfer work. Its normal exit while bypassing is
not a passing fix verification. D97 had only a disabled-menu observation.
These results remain separate from the final 2869 run.

Full local methodology and limits are in
[the game record](LOCAL_GAME_QUALITY_2026_09_04.md) and
[the RenoDX comparison](RENODX_2026_09_04_COMPARISON.md).

## Final 2869 automated qualification

The following automated checks passed. Command-state results were run against
the final `nr_command_state.inl` hash **0356F395…**. The ten non-state tests
reuse their recorded results because their exercised main-source, shader,
guide, capture and configuration inputs did not change; they are not described
as freshly rerun on every intermediate addon.

| Gate | Recorded result |
| --- | --- |
| Guide/context history and diagnostic identity | Both pass; includes mapping-change retirement and successful-evaluation identity. |
| Observer lifecycle | Pass: aliases, reversed aliases, publication overlap, failed/null Create, hook failures and forwarded module PIN. |
| Resource contracts | 25 checks, 0 failures. |
| Stage export CPU / lifecycle | 2,027 / 164 checks, 0 failures. |
| Peak-shoulder CPU mapping | 13,312 checks, 0 failures; mathematical validation, not model-quality acceptance. |
| Screenshot formatter | 17,557 checks, 0 failures; Chinese/English output identical, longest suffix 75 ASCII characters, reproduced CP path 184 characters. |
| Production codec shader compilation | Encode, Combine, Copy and Motion Resample compile as `cs_5_0`. |
| D3D12 WARP codec | 32 paired/grid scenarios pass, including capture on/off output identity and raw-stage, alpha, subrect and invalid-input checks. |
| Final native command-state hooks | 46 checks, 0 failures; 64 KiB worker passes. The stack-use gate covers 107 command-state functions, each at most 4,096 bytes. |
| Final command-state GPU restoration | 12 WARP scenarios pass: 4 native controls and 8 restores, with compute, graphics and plugin output checked. |
| Final LLVM analyzer | English, Chinese and bridge targets pass with zero diagnostics. |
| Final reproducible build | Two sequential Clean builds produce matching SHA-256 and sizes for both addons, bridge and INI; all seven `src`/`config` file hashes remain unchanged. |

The native harness covers partial constants, root/heap semantics, indirect
invalidation, late installation, failure rollback, COM ownership and bypasses
for unsupported state. The GPU restoration run used WARP with hardware fallback
disabled; its log reports the D3D12 debug layer unavailable, so its pass is
based on the checked GPU outputs, not a clean debug-layer report. The stage
lifecycle harness has one unused synthetic `g_config` warning. Neither warning
is a failed production gate.

Local evidence is retained under:

- `test-output/r4-state-validation/VALIDATION_SUMMARY.json`: ten non-state
  results, commands, exit codes, complete logs and input manifests.
- `test-output/r4-indirect-validation/`: final native/GPU logs, stack report,
  three analyzer plists, analyzer log and matching source hashes.
- `test-output/r4-indirect-validation/repro/REPRODUCIBILITY.json`: the two final
  build manifests, independent output copies, versions and before/after hashes.
- `test-output/r4-indirect-validation/repro/NON_STATE_EVIDENCE_CORRESPONDENCE.json`:
  the exact unchanged-input relationship supporting non-state result reuse.

The main source hash is
`AB7931C87C99BF5CFABBD028CC2D17109AD9B5DA6748E3A8531C0565E6479DB0`;
the final command-state include hash is
`0356F395E4D6F700586027ED86CEA6BB2605398657D8574D51489D479D0B726F`.
The final analyzer and reproducibility manifests match these inputs. BFF's
older state hash and its own successful clean builds are not substituted for
these final-state results.

### Final output identity

Both addons report file/product version `0.3.3.4 beta dev r4`. The bridge has
no embedded version resource; it is identified by its SHA-256.

| Output | Bytes | SHA-256, identical in both final builds |
| --- | ---: | --- |
| Chinese addon | 652,288 | `2869D7EE12BCB73C4FADC4E2673A7BF3290FD2D90A294B2437E64B72C1BDD9E8` |
| English addon | 652,288 | `2E9DB3A06432F5E8D51C789AE58A583C9F28107621DD87767BC16B279876FF5D` |
| `nrchain_nvngx.dll` | 8,192 | `46041A5FF91AE2FD907E310D132AABC3C4A1ECD48DACE511B8672909D5D9C2FB` |
| `nr_before_sr.ini` | 1,406 | `47E052CC67850C21270884355D96912E4AD4BC796B1F7098105B841967F10452` |

## Final-candidate local game gates

| Game / gate | Current recorded status |
| --- | --- |
| RE9, Chinese 2869 | **Pass for the bounded command-state reproduction.** Existing save reached the street, both NR orders produced successful evaluations and submitted transfer work, return to Mode 2 and NR off worked, F7 exported both modes, and the process exited normally. |
| Cyberpunk 2077, final 2869 | **Pass for the bounded SDR compatibility smoke.** Both orders produced NR and submitted transfer work, Mode 2 returned successfully, NR off worked, both stage captures exported, and Alt+F4 retired the queue and ended the process. ReShade's exit warning is retained in the detailed record. |
| The Witcher 3, final 2869 | **Pass for bounded SDR active execution, switching and capture.** Both orders submitted NR, return to Mode 2 and NR off worked, and both stage sets exported. Normal exit was not completed through the test UI; the test process was terminated after collecting evidence. Display-HDR switching remains untested. |

RE9's exact setup, per-mode counters, exit evidence, raw-stage integrity and
limits are recorded in the
[candidate 2869 acceptance addendum](RE9_COMMAND_STATE_INVESTIGATION.md#local-acceptance-addendum-candidate-2869).
That run used SDR display output and the existing REFramework/MFG Unlock setup.
It establishes active execution for that machine and scene, not general HDR,
facial-detail or temporal-quality acceptance. The captured live exposure
texture value is unavailable, so no cross-domain luminance claim is made.

Spider-Man 2 HDR/SDR faces and motion, Yanyun R8 transitions, and a repeatable
display-HDR toggle remain separate game gates. No local result here substitutes
for them. Helldivers 2 remains deferred pending a distinct SR integration path.
