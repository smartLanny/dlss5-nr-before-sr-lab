# D3 owner feedback follow-up

D2 production source was `e071c778d10cb4f079857f128aad3093defb7b9e`.
The owner tested Neverness To Everness and reported visible glow suppression,
weak or absent dark-structure/reflection effects, darker faces especially in
post-SR, and face adjustment visible after restoring defaults but later weak
after changing model style or other picture controls. ABC integration remains
deferred. This follow-up does not replace those observations with synthetic
test results.

The supplied F8 SHA256 is
`e7ef1e1b55aec4ebd73f631de67903909c0e100508e6a571188b97eda7a8cdba`.
The matching complete current-process log and INI were privately preserved.
No raw feedback, image or local installation path is added to source.

At capture, config generation 245 matched the latest successful NR identity,
instance 113. Post-SR evaluation and transfer continued during collection.
The last face off/on sequence retired the old feature after its GPU fence,
created new instances successfully, and sent skin strength -1/0 respectively.
This rules out a missing toggle transaction in that observed sequence. It does
not prove the model detected a face, or that every preceding edit behaved the
same way.

The final sampled whole-image means were proxy -0.07%, NR -6.64%, and composed
output -6.55% relative to original luminance. The source/proxy was nearly
unchanged while the model supplied a negative luminance residual; this is
whole-image evidence, not a face ROI measurement or a measured pre/post A/B.
The later defaults/toggle sequence also changed mode, model strength, local
tone, local structure and earlier proxy mapping. Those changes cannot be
collapsed into a proven mask reset bug without matched image evidence.

One concrete reporting defect was found: the current single-TXT F8 path omitted
all D light requests and the preview mode, although another capture path
already included them. D3 adds requested snapshot fields and fuller historical
successful NR tuning fields. D coefficients are not labelled applied NR
tuning: actual compositor constants remain a separate per-dispatch contract.
Missing snapshots are explicit rather than populated with plausible defaults.

The original model style selector is moved near the top. The complete light
group, including advanced light controls, moves below model details and above
advanced picture controls. Controls retain their values and transaction logic.

No compositor, model parameter semantics, skin mask or exposure algorithm is
changed. A fixed-view effective/ineffective pair after one model/picture edit
is the next useful image discriminator. Shader checks remain synthetic; game
output, face stability and performance must be reported separately.

Candidate identity is beta0.5-dline3 / PE0.5.1.3. Frozen production source is
`9e171fb40f7937cec48f2e7553db85e62ba94a3b`. Its 28 required local acceptance
checks passed, including canonical staging, all production SM5 entries,
bilingual Core/bridge linking, actual Windows feedback collection and WARP.
Source manifest SHA256:
`432a7a32896fbb884dbdfe559c85559a1423278e2d30c8fe65df3f494a05b812`.

The post WARP fixture now includes 24 added groups isolating reflection and
dark-structure 0-to-2 responses, wide-region preservation and non-target sign
preservation at LightingLock 0/.2/1. All 86 post groups passed. All 170 existing
front light groups and the existing combined low-grid fixture also passed.
No real NR model or game scene is exercised by these tests; they show that
the two controls can affect their synthetic target regions, not that their
visible strength is satisfactory in the owner's scene.

The actual Windows core-feedback fixture passed 301 assertions and independent
archive parsing. It verifies frozen D requests, differing requested/applied
generations and modes, no invented parameters for busy/missing snapshots, and
unchanged picture configuration. Separate existing stability (33), parser (32)
and report (15) checks also passed on these source changes.

Actual English and Chinese DLL menu hosts each completed 40 frames and six
overlay events, with three captures and no INI mutation. The final capture
from each language was visually inspected: original style is near the top,
model details precede the complete light group, advanced picture follows it,
and labels/notes remain readable. These are pre-SR fixture snapshots without
injected preset clicks, not a game interaction result.

| Artifact | Bytes | SHA256 |
| --- | ---: | --- |
| Chinese Core | 1498112 | `3ca4bd6f20ab51809e722cdd14b16604a5263a84ec3331ba3c43fb26e869fd0d` |
| English Core | 1468416 | `7cc157dbe664fb1d2ce59b5830a5e9dad34f9ea7aa306f7766008260300ce617` |
| nrchain | 8192 | `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb` |

The Chinese D3 Core was installed into the owner's closed NTE installation.
Only the Core was replaced: the latest personal INI, ReShade and owned bridge
were verified byte-for-byte unchanged. D2 and settings were backed up; the D2
rollback preflight passed without performing a rollback. No game was launched,
no RR setting was edited, and no ABC/main/release integration was performed.
Private install/menu/validation receipts are retained. NVIDIA, D3 game output,
face behaviour and performance remain unverified; hosted CI is not counted
as passed and known billing-blocked jobs are not rerun.
