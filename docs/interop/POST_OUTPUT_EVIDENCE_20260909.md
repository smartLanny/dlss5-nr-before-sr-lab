# Packed post-output storage evidence

The owner reports that queue.3 appears to restore NR→SR, while SR→NR still
has little visible effect and intermittently flickers. The matching F8 sample
has NR and submitted counts both +53, with no new unknown-queue bypass,
actual mismatch or retirement timeout. This does not establish presentation.

The seven complete fixed-camera F7 groups agree with that report. In a static
mountain rectangle outside both menus, post 50/75/100% differ from native by
about 0.154/0.080/0.181 mean absolute stored 8-bit values. These are sequential
frames, with temporal rendering differences, rather than same-frame images.
The previous model/composition statistics describe shader values before the
external UAV store, so they cannot prove that the game's actual Output changed.

The frozen queue.3 production snapshot/compose fixture was also run on the
owner's NVIDIA adapter (10DE:2C58). All eight R10/FP16, full/half, identity/edited
cases passed 1697 checks, including active output and untouched outside pixels.
It constructs neural textures and does not load NGX or a game. This narrows the
question but cannot replace the game transaction.

## Candidate scope

`0.4.7beta-output.1` adds a sparse read of the actual packed post Output after
the composition store and its UAV→SRV transition. It uses the existing sampled
diagnostic schedule and existing slot/fence ownership. The native Output returns
to UAV state before the game continues. The reference is the private snapshot
taken before NR. The probe never writes either picture texture.

F8 includes a coherent frame/config/feature identity, sample counts, expected
pre-store and observed post-store RGB deltas, invalid count, corroborating hashes
and sample age. Stored-format quantization can change hashes. The mean absolute
per-pixel maximum RGB delta is capped at 1, and is not luminance. Evidence is
limited to the sampled pixels at the NGX transaction boundary, not Present.
FP16 currently uses its exact copy path without the packed snapshot SRV heap;
this probe is unavailable there. More than 65535 samples also disables the probe.

Optional probe creation failure retains the existing rendering codec. No model,
composition formula, default, guide, queue admission or wait policy changes.
The new version is diagnostic, not a claimed repair of the remaining defect.

The production fixture arms the real sampling path and includes a deliberate
unused-target write. That negative control must show a changed expected result
and an unchanged actual Output; normal edited and identity cases must report
the correct stored result. Frozen build and game results are recorded separately.

## Frozen validation and deployment

Source `eea9fcf50c04b1f30de7196828b1ab8e1993cbad` passes all 40 local gates.
The complete production WARP fixture passes 1938 post-output checks, including
the unused-target negative control. The same source and cases pass 1939 checks
on 10DE:2C58 (one extra adapter-selection check), without loading NGX. Both
actual language DLLs load and draw in the private ReShade menu fixture, each
completing 40 Presents. The earlier validation attempt stopped in the CPU
colour-policy extraction seam; its missing optional probe variable was adapted
without changing the tested policy. Its failed record remains preserved.

- Chinese Core SHA-256: `f69dee52e21d12f63bab83505a6893b78a26d461bb6dfbdc2b4da13c93c639cf`
- English Core SHA-256: `5f63dc4c41a8ff5951973a2ce81db924bf6284d854ee61984941b3e352441b59`
- Validation SHA-256: `e6728ffde5448c2a5811a54c1fd06b2907de7e74dfd6707bfc5e4aad5bf31bf3`
- Source manifest SHA-256: `da8c045c77c64e911a6f6300cc562af894889679707392ab6ed7d069364f30d2`

Only the Chinese Core was deployed to the owner's reproduction installation.
The prior queue.3 Core and current configuration were backed up; 16 existing
DLL/configuration files have identical before/after hashes. The restore helper
was checked without executing a rollback. No game/NGX result for output.1 has
been reported yet. The extra sampled read/barrier can perturb timing, so this
diagnostic does not establish unobserved frames' behavior.

## Owner validation

Run SR→NR in the same scene for several seconds, then collect F8 once. A new
F7 seven-group capture is not required. If expected and observed stored deltas
agree, investigate the later consumer/presentation boundary; if they diverge,
investigate the in-game store, resource/view identity and barriers. No outcome
here alone proves the cause of temporal flicker.
