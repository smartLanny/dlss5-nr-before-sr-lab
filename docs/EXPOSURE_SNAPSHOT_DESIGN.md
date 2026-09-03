# Exposure snapshot follow-up design

Status: proposed separate change, **not implemented by the r3 diagnostic candidate**.
Prepared on 2026-09-04. This document defines the evidence and validation needed
before the follow-up can be merged or described as an exposure fix.

## Current behavior and evidence

`ReadColorContract` captures the scalar exposure parameters at the entrance to
the game SR call. The post-SR path retains that scalar snapshot across native SR.
The GPU texture is different: Encode and Combine each load the external
`ExposureTexture` red channel. A supported resource must be a shader-loadable
1x1 Texture2D in the accepted floating-point format family. These descriptor
checks do not prove the resource's current D3D12 state or that its value remained
unchanged between the two shader passes.

The current shader replaces invalid texture values with 1.0 and clamps usable
values. It does not carry a separate validity bit through both passes. Existing
exposure diagnostics use delayed GPU readback and can schedule a later history
reset; they cannot repair an already recorded frame or prove the original
sample's validity after substitution.

The supplied Wuthering Waves r1 feedback does **not** establish this texture bug:
all 55 exposure records in its 319,506-byte NR log reported no ExposureTexture,
with changing scalar PreExposure and unit scalar ExposureScale. Investigate its
weak-effect/lighting report through the scalar, proxy, model and composition
stages. Do not claim that the design below fixes that report.

## Entrance contract and immutable snapshot

Capture the texture once, on the same graphics command list immediately before
the intercepted native Feature 1 Evaluate. This location is shared by both
processing orders; post-SR Encode must consume the entrance snapshot, not reload
the external texture after SR. The intended source state is one already legal
for a compute shader read: `NON_PIXEL_SHADER_RESOURCE`, or a combined read state
that includes it, such as `GENERIC_READ`.

Admission requires a verified caller/resource-state contract at that entrance.
An engine integration that transitions the resource for texture reads before
NGX can supply such evidence. `GetDesc`, typed-load format support, and the mere
presence of an NGX parameter cannot supply it. If state is not established,
retain native SR. Do not invent an external `COMMON -> shader-read` barrier,
guess the old state, or infer it from the game's name. This follow-up does not
implement general command-list state shadowing.

Add a per-slot owned GPU snapshot containing the sampled finite value, explicit
validity, and a capture serial. Associate it with the Feature 1 generation and a
context allocation epoch assigned before the first snapshot; add the successful
NR instance ID when Create succeeds. A reused table slot or an instance ID
still equal to zero cannot identify an allocation on its own. Frozen CPU
metadata carries the same identifiers. Define validity from the raw sample
before clamping or substitution: finite, greater than 0.000001, and at most
65504. Clamp valid values to [1/1024, 1024], preserving the current numerical
behavior as a separate operation. Test each boundary before considering a
broader range.

Both Encode and Combine read this owned snapshot. Invalid samples may use a
finite neutral placeholder internally to prevent NaNs, but their validity stays
false and their visible transfer weight is zero. They must never be reported
as valid unit exposure. A genuinely absent optional texture with an admissible
scalar contract is a separate path; it must not be confused with an invalid
present texture, a failed descriptor check, or an unknown resource state.

## Temporal validity and recovery

A per-context GPU latch marks history tainted when any evaluation sees an
invalid snapshot. Combine reads that latch as well as the current validity:
returning to a valid exposure value alone must not make contaminated temporal
history visible during the CPU readback delay. Native SR remains the visible
result. Feature 18 may still execute with a finite placeholder on such frames;
this is distinct from the F6-disabled path, which must perform zero Feature 18
work.

Use a small mandatory snapshot/status readback ring independent of optional
color diagnostics, capture requests and their sampling cadence. Read a slot
only after its exact owning queue fence completes. The CPU consumes invalid
status and schedules Reset for the matching live context. It does not use a
previous exposure value as the current frame's value.

Clear the GPU taint latch only through work recorded after a successful native
Feature 18 Evaluate with Reset requested, on the same proven queue, and only
when that evaluation's entrance snapshot is valid. Order the clear after NR
work and before the corresponding visible combine using the required buffer
barriers. A failed Create/Evaluate, invalid reset-frame snapshot, missing
submission proof or canceled recording cannot clear it. A failed Reset request
remains pending until a successful matching evaluation.

CPU acknowledgments must include capture/reset serials and context identity.
An older readback arriving after a newer reset acknowledgment cannot clear or
reopen a different generation's state. A callback from a retired context must
not modify its replacement. Context-local GPU latch buffers and slot resources
retain exact-fence retirement; never repurpose their storage merely because a
new frame or game handle with the same pointer appeared. Ring exhaustion keeps
the native output and recovery pending rather than assuming validity or waiting
on the rendering thread for a readback slot.

## Required validation before implementation is accepted

1. Obtain a current-build trace from a game that actually binds a supported
   ExposureTexture, and establish its state at Feature 1 entrance with an
   integration contract or a graphics capture. Preserve game/driver/runtime
   identity, processing order and active HDR state in the evidence.
2. Build a D3D12 GPU harness exercising a 1x1 texture changed between the Encode
   and Combine locations. Both transforms must use the identical entrance
   sample. Verify ordinary exposure ramps and large EV jumps in both modes.
3. Inject zero, negative, NaN and infinite samples, plus a valid-invalid-valid
   sequence shorter than the CPU readback delay. Every tainted visible result
   must remain native until a valid successful Reset acknowledgment; all
   internal values and output pixels must remain finite.
4. Exercise failed Create, failed Evaluate with Reset, unsupported/missing
   resource, ring exhaustion, unsubmitted command list, delayed fence, reset of
   a recording, and out-of-order status collection. Confirm no stale value or
   acknowledgment is applied to another context or generation.
5. Verify pointer rotation without semantic changes, DRS, mode changes and HDR
   switches; validate descriptors/barriers with the D3D12 debug layer and GPU
   validation where supported. Measure the additional GPU and CPU cost.
6. Re-run scalar-only scenes, including the reported Wuthering Waves case,
   without changing their scalar exposure behavior. Test F6 off independently.

The first diagnostic candidate deliberately keeps this GPU temporal-state
change separate: the community texture failure is not yet reproduced, state
admission is not established for arbitrary callers, and correctness depends on
failure/recovery ordering beyond merely adding a cached float. Compile success
or a sparse diagnostic sample is insufficient to fold it into r3.
