# Low-grid colour EditField integration (#86)

The opt-in `LowGridColorEditField=0` candidate stores one RGBA32F model-grid
field: `x=clamp(log2(Yn/Yp),-8,8)`, `yz` are the neural-minus-proxy changes of
the luminance-normalized opponent axes `(R-G,B-G)`, and `w` contains exact
luma/chroma validity bits. Near-black proxy or neural values retain valid luma
when possible but make chroma invalid; non-finite storage makes the field
invalid. It does not store three per-channel log-RGB values or infer a face
mask.

The opponent change is interpolated first, converted to a unit-luminance RGB
delta, and applied to the full-resolution source with one direction-preserving
scale. `exp2`, source-edge confidence, HDR headroom, source-relative ratio,
gamut projection and final finite/storage checks remain full resolution.
Interpolation does not commute with `exp2` or gamut projection, so this is a
new default-off algorithm rather than an equivalent optimization. The previous
bilinear luma and independent log-chroma implementations remain references.
Composition controls remain outside `SameHistoryConfig` and do not recreate
Feature 18.

#85 keeps production bit `16384`, its RG32F luma field and its colour-zero
restriction. #86 uses bit `32768`; only that option selects RGBA32F. The shared
`ReadLumaEditV6(int2)` reads `(log,valid)` directly for RG32F and decodes
`(x,w bit 0)` for RGBA32F. A format change occurs only behind the existing safe
slot mutation gate. Create-before-release replacement includes the old field,
the horizontal restore plane and the new field in the real 64 MiB slot and
256 MiB Core peak checks. Failure retains the old resource and composition A.

The reviewed #86 source is `9670b1057a5fbd9634ad198c6c0ff84e668c528f`.
Its private generated source manifest has SHA-256
`f4e334207809258c73e60215cb9bf2eab7c8fd591203090b5509b0f956a84dc5`;
all 103 listed files matched and the generated Core source SHA-256 was
`84865e9f92bdb3a1f47e267bd1e1cc0f00ff9b7a42fc4314dff4c74a3ad1b906`.
The 30-group production WARP fixture passed explicit validity, finite output,
normal/extreme/near-black/invalid inputs, and byte-identical default-off output
against dev6 `6062a0b`. Final bilingual links from that source passed: English
Core `028d6b4cde5d44c61294cb5eb9bd2a2f3216e9562f69d75dcae041fe2812bca5`,
Chinese Core `5db6d2b6374b578e402bc9660836ba0dd546c476a7a9d75822196b336292adcc`,
and `nrchain_nvngx.dll`
`46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb`.
These are source, WARP and build facts, not NVIDIA inference, game-quality or
performance acceptance. #85 colour Catmull was not tested or enabled.

The ordinary #85 + #86 merge source is
`8982733ce456934c884c64255e6788901cc594e1`. Its private generated source
manifest SHA-256 is
`e6cbc65c7590c5122059c5a064a6ababc7ff3514c4739f1cb8b5c3d674e16eef`;
all 103 listed files matched and the transformed Core source SHA-256 is
`8b8e6cb2a907c098cafac57d1ddb019d9be9f8df0c10a692675a6c0e66664556`.
All nine production shader entry points and both focused GPU fixtures compiled
without dispatch. Bilingual integration links passed: English Core
`124b88339a19666ea97415dd3205483a5677c12e29cfed603d935c3112327432`,
Chinese Core `81a8937b21bf212359c8afd3faf97b7b16fb246d24c3a0327eaeb6055b13844e`,
and `nrchain_nvngx.dll`
`46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb`.
The merge reused the already completed #85 and #86 WARP evidence; it did not
repeat either GPU run or claim that Catmull reconstructs colour.

#84 integration keeps its input plane at `t7/u7`, outside the `t5/u5` field and
`t6/u6` restore descriptors. The merged allocator uses one extra-resource total
and one per-slot sum for field + horizontal restore + Lanczos input, including
create-before-release overlap. The resulting 64 MiB slot and 256 MiB Core caps
therefore cover all three optional planes together.

The three-slice production merge is
`bab376699fb609f35c615c1333917e2a6e540d6a`. Its private generated dev7
103-entry source manifest has SHA-256
`375722b279322eb5e1fff3d7b17b1895894a7c225ec5fdf11761b68834f91d1e`
with zero mismatches, and its transformed Core source SHA-256 is
`32c6fa500806f6867d50b83e7c8afa3bd8a6a0f87d39053cfb9f80b26b67eab9`.
All eleven SM5 entries and bilingual Core linked. English Core SHA-256 is
`4af2c4b2543bb6ece599a56035cad6c512edfb14dc2877a1b7cbeda1e10519d4`;
Chinese Core is
`4442e8fa20a3f9bb2c7389ec77c372581a1657dd646af40c98e695dc1d6fcb4b`.

One combined WARP fixture passed 13 groups: eight all-off byte comparisons to
dev6, one RG luma field, one colour-zero RG + Catmull path, one RGBA opponent
colour path, one finite Lanczos input/prefix test, and one unknown-domain
invalid-input protection test. The Lanczos fixture stops at Proxy and does not
read old Neural or represent a real NR result. The runner then unintentionally
fell through into its existing default matrix because the new branch lacked a
`return`; that additional descriptor/motion/boost/layer/codec matrix also
passed. Commit `96057b355d6675533aec195c1e05e4617ab9e87e` fixes the runner without
another GPU execution. No 200/80/30 raw export was repeated. The private WARP
log SHA-256 is
`5ceddd191c98b06b31e13311fe6e3d9af813190321efc55b2fe929daa7f20840`;
the private validation summary SHA-256 is
`af36d9ed755f750f8716e2ba7bb724c55d19a4a82c4a42dd2f0a9af24c72df36`.
