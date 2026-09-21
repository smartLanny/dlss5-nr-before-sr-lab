# unified6: first residual accumulation candidate

Existing picture settings and five flat pages remain. New stabilization defaults OFF.
New configurations use Conservative color; the separate Color Priority amount defaults to 0.7.
Saved settings remain intact. Independent face detection and both energy modes default OFF.

Experiment > Residual stabilization offers Off / Native motion / AMDOF and
Whole chain / Later layers. Whole chain stabilizes the final edit relative to the
unedited image. Later layers composes NR1 through the same final controls, then
stabilizes only the difference from that anchor. It does not rerun NR1, but adds
composition work and an FP16 anchor. One-layer/missing-anchor cases retain the
current candidate without silently changing scope.

The first supported route is native DX12 SR followed by NR, proven SDR color,
same device and proven queue. Pre-SR, RR, HDR and unproven Present sources do not
run this stabilizer yet. Native motion requires a prepared, verified non-jittered
guide; AMD uses the unenhanced image and does not replace model guides.

The 0.65 reliability prior scales the 60ms half-life parameter to 39ms. History
weight uses real source-frame intervals; gaps above 250ms reseed. Warmup, cuts
and failed original-image matches retain the current edit. Developer offers the
old fixed 0.5 history weight for comparison. Adaptive, accumulation+, low-frequency,
HDR, NVOF and private SR/RR are not included in this candidate.

## Short test

Keep model, layers and sizes fixed. Start OFF and confirm actual NR adoption.
On the supported post-SR SDR route compare Native and AMD; with multiple layers,
compare both scopes. Check static details, head turns, camera movement, occlusions
and cuts for flicker, trails and lost detail. Toggle OFF/ON and change layers/size.
Save F8 plus frame times and same-process memory Usage/Budget.

Selection is not execution. Check status and increasing temporal.submitted in F8.
Recording, submission and GPU completion are separate; texture/SDK accounting is
not total process VRAM. Busy slots or exhausted budget retain the current NR result.
OFF starts no new flow/history/dispatch; submitted work retires safely.

#246, #254, 99%/100% color, RGB halos and RR/FG performance require their own
post-change evidence. This candidate does not declare those issues resolved.
Exit the game to replace one Core and its nrchain. Preserve INI and unified5 files
outside the plugin search directory for rollback. The full optional face backend
and AMD SDK license are included; no AMD driver installation is needed.
No automatic game installation or stable release. Build, WARP, controlled NVIDIA
and real-game validation are reported separately.
