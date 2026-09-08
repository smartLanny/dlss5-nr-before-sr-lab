# Independent D legacy-post extension

Initial scalar slice base: D integration `c237ac969ef1baa576bf3bff6204850fe7c718d0`.
Final D2 build source: `e071c778d10cb4f079857f128aad3093defb7b9e`, including
the ordinary merge of final D1 source `59ab029614df6fad6ea1e95df18a0a986be58962`.
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

Final focused acceptance completed with `success=true` and 27 required checks.
`tests/post_light_style_gpu.inl` is now included by the production fixture;
`validate-codec-gpu.ps1 -PostLightStyle` verifies both generated-source
manifests and runs legacy post directly (`v2=false`). Final D1's full generated
source is the WARP reference, including scaled post sampling, rather than the
raw original shader. Its recorded source SHA256 is
`54f58cecc6e3cb7343f5901cfd5c60d25a8e1049b458bbf820562f48a1f8893f`.
The full run includes front/post light groups, existing combined low-grid WARP,
SM5 compilation and full bilingual Core/owned bridge linking. A separate
post-only confirmation captured native output for all 62 WARP groups: neutral
legacy bytes, independent broad/fine signs, LightingLock 0/.2/1, scaled
colour/finite/alpha and previews. All model planes are synthetic.

Frozen build identity:

| Artifact | Bytes | SHA256 |
| --- | ---: | --- |
| Source manifest | — | `e6987637948070319168ad098ebc57d1cbb582673dfb77cbb03adf50d0591e20` |
| Chinese Core | 1496064 | `ea43c49a81cd5d30a97ab2ada4b0d57a172c23accbeb4ad6a0a2753801c231d7` |
| English Core | 1467392 | `934265b9c2953f96061fb9d9a9ccbcc27fd9069f9d959347879ba356c685040a` |
| nrchain | 8192 | `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb` |

Actual DLL menu validation used ReShade SDK
`4a50d1eddace85734871d91792ff214f13f66c01` and hash-locked ReShade 6.8.0.
Both languages ran 40 frames and six overlay events, exited successfully,
preserved fixture INI and verified all 109 source files before/after. Three
screenshots per language were visually inspected: controls, presets and face
settings are readable. These are pre-SR menu snapshots without injected
preset clicks; they do not validate post-mode UI interaction or game output.
Machine receipts and separate visual-review receipt remain private.

The owner requested independent D testing in Neverness To Everness with RR
off, and deferred ABC integration. Chinese D2 was installed with a verified
dev14 backup, exactly one live Core, Mode1, LightingLock=.2 and neutral light
controls; other saved settings and ReShade were preserved. The installer did
not edit encrypted game settings or launch the game. Owner-reported facial
darkening in a previous post candidate is recorded, not claimed fixed.
NVIDIA feature18, game output, motion and performance remain unverified.
No new RR adapter, ABC merge, main/release merge or stable release is implied.
