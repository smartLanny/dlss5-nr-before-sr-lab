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

The final production source is `2b573c2beb2aa6ea1bacbc2ba4f13cdff49df2dc`,
beta0.5-dline5 / PE0.5.1.5. The first source `9a5f92d` completed its 45 gates,
then actual English-menu inspection found the long bright-control label left
too little slider width. The follow-up only shortened that English label and
repinned it. The final source again passed all 45 exact-source checks.

The final WARP runs passed 214 pre-light and 130 post-light groups, each with
44 new whole-direction/D4-reference groups. Existing RR-depth, native-command
state/lifetime and combined low-grid gates remain passing. The source scalar
gate passed 128419 assertions and six compiled numeric negative controls;
the old four-region and post scalar gates remained passing. Config/undo and
capture tests verify scope persistence, capture freezing, missing-key fallback,
and unchanged NR history/tuning identity.

Both language DLLs were exercised with mode 0 and mode 1 in real ReShade menu
hosts. Each final run completed 40 frames and six overlay events, retained its
fixture INI, and reverified all 113 generated source hashes. The final screenshot
from each of the four runs was visually inspected for labels, selected mode,
usable slider width and menu ordering. Twelve images were produced, but only
those four final images were visually inspected; no preset/mode clicks were
injected. One earlier final-binary run completed 40 frames but received an extra
early overlay-open before the scripted requests. Its strict seven-event rejection
is preserved. One same-binary repeat passed the unchanged six-event assertions;
the extra event's cause was not established or classified as a game crash.

FXC emits X4000 at the selector call. A separate initialized-input fixture and
both complete combine entry points reproduced it; inspection found no missing
return or uninitialized input path. An equivalent explicit-else variant removes
the diagnostic, but its DXBC differs from the frozen variant. Source and
disassembly evidence is preserved; the candidate was not changed merely to
silence this compiler flow diagnostic. Numeric/NaN and WARP evidence remain
separate from that analysis and do not establish safety for every possible input.

Final source-manifest SHA256:
`f7843f2cd1c88bd114b15efc69f0385bce5c8efe8ae1b0b94bc0d0ffb2a424ca`.
Validation-log SHA256:
`c02a803c975e6fb9d415f7b6e10fee153e0439546b926af1091c1a8d01944ec4`.

| Artifact | Bytes | SHA256 |
| --- | ---: | --- |
| Chinese Core | 1875968 | `ee931632246d1b593dd8c94fe9bc285988fce3e93e38f3ed0dffb0c247120584` |
| English Core | 1845248 | `f7b78334765d059e1fdb9fc574f4c2d5eb7caf0a13ebe66b584d6990e5236cb6` |
| nrchain | 8192 | `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb` |

The Chinese final Core replaced D4 in the closed NTE installation. Independent
checks confirmed one live Core and byte-identical personal INI, ReShade and
bridge. D4/settings were backed up, and the guarded Core-only rollback passed
its read-only preflight. No game or RR setting was changed and no game launched.
The preserved INI has no new selector key, so the owner explicitly chooses
Whole light/dark for the test. D5 game/face/motion quality and GPU cost remain
unverified; no main/stable merge, release or capability promotion is implied.
