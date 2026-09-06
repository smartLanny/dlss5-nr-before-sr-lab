# HTGame picture controls and mandatory amplified-colour budget

This is a candidate on top of PR #132, not a released binary. Do not use the
old `beta0.4.5-fix1` file name as evidence that these changes were built.

## New attachment and version correction

Owner's outer archive SHA256:
`14451d6d439931959c940cfe892dbb1e395119d47fbf71aac0f408ff344ff07e`
(8,550,076 bytes). It includes a folder named 0.4.5fix, but its actual core
identity is `beta0.4.5@d403388158b189103f76bb0a6a3d1a404782e02b`, clean;
addon `ca834138a8b01cc38d91e80e0e978e53dbbc57213d5bcb3f490bf171e8b9c393`.
This is not NBA2K25's e5ce121/fix1 capture. HTGame, RTX 5090 Laptop,
driver.raw=61062, 3840x2160 D3D12. No raw tester paths/files are committed.

Same mode/config/epoch pre-SR observation: NR 21343 -> 21389 and submitted
21297 -> 21343 (+46 each). Colour request/effective=1/1, explicitly opted-in
unverified HDR. Model intensity=2, local tone/structure=1, style=0,
final strength=.49, face mask=0. This proves parameter delivery and activity,
not perceived chroma or facial-mask quality. Stored custom .75 does not apply
while WorkMode=Quality; the latter is output-relative and is near N=R for the
recorded 2228x1253 -> 3840x2160 contract.

The earlier post interval did fail: native SR succeeded, second command-state
capture refused, prepared-only, no NR call. The log includes fresh known-null
with dynamic=0x8 and zero unknown domains. d403 already admits finite [0,1]
blend factors; simply enabling the same bit again is not a new fix. The four
actual blend values and other detailed rejection subconditions are absent.
This PR does NOT loosen command-state/PSO admission or claim post fixed.

The 0.3.3.5 archive does show successful post NR and nonzero resulting image
changes. Its captured model intensity=2, local tone/structure=2, style=2,
final=.71, requested colour=.78, while sampled effective colour=0. Thus it is
not a parameter-matched old/new colour A/B. Face-mask visual equivalence and
startup flicker remain open; do not replace the observation with a made-up
mask implementation or claim a default model value of 2 (the default is 1).

## Implemented

- Mandatory >1x colour budget finalized before actual codec constants and
  diagnostic publication: `coefficient=min(requested_effective,1)/amplification`.
  In Mode 2 this holds the shader's `a*C*confidence` chroma exponent at the
  unamplified budget while leaving brightness amplification unchanged.
  In Mode 1 it reduces the colour MIX after amplification; this is NOT an exact
  per-pixel hue/gamut guarantee. Existing shader finite/gamut/ratio guards stay.
- No opt-out in ordinary UI. 0..1x behaviour is unchanged, including the owner's
  .49 capture. Consequently this is NOT a claim to fix that capture's existing
  unusual colour. Wrong colour/exposure contracts and model hue remain separate.
- Preserve model tuning and its default. Rename final strength to
  `Strength amplification / 强度放大`, under Advanced picture controls.
  Move model strength, local tone/structure, face/skin and UI protection into
  the primary picture section. Same capture hold and config-generation commit.
- Precision control directly below processing mode; explicit 100% selects
  Reference for exact N=R. Existing presets remain until explicitly edited;
  their slider estimate is labelled historical, not an applied frame tuple.
  Valid custom values map to the current 50..100% pre-SR core. 0 disables NR
  while retaining precision; 1..49% is explicitly REJECTED, not a silent clamp.
  Adjusting nonzero precision does not implicitly turn on Enable/F6.

## Not completed / product requirements retained in #82

The full continuous 0..100% kernel range, new-install default migration and
real reduced-resolution post-SR execution are not implemented by this slice.
The post control stays disabled with an explicit explanation rather than a
fake slider. A correct follow-up needs separately sized full-output snapshot,
matched downsample/model/reconstruction, actual guide phase/MV/exposure and
history/retirement tests. No >R work grid is introduced. Latest owner scope
supersedes the previous independent-output-scale and >R supersampling proposal.

No extra NR, texture, dispatch or per-pixel shader work is added here; the
budget is CPU policy arithmetic. No measured performance or cost-free cascade
claim follows. The main translation unit and its embedded shader remain unchanged; the
composition policy include and UI are changed as described above.

## Verification and local build

Run `scripts/validate-picture-budget.py` with g++ and clang++, then again with
`--negative-control`. Actual production policy and default Dispatch strength
are used. 170,254 host assertions pass; the unbudgeted assignment must fail
234 checks including the named oracle. The scalar exponent sweep is not GPU
execution. Run existing 045-colour, colour-policy, bug-sweep, strength-cap and
recommended-picture tests too. UI checks follow the actual primary controls
inside the capture hold and generation transaction; no simulated UI result is
called a game/DPI pass.

The old colour-policy harness still expected the pre-#109 disabled slider.
It now separately tests historical suppressed display and explicit opt-in;
all 40,960 colour contracts remain. The face harness's extraction endpoint
follows the moved production block. Clang exposed misleading indentation in
the old recommended-settings harness; only braces/line layout was fixed.

Hosted workflow retains the full Windows/Shader/native/WARP build and
cross-platform feedback checks. These and real NVIDIA/game tests are pending
until they actually run; no old green check is assigned to this new commit.
Do not publish the inherited version identity as another indistinguishable
fix1. Local agent must freeze a new candidate identity, clean-build both
languages/companion, validate, and provide exact hashes before distribution.

## 0.3 baseline assessment

`codex/beta0.3.8@f4d0657ad2ddcb82b8af3b0edc6c67ecf9109934` is a reasonable
separate maintenance candidate, NOT an established stable release. Its
DestroyContext still clears the handle after ReleaseFeature even if the
returned result failed; #111-class lifetime safety needs porting first.
#17/#42/#53 record 0.3.7-class failures, not a measured 0.3.8 failure rate.
#121 reports .3.3.4 better than .3.3.5 in AC Shadows; this owner's HTGame post
works in .3.3.5. Neither observation ranks all games. Preserve exact old
binaries as title-specific visual references, then test safety-backported
0.3.8 separately; do not wholesale restore old hooks or drop guards.

Simple player retest AFTER a new validated binary exists: enter the same scene,
compare F6 and modes, confirm primary controls/advanced amplification, and
send one F8 TXT; visual anomaly F7 only when convenient. Persistent bypass
must remain visible and be reported. Stop and roll back on crash/black screen.
