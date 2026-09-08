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

Candidate identity is beta0.5-dline3 / PE0.5.1.3. Validation and delivery are
pending at this source preparation step; exact frozen results will be appended.
