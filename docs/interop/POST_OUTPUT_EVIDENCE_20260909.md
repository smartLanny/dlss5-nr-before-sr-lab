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

## Owner validation

Run SR→NR in the same scene for several seconds, then collect F8 once. A new
F7 seven-group capture is not required. If expected and observed stored deltas
agree, investigate the later consumer/presentation boundary; if they diverge,
investigate the in-game store, resource/view identity and barriers. No outcome
here alone proves the cause of temporal flicker.
