# #39: proxy-luma-guided lighting weights (2026-09-07)

Base: `main@7d17c10d8abf0b6342b0bcf4ff0e4a1e164647d3`.
Branch: `fix/045-lighting-edge-weights`. Independent of the P0/P1 null-guard
reason patch; neither branch requires the other to be reviewed or reverted.
The later online `main@db0577c565d0a81a6f5c010b35698e078adf1205`
adds feedback evidence only; the merge base and production source base remain
the commit above.

## Evidence boundary

#39 comment 5569032830 identifies four `beta0.4.5-multiapi-dev3` captures:
post mode at 50% work scale / lighting about 0.15, and pre mode at lighting
0.2 / 0.6 / 1.0. The recorded scope has thousands of NR successes, while the
reported visual black outline persists. This is not an NR-skip diagnosis.

The reported 20260907 dev3 raw directory is absent from the checked main tree;
ROG was unreachable in this session. The published summary was read, not those
raw TXT files or a new image pair. The checked-in fix1 HDR/color-follow positive
control is a different test and is NOT acceptance of the outline fix. Later
subjective reports of a fainter outline also do not establish a clean result.

## Reproducible mechanism / root-cause hypothesis

The existing clamp prevents reversing or amplifying a pixel's stabilized
residual. It does not prevent spatially uneven removal of that residual:
fixed cross-tap weights may mix a different surface into the lighting estimate.

Analytic example, deliberately simple rather than a claim about captured skin:
foreground neural/proxy ratio = 0.5. With uniform neighbors, LightingLock=1
removes the -1 stop edit. At a foreground boundary, one neutral-background tap
(ratio = 1) changes the fixed weighted estimate to
`0.40*0.5 + 3*0.15*0.5 + 0.15*1 = 0.575`.
The protected residual is about -0.202 stops, still inside the old [-1, 0]
attenuation interval. The boundary therefore stays darker than its corrected
interior without any sign reversal. Foreground/background ratio disagreement
is enough; the protection itself need not create a negative residual from zero.

This mechanism is falsifiable with source/NR/proxy/final F7 data from the
original scene. It is not yet established as the only cause of #39.

## Minimal production change

`ModelLightingTapV2` uses the same four sparse positions, existing model-grid
mapping, clamped resource-load helpers and two loads per neighbor. The proxy's
luma difference from the center weights each tap with
`1 - smoothstep(0.10, 0.50, relativeEdge)`.

The normalized weighted ratio replaces only the fixed local lighting estimate.
The known finite center retains weight 0.40, so rejecting every neighbor still
has a nonzero denominator. Nonfinite neighbors contribute zero weight, not a
fabricated unity ratio. Flat same-luma regions retain the old weights and the
single-log estimator. The 0.10 / 0.50 thresholds are provisional engineering
choices, not measured game-calibrated constants.

The existing no-reversal/no-amplification clamp is unchanged. LightingLock=0
still skips all neighbor work. No NR strength/default, color-follow permission,
HDR encoding/exposure, detail-stabilization, motion/jitter, resource descriptor,
command-state, queue or lifetime change is made. DetailDecision continues to use
its existing ModelLumaRatioV2, not the new lighting-only helper.

## Tests performed on isolated Mac DevSpace

- `python3 -m unittest discover -s tests -p test_lighting_edge_weights.py -v`:
  extracts the actual production helper and lighting block. The unmodified
  estimator fails 2,566 of 7,324 analytic checks. The new estimator passes all
  7,324. Includes bright/dark neighboring surfaces, flat regions, disabled
  protection, several model-grid ratios, nonfinite neighbors, continuous weight
  sweep, center-only fallback and stabilized-residual bounds.
- `python3 scripts/validate-bug-sweep.py --compiler clang++`: all six tests pass.
  The unchanged attenuation envelope passes 30,012 checks with arbitrary
  nonnegative neighbor weights. Other host oracles pass 28 face-control, 50
  native-hook-publication and 80 settings checks; these do not close other game
  Issues. Existing UI layout stubs were updated for SettingsField/SettingsNote.
- `python3 scripts/validate-color-policy.py --compiler clang++`: 40,960 contracts,
  164,843 checks, zero failures. Repaired a missing SettingsField layout stub and
  disabled unsupported static system linking for this Mac host harness only;
  the Linux/Windows static gate is preserved. No production color-policy edit.
- `python3 scripts/test-beta04-math.py`: pass.
- `python3 -m unittest discover -s tests/multi_api -p 'test_post*.py' -v`:
  23 pass, one explicit skip (real complete-core generator fixture required).
- `git diff --check`: pass.

Host compilation of extracted arithmetic is not a texture/driver/image replay.

## Windows compile and WARP validation

The initial Windows build correctly refused the candidate because the integration
generator and its format-transform identity test still pinned the pre-change core
blob. Commits `438bbfb` and `58853c2` repin both checks to the reviewed production
blob `4f0b836ce57246cf68204b1df8d63f69df6422e3`; the gates continue to reject any
other source instead of bypassing identity validation.

The complete isolated Windows gate passed at source commit
`58853c24270dbaa09c72afb886c44b91d109c40a` with LLVM-MinGW clang 23.1.0:

- all six staged production HLSL compute entries compiled;
- 48 production EncodeCS/CombineCS WARP cases passed across FP16/FP32,
  SDR/HDR, 50/67/75/100% work sizes, nonzero destination bases, identity and
  deterministic nonzero neural planes;
- typed-format and private-baseline WARP, 24 command-state WARP cases and the
  wrong-blend pixel negative control passed;
- the bilingual core, `nrchain`, native carrier and Windows resources compiled
  and linked; LLVM static analysis reported zero diagnostics.

Full-build `validation.json` SHA256 is
`8acb6a883e464e85c71082737bbe95ec5aba38b0f429de8caa2c07e61e799d3e`;
the retained local build log SHA256 is
`4a23650a16fa14746e0b33321b7eebbf5e0ef92bedf16def178a124805b1a8ab`.
Generated binaries and raw logs remain local.

Review found that the pre-existing 48 cases left `LightingLock=0` and compiled
the legacy `CombineCS`, so they did not execute this candidate's new branch.
Commit `2ae0daf56fccbe2ffd5fdb6ceb338d71751d46b9` adds a focused WARP oracle that
compiles and dispatches the actual production `CombineV2CS` with FP16 textures.
It verifies a fully protected flat field, rejection of a strong cross-surface
luma tap, and the documented equal-luma color-boundary limit. It also verifies
that the lighting-only path does not inject chroma into a gray source. The
focused result JSON SHA256 is
`6f0d3dc00643c0f35b958edfdf0715d63fc00414a2d09e69f01781270142d260`;
stdout SHA256 is
`87e9613d92f603be9060b2fb472990f3ab964463ab23408f67ccbd983e441e31`.

These WARP neural planes are deterministic test inputs, not NVIDIA Feature18.
No package was published, GPU cost was not measured, and no Wuthering Waves,
HDR display, motion or long-run game acceptance was performed. Hosted CI remains
unstarted because of the account billing/spending-limit annotation and is not a
passing check.

## Risks / ROG retest

Luma guidance is not a face, depth or object mask. Similar-luma surfaces can
still mix; strong legitimate within-surface gradients may lose neighbor support.
The center-only fallback protects more of the original lighting and can reduce
NR's intended local shading. Smooth weights reduce hard thresholds but do not
prove temporal stability. Additional scalar arithmetic/divisions have unmeasured
GPU cost even though the four paired neighbor loads are unchanged.

First inspect the already archived dev3 F8 / image evidence, without requesting
old material again. Build on ROG and compile the actual codec shaders; run the
production-shader WARP and output/base/format identity gates before game testing.
In the original Wuthering scene compare LightingLock 0 / 0.15 / 0.2 / 0.6 / 1,
HDR on/off, pre NR and post 50/67/75/100% as separately labeled cases. Keep NR
strength, color follow and other settings fixed. Use F6 plus matching F7 stages
and F8 to inspect contours, same-luma edges, motion, highlights, UI and frame cost.

Keep #39 open / needs-retest. A fixed analytic mechanism, color-control positive
or successful NR counter is not proof that the visual outline is gone. Any
bypass or new regression must remain visible and direct the user to feedback;
restore recommended settings or roll back rather than weakening safety guards.
