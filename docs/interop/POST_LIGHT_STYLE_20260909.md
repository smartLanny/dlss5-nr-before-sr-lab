# Independent D legacy-post extension

Base: first-batch D integration `c237ac969ef1baa576bf3bff6204850fe7c718d0`.
This is a second candidate and does not alter the frozen pre-SR-only package.

Legacy `CombineCS` retains its linear relative luminance edit
`(Yn-Yp)/max(Yp,1/1024)`. Its D neighbourhood samples the same decoded
Proxy/Neural values, reconstructs on the same composition grid, and averages
that same relative-edit expression. The radius is eight composition pixels,
independent of the post NR working resolution. Invalid taps contribute no
weight, and the valid centre anchors the denominator. This is an additional
four-neighbour estimate; the existing differently expressed LightingLock
neighbourhood is unchanged.

The raw relative-edit partition is established before protection. The original
LightingLock block runs unchanged. A nonneutral D request then applies the
shared dimensionless gain to the remaining relative edit through the legacy
`1 + relativeEdit * effectiveStrength` expression. The effective strength is
capped at four. The original ratio clamp and near-black confidence follow.
No exponentiation of the relative edit is introduced. An explicitly neutral
flag, including preview-only requests, takes the exact old luma arithmetic.

Original model decoding, source/exposure normalization, LightingLock, colour
channel-ratio transfer, target-luma renormalization and output storage remain
byte-for-byte identical source statements. D does not modify Neural RGB to
adjust luminance, so original model colour evidence remains intact. Preview
encoding alone uses the already matching codec encoding helper; invalid input
and master-zero bypass remain original-image paths. Preview 1 shows the
remaining relative-edit partitions, with the same colour legend as pre-SR;
preview 2 shows final NR/source absolute linear difference x4.

`DispatchCodec` also accepts incoming legacy `combine`, which includes ordinary
post-SR and existing RR automatic post-SR while the saved mode remains 2.
Existing optional post redirection to V2 still depends exclusively on the
existing independent-colour/field requests; D never changes that selection.
If the user already selected shared V2 post, its unchanged V2 body uses the
existing log-edit D implementation. Each route retains its own edit domain;
the same slider values are not claimed to produce identical pre/post pixels.
There is no new setting, constant, flag, pass, texture, NR call or history.
The existing 41 constants and production flag 262144 remain in use.

Completed locally, without GPU dispatch:

- `validate-post-light-style.py --negative-control`: 12,015 assertions compile
  the actual post luma/protection statements against deterministic taps, with
  LightingLock 0/.2/1, four partitions, natural/glow controls, master 0/1/2/4
  and near-black confidence. Four successfully compiled mutants (log transfer,
  all-positive glow, ignored D controls, ignored lighting protection) failed
  the exact numeric oracle, not a compiler/runtime setup check.
- The same test compares original decode/normalizer/lighting/colour/storage
  source against the base, and confirms the V2 body and colour-routing block
  are unchanged. `--baseline-ref` defaults to the frozen base above.
- All eleven production SM5 entries compiled through
  `validate-codec-shader.ps1`; existing exposure/finite/optional-restore
  warnings were retained. No shader compile error was suppressed.

`tests/post_light_style_gpu.inl` is an optional unexecuted coordinator hook.
Insert it after common `Run`, `LightCase`, `Gpu`, and frozen `kMaintenanceShader`
definitions, then call `ValidatePostLightStyle(gpu)`. It uses the current
41-constant harness interface and tests legacy post directly (`v2=false`).
The baseline and candidate must both be actual generated post-compatible
sources so scaled sampling is present. The checked-in main harness/runner were
not changed. CPU mutants above remain the executable negative-control gates.

Before integration: compile and run the optional hook in the coordinated WARP
slot, check the runtime route/receipt contract, update player support wording,
then perform the separate frozen generated-source Core build and actual-game
acceptance. This slice did not perform GPU, NVIDIA, game, timing, hosted CI,
push, package or deployment work.
