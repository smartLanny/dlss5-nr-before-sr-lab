# D5: whole light/dark controls on the D4 compatibility baseline

The owner asked to continue from the offline Magpie/engine study to an
in-game-testable candidate. This branch continues `fa450c9` and retains the
D4 / 0.4.7 ancestry. D4 source `ec338d7d` has an owner-reported NTE RR pass;
that does not establish a D5 game, face-quality, HDR-quality, or performance pass.

## Runtime scope and controls

`LightControlMode=0` retains D4 detail-layer weights. Mode 1 selects whole
bright/dark weighting on the existing pre-SR and executed post-SR composition
paths. A missing or invalid key selects mode 0. The configuration template and
explicit full-picture recommended reset select mode 1. Group presets and group
neutral reset preserve the selected mode; undo restores the previous mode and
coefficients. The selector is included in configuration identity, persistence,
the bounded capture hold and group snapshots, but not NR history/tuning identity.

Existing flag 262144 still gates light processing; new flag 524288 selects
whole-direction weighting only when the light group is active and supported.
The selector alone does not activate a neutral group. It adds no root constant,
texture, guide/history resource, NR invocation, or change to native command/RR
admission. Requested mode is exported in the single F8 TXT. Dispatch-recorded
mode is separately exported in stage/frame metadata, with unavailable marked
when the light group was not active/supported. A request is not GPU completion.

For original same-sign broad fraction f, directional gain d, broad modifier b,
and public strength m, mode 1 uses `clamp(m*d*(f*b + 1-f), 0, 4)`. For positive
edits b is broad*(1-glow), otherwise it is dark. D4 mode 0 keeps
`clamp(m*(f*b + (1-f)*d), 0, 4)` exactly. The original partition is retained
after protection, and only the remaining edit is amplified. Neutral controls,
zero/invalid input guards, sign restrictions, the final ratio ceiling, near-black,
edge, LightingLock, HDR energy, colour policy and alpha preservation remain.

This is whole signed-luminance control, not Magpie RGB/HSL reproduction or
reflection/face/material identification. Glow100 still removes broad bright
edits, including broad reflections classified there. The new bilingual labels
and help describe that interaction; the old mode retains its previous labels.
Faces remain an unresolved owner observation. Depth and motion experiments are
not integrated in this candidate.

## Exact references and validation

The scalar/source gate removes only the added helper/declaration and two exact
selector calls, and requires the entire shader to recover frozen D4. The old
V1 helper is unchanged. Its independent numeric oracles test mode selection,
whole-region response, protection, direction isolation and limits, plus compiled
incorrect variants that must fail numeric assertions.

The GPU fixture retains the historical generated D1 reference. It additionally
requires the exact generated D4 source and manifest, so old non-neutral mode and
new neutral mode can be checked against the actual D4 shader. The D4 reference
source SHA256 is `c681b208ef320b1b9fbd497e3c46485bf3836296a084cd4a741a2b179f1c8208`,
manifest `212d863167d4b55b5cc613528a2df68600243c73d07195c9136a6fe8b43c18c6`.
The packager requires both reference hashes and the new successful gate flags.
Unknown post transformations remain rejected by the prior exact adapter proof.

Production source identity, full bilingual build, CPU/WARP results, actual menu
inspection, package hashes and deployment are pending and will be recorded after
they execute. No unrun check is counted as passed.
