# Known limitations

## Exposure texture contract

The current code reads scalar `DLSS.Pre.Exposure` and `DLSS.Exposure.Scale` and
records whether `ExposureTexture` exists. It does not sample the exposure
texture. A title that supplies an exposure texture is failed closed to native SR
instead of being adapted from incomplete scalar information. Dynamic exposure
sourced from texture content is not represented in the current history-reset
policy.

## Active extents and dynamic resolution

Color, depth, motion, and output resources may have different physical sizes,
active extents, and non-zero bases. The current adapter handles several observed
contracts and validates physical bounds, but some fallbacks infer an active size
from physical allocation size. That can be semantically wrong for padded or
dynamic resources even when it remains in bounds.

NR contexts are keyed by exact `(game feature, mode, width, height)` and the
current pool is finite. A title that exposes many distinct dynamic-resolution
sizes during one feature lifetime may exhaust the pool and is failed closed to
native SR. Current retirement tracks marked command lists through the real
queue submission. The native submit hook pins the matching slot before the real
`ExecuteCommandLists` call, then signals a private fence on that same submitting
queue before allowing descriptor rebinding or object destruction. A slot seen on
more than one queue, a failed signal, an unprovable hook state, or an ambiguous
submission is conservatively failed closed and quarantined until process exit.
This still needs current-build stress testing with repeated command-list submits,
resets, feature recreation, resolution changes, and multiple queues.

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
neural result equals the proxy.

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
additional Feature 18 pass. The correct behavior while RR is active is little or
no visual change from this add-on because the RR path is passed through. Live RR
handles and an RR-create-in-progress guard bypass companion feature-1 work; an
ambiguous or overflowing ownership table fails closed. This is coexistence by
exclusion, not a joint RR + NR -> SR implementation.

## Mode 1 command-list state

Mode 1 records the final post-SR codec/composition dispatch on the game's command
list. D3D12 provides no generic way for this add-on to query and restore every
compute root signature, pipeline state, and descriptor heap previously selected
by the title. Mode 1 is therefore an experimental, per-title path even where its
Feature 18 contract succeeds. Mode 2 and the master-off native-SR bypass have a
smaller state-interference surface. A universal fix requires either reliable
state shadowing or work recorded on an independently synchronized command list.

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
