# beta0.3.3 create-time tuning and packed-color input candidate

## Revision r4: command-state and diagnostic follow-up

The current development identity is `beta0.3.3-dev-r4`. It adds native command
binding preservation around the plugin's pre-/post-SR work, prompted by RE9's
reproducible crash after its first successful NR evaluation. Game SR retains
its original command-list interface. Unsupported or incompletely observed
state keeps that evaluation on native SR; later verified Reset/bindings can
recover without permanently disabling NR.

F7 capture now uses a fresh, bounded context lifetime that can actually retire
and export while the game continues rendering. Screenshot labels are short
enough for the reproduced Cyberpunk game path. An optional HDR peak-shoulder
mapping preserves distinctions that the old proxy can collapse at bright
fixed-colour values; it remains off by default and is not a verified facial
detail improvement.

See [the r4 validation record](BETA_0_3_3_R4_VALIDATION.md) for exact gate status,
[local game comparisons](LOCAL_GAME_QUALITY_2026_09_04.md), and
[the current-runtime intensity investigation](RENODX_2026_09_04_COMPARISON.md).
The local I1/I2 plateau was already present in raw NR output. This revision
does not claim that model intensity above 1 now creates stronger detail.

## Revision r3: diagnostic and compatibility candidate

The r3 build identity is `beta0.3.3-dev-r3`. It fixes no-op observer epochs,
reverse module aliases, stale dynamic-size contexts, guide history transitions,
failed Reset retry, restricted untyped resource reads and explicit-unity stale-HDR
R8 admission. Instance/config/frame diagnostics and an opt-in bounded four-stage
raw capture make intensity/style comparisons attributable. Default composition
is unchanged. Capture does not read an unverified native-SR Output state.

See [the r3 validation procedure](BETA_0_3_3_R3_VALIDATION.md). CPU/compute
validation is separate from game validation; no r3 Spider-Man or Yanyun visual
pass is claimed. The older sections below document earlier revisions.

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

### Live bounded R8 input and older shader compilers

The supplied Yanyun log reached a valid Feature 1 at 1920x1080 -> 3840x2160,
but beta0.3.2 rejected render Color format 28 before Feature 18 and recorded
zero successful NR evaluations.

The first beta0.3.3-dev Yanyun follow-up exposed two later gates. Its local
`d3dcompiler_47.dll` rejected the embedded `cs_5_1` target, so none of the four
conversion shaders existed and Feature 18 could never run. It also retained an
HDR-labelled Feature 1 handle while the live Color and Output resources changed
from FP16 to exact `R8G8B8A8_UNORM` with unity exposure. The old flag-only check
therefore rejected a coherent current-frame value-domain contract.

Revision r2 compiles the conversion shaders as `cs_5_0`, which uses no feature
that requires shader model 5.1. It admits exact `R8G8B8A8_UNORM` for `NR -> SR`
when the live texture contract and unity exposure pass and either the current
Feature 1 flag declares SDR or the current Output independently corroborates
the same bounded R8 format. Live Evaluate resources outrank a stale Create flag.
Encode copies the bounded values without guessing gamma or paper white;
Combine returns values in the same domain through the controlled FP16
replacement texture. If the same Feature 1 handle changes FP16/R8 storage
without another Create, its old Feature 18 context is retired at the exact GPU
fence and rebuilt with the new codec contract.

Typed `R8G8B8A8_UNORM_SRGB`, non-unity exposure, an unmatched R8 output, and
any resource that fails the live capability contract remain safe native-SR
bypasses. Post-SR R8 write-back also remains fail-closed unless its output
encoding is explicitly verified. Admission is contract-based, not a game-name
whitelist.

### HDR/DLSS rebuilds no longer permanently poison ownership

The supplied Witcher 3 HDR log did not fail an HDR colour contract. During the
display-mode rebuild, the game first attempted an invalid reversed-dimension
Feature 1 Create (`2560x1440 -> 1707x960`), received `InvalidParameter` and a
null handle, then evaluated that failed path before creating the valid
`1707x960 -> 2560x1440` Feature 1. Revision r1 treated that null/unknown Evaluate
as proof that an unobserved RR handle existed and permanently disabled Feature
18 for the process.

Revision r2 treats a null handle as a native invalid-call path only. Failed/null
Feature 1 Create attempts remain diagnostic counts but no longer count as
successful game-owned SR activity when a delayed module is assessed. A genuinely
unknown non-null handle still fails closed for the process because its feature
identity cannot be proved safely. Delayed core/DLSSD Create, Evaluate and Release
detours are prepared first and published in one queued MinHook transaction, so
an Evaluate hook cannot become live before its matching Release observer.
A single sequentially consistent odd/even epoch brackets that publication
window. Each outermost Feature 1 Create samples it at entry and completion, so
only a successful non-null overlapping Create fails closed; a failed/null
attempt does not. Native Create output slots are cleared first so a failed call
cannot inherit a stale caller pointer. An unknown non-null Release is treated
as the same ownership gap, while permanent NGX detours pin their owning module
until process exit instead of retaining an unloadable trampoline.

### Post-SR colour following reports and applies its effective value

In the supplied Witcher 3 fixed-camera set, our post-SR result and the RenoDX
paper-white-4 result had nearly the same global exposure, while the larger
visible difference was chroma. Earlier HDR code displayed/logged the requested
Colour Follow value but silently forced its effective shader value to zero.

For the narrow post-SR FP16, unity-exposure, no-ExposureTexture contract,
revision r2 now allows the requested Colour Follow control. It transfers bounded
per-channel neural ratios, then renormalizes them to the luminance-only result;
this follows NR chroma without replacing the original HDR lighting. Diagnostics
now record `colorFollowEffective`, so a log cannot claim a value the shader did
not receive. Unknown HDR and `NR -> SR` gamut contracts remain luminance-only.

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
- Module-rescan summaries separately report Feature 1 create attempts,
  successful non-null creates, in-flight creates, the observer epoch and
  null-handle Evaluates. A transient failed
  HDR/DLSS rebuild can therefore be distinguished from a real unobserved
  non-null owner without a local reproduction.
- Delayed `_nvngx`/DLSSD lifecycle observers log their queued hook transaction,
  and concurrent Feature 1 calls have a dedicated one-frame native-bypass
  reason instead of being labelled as Ray Reconstruction.
- R8/R10 decisions log format, active subrect, Feature 1 flags, resource
  capabilities (`support1`/`support2`), format-query result and the exact
  accepted/bypass reason. Existing pipeline totals then distinguish create,
  evaluate, visible transfer and native fallback.

## Build provenance

Release builds use optimization, LTO, dead-section removal and symbol stripping.
The DLL resources and exported provenance strings retain the author and only
official release location. Streamlined distribution archives contain only the
four required runtime files and one README. ZIP hashes are printed by the
internal packaging process for optional publication, but standalone signature
bundles, verifier scripts and duplicate documents are not placed in the archive.
These measures preserve clear attribution without complicating installation;
they are not a claim that native code cannot be reverse engineered.

## Still not resolved by this candidate

- A first successful beta0.3.3 r2 Yanyun run is still required. The shader-model
  failure and stale-flag R8 rejection are fixed; a later runtime, guide or queue
  contract may only become visible after those gates are removed.
- Witcher 3 must repeat the HDR off/on rebuild on r2. The supplied r1 log proves
  the false permanent latch, while source/build validation proves only the fix,
  not the live transition.
- Ray Reconstruction remains a strict safe bypass, not a joint RR/NR chain.
- Unknown HDR gamut/transfer and unsupported ExposureTexture layouts remain
  fail-closed. Scene Paper-White Scale is an expert ablation, not a universal
  HDR correction.
- The full-resolution `SR -> NR` path still has the documented generic D3D12
  state-restoration boundary and must be validated per live resource contract.
- Delayed RR-capable module observation and the global chain lock around opaque
  NGX calls remain architecture review items from the current GPT Pro review.
