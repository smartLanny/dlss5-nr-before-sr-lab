# beta0.3.3 create-time tuning and packed-color input candidate

beta0.3.3 is a focused follow-up to beta0.3.2. It addresses two concrete
community logs without widening the project into a general graphics wrapper.
Both processing orders remain available: render-resolution `NR -> SR` is the
default performance mode and full-output-resolution `SR -> NR` remains the
quality/reference mode.

## Fixed from supplied logs

### NR controls now rebuild the temporal Feature 18

The supplied Red Desert log showed thousands of successful Feature 18
evaluations while Intensity edits changed only the evaluate parameter block.
The runtime snapshots Intensity, Style, Automatic Mask, Skin Structure, Local
Tone, Local Structure and UI Correction when Feature 18 is created. Updating
those values only at Evaluate therefore did not reliably change the model.

After a debounced UI/config commit, beta0.3.3 now compares an explicit tuning
signature. A mismatch retires the old context after its exact queue fence and
creates a new Feature 18 with the committed values. The log prints both the
old/new signature and the exact create-time values. No handle or texture is
released while a recorded or submitted command list can still reference it.

This fixes the deterministic stale-parameter bug. It does not assert that the
model itself produces an equally visible change across every point in its
0–2 range; ColorDiag and labelled screenshots remain the evidence for that.

### Non-HDR R8 input is no longer rejected unconditionally

The supplied Yanyun log reached a valid Feature 1 at 1920x1080 -> 3840x2160,
but beta0.3.2 rejected render Color format 28 before Feature 18 and recorded
zero successful NR evaluations.

beta0.3.3 admits `R8G8B8A8_UNORM` for `NR -> SR` only when the live Feature 1
create flags explicitly mark its Color buffer non-HDR and the exact resource
passes texture, sample, subresource and shader-load capability checks. Encode
copies the game's bounded DLSS input values without guessing a gamma or
paper-white conversion; Combine returns the result in the same value domain
through the controlled FP16 replacement texture.

Typed `R8G8B8A8_UNORM_SRGB`, an R8 input marked HDR, and any resource that
fails the live capability contract remain safe native-SR bypasses. Post-SR R8
write-back also remains fail-closed unless its output encoding is explicitly
verified. Admission is contract-based, not a game-name whitelist.

### Packed 10-bit Color uses the controlled FP16 conversion

Logs from Horizon Forbidden West and Horizon Zero Dawn Remastered showed valid
Feature 1 calls and active subrects, but both `NR -> SR` and `SR -> NR` stopped
solely at format 24 (`R10G10B10A2_UNORM`). Their live Feature 1 contracts mark
Color HDR, provide scalar exposure 1, and expose shader-readable/UAV-capable
resources.

beta0.3.3 accepts this packed format only when Feature 1 marks it HDR, its
scalar exposure contract is unity, no separate ExposureTexture is present, and
the live texture/subresource/sample/typed-format checks pass. It then performs
a symmetric ST.2084 path: PQ is decoded to linear light, converted from the
10,000-nit PQ unit to scRGB, normalized by Scene Paper-White Scale, processed
through the controlled FP16 proxy, and encoded back to PQ. The neural proxy is
forced opaque while the game's original two-bit alpha is restored on output.
Post-SR additionally requires a typed UAV store before write-back. A non-HDR
R10 buffer or a second non-unity exposure transform is ambiguous and remains a
native-SR bypass. No title name is consulted.

## Diagnostics and controls

- The Advanced panel now exposes `Scene Paper-White Scale`. It is the
  reciprocal UI view of the existing HDR proxy scale: values above 1 feed a
  darker normalized scene to NR. It now applies consistently to scene-linear
  FP16/R11 and packed PQ R10 paths. It is not display brightness or NR intensity.
- Parameter-labelled screenshots now include `PW` for Scene Paper-White Scale.
- Configuration, context retirement and Feature 18 creation logs include the
  full create-time tuning signature.
- R8/R10 decisions log format, active subrect, Feature 1 flags, resource
  capabilities (`support1`/`support2`), format-query result and the exact
  accepted/bypass reason. Existing pipeline totals then distinguish create,
  evaluate, visible transfer and native fallback.

## Build provenance

Release builds use optimization, LTO, dead-section removal and symbol stripping.
The DLL resources and exported provenance strings retain the author and only
official release location. Packages contain an ECDSA-signed file manifest and
`AI_AGENT_NOTICE.md`. These measures provide attribution and tamper evidence;
they are not a claim that native code cannot be reverse engineered.

## Still not resolved by this candidate

- A first successful beta0.3.3 Yanyun run is still required. The logged format
  rejection is fixed; a later runtime, guide or queue contract may only become
  visible after that gate is removed.
- Ray Reconstruction remains a strict safe bypass, not a joint RR/NR chain.
- Unknown HDR gamut/transfer and unsupported ExposureTexture layouts remain
  fail-closed. Scene Paper-White Scale is an expert ablation, not a universal
  HDR correction.
- The full-resolution `SR -> NR` path still has the documented generic D3D12
  state-restoration boundary and must be validated per live resource contract.
- Delayed RR-capable module observation and the global chain lock around opaque
  NGX calls remain architecture review items from the current GPT Pro review.
