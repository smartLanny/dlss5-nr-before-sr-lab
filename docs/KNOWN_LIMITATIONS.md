# Known limitations

## Exposure texture contract

The codec accepts a live `ExposureTexture` only when its runtime resource
contract is a shader-loadable 1x1 Texture2D in the DirectSR exposure-scale
format family. Its red channel is
sampled in both encode and combine on the same game command list and is divided
by `DLSS.Pre.Exposure`; the Feature 18 parameter block itself receives neutral
exposure values so the transform is not applied twice. Dimensions, formats, or
view capabilities outside that contract fail closed to native SR.

The sparse diagnostic readback records the sampled red-channel value, but it is
fence-delayed. A change larger than 0.5 EV schedules a history reset for the
next matching Feature 18 evaluation; it cannot reset the same frame in which
the GPU first observes the new value. Scalar exposure/contract changes still
use the immediate reset policy. This path also
does not identify an engine's primaries, gamut, transfer, or private working
space; those remain separate HDR-admission questions.

## Active extents and dynamic resolution

Color, depth, motion, and output resources may have different physical sizes,
active extents, and non-zero bases. The current adapter handles several observed
contracts and validates physical bounds, but some fallbacks infer an active size
from physical allocation size. That can be semantically wrong for padded or
dynamic resources even when it remains in bounds.

NR contexts are keyed by exact `(game feature generation, mode, width, height)` and the
current pool is finite. A title that exposes many distinct dynamic-resolution
sizes during one feature lifetime may exhaust the pool and is failed closed to
native SR. Current retirement tracks marked command lists through the real
queue submission. The native submit hook pins the matching slot before the real
`ExecuteCommandLists` call, then signals a private fence on that same submitting
queue before allowing descriptor rebinding or object destruction. Feature 18's
context is tracked with the slots: several recorded command lists may overlap
when each list's last observed native queue matches the context queue. The
actual queue is checked again at submission. A new/unknown list first remains
native to establish affinity; an unresolved queue change is not bridged with a
private GPU Wait and is failed closed. A failed signal, an unprovable hook state,
or an ambiguous
submission is conservatively quarantined until process exit. This still needs current-build
stress testing with repeated command-list submits, resets, feature recreation,
resolution changes, and multiple queues.

The pre-recording queue signal is historical affinity, not a formal promise from
D3D12. Submitting an already-recorded list concurrently or moving it to another
queue remains an architectural boundary: the actual queue is known only at
`ExecuteCommandLists`, and fail-closed detection cannot remove Feature 18
commands already in that list. The candidate quarantines future work and reports
the mismatch. A universal solution requires independently recorded plugin
command lists or a proven queue/ownership contract.

Accordingly, this candidate requires a title to reset/re-record the temporal
DLSS command list and to keep that list on one stable D3D12 direct queue. The
first observations remain native while affinity is established. Replaying a
closed list containing temporal Feature 18 work without a successful Reset, or
submitting it on a different queue, is unsupported and latches lifecycle
isolation. This precondition is why the build remains a candidate even after
source/static qualification.

## Motion vectors and depth

Low- versus high-resolution motion is inferred from create flags and observed
resource extents. High-resolution motion conversion uses destination-centred
nearest-neighbour spatial sampling and preserves the game's `MV.Scale`; it no
longer multiplies vector values merely because the resource grids differ. A
missing, zero, or non-finite `MV.Scale` falls back to 1.0. Cross-engine
confirmation is still required because resource dimensions alone do not prove
whether values are pixels, UV units, or already normalized. Depth and motion may
legitimately use independent active extents.

## HDR proxy and visual equivalence

The HDR proxy applies exposure normalization, a highlight shoulder, proportional
peak scaling, and an sRGB model-input encoding. Composition grafts the neural
delta back onto the original HDR image and has an explicit identity path when the
neural result equals the proxy. Proxy-domain changes are transferred as a
relative residual around proxy luminance rather than through either a fixed
gain cap or a full inverse shoulder. This keeps the result responsive when
PreExposure makes the canonical source much larger than the proxy, without
introducing the inverse transform's near-white singularity. It remains an
approximation when the model changes strongly saturated clipped channels, and
the final luminance ratio is still bounded by `MaxLumaRatio`.

Shader-level identity does not by itself prove end-to-end identity. In particular,
`R11G11B10_FLOAT` SceneColor is expanded to FP16 before substitution, and the
game's SR may respond differently to a substituted resource even when sampled
values are equivalent.

Mode 2 cannot be expected to reproduce every Mode 1 face or texture detail. Its
neural pass operates on fewer pixels and the result is reconstructed by SR. The
experimental `PreserveLighting` option is disabled by default: its sparse local
luminance estimate can reduce broad drift in one scene, but may also change
faces, edges, and local contrast. It cannot restore source detail that the
lower-resolution model never observed.

ColorDiag is a sparse, bounded display-linear diagnostic, not a colorimeter or
an HDR mastering measurement. Hashes are meaningful only with the same active
extent, sample grid and settings. In `NR -> SR`, its `composited` stage is the
render-resolution image handed to the game's native SR; the subsequent screen
result is assessed by the fixed-scene capture group rather than by reading an
unknown game output state from the add-on.

## Runtime and GPU support

This source does not contain a Feature 18 implementation. Runtime acceptance is
external and varies by GPU generation and runtime provenance. A successful init
followed by `FeatureNotSupported` at create time is not sufficient to prove the
exact internal hardware/kernel restriction.

Identifiers such as `signed_module` in the source are descriptive variable names,
not cryptographic signature verification. Community-modified runtimes and
ShortFuse/RenoDX add-on ownership must be treated as separate concerns. No such
runtime is distributed here.

## Ray Reconstruction

Ray Reconstruction (feature 13) is intentionally unsupported as a carrier for an
additional Feature 18 pass. The correct behavior while RR owns a live handle is
little or no visual change from this add-on because the RR path is passed
through. Live RR handles and an RR-create-in-progress guard bypass companion
feature-1 work; an ambiguous or overflowing ownership table fails closed. Some
engines retain a feature-13 handle after their visible RR setting is disabled.
Those processes remain bypassed until explicit Release or restart rather than
using a racy timeout/dormancy guess. This is coexistence by exclusion, not a
joint RR + NR -> SR implementation.

Feature 13 Create/Release is serialized against the modified Feature 1 interval
with a nonblocking ownership gate. A recursive Feature 13 Evaluate discovered
only after render-resolution Feature 18 commands were already recorded causes
the transaction to restore parameters, quarantine that command-list dependency
and disable future processing. It cannot retroactively erase those commands;
the strict zero-Feature-18 guarantee therefore starts once RR ownership has been
observed, and the transition frame remains an explicit stress-test target.

## Mode 1 command-list state

Mode 1 records the final post-SR codec/composition dispatch on the game's command
list. D3D12 provides no generic way for this add-on to query and restore every
compute root signature, pipeline state, and descriptor heap previously selected
by the title. Mode 1 therefore remains experimental even where its live resource
contract succeeds. Admission is based on current format/view/subrect/exposure/
guide contracts, not an executable-name list. Mode 2 and the master-off native-SR
bypass have a smaller state-interference surface. A universal fix requires either
reliable state shadowing or work recorded on an independently synchronized
command list.

FP16 and R11 output storage can be adapted automatically when the live resource
contract passes. R8 UNORM additionally needs a confirmed linear-versus-sRGB
semantic contract; unknown encoding is deliberately bypassed.

## Post-SR guides

Some runtime/title combinations accept output-resolution NR with lower-resolution
depth and motion guides; others reject that create contract. Rejection falls back
to the original game SR result. Mode 2 may remain available when Mode 1 is not.

## API and deployment scope

- D3D12 only.
- Ordinary DLSS Super Resolution feature 1 only.
- The carrier must be observable as a native NGX D3D12 feature-1 call; OptiScaler
  or another translation layer may hide or replace that call and is unsupported.
- No DX9, DX11, Vulkan, or non-DLSS fallback implementation.
- No anti-cheat or multiplayer support.
- No guarantee across game updates, driver updates, or runtime replacements.
