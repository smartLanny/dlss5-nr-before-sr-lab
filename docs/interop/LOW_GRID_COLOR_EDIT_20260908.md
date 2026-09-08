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
Its generated source manifest is
`C:\Users\pc1\Documents\ChatGPT\DLSS5\beta050-086-source-r2-20260908\source-manifest.json`,
SHA-256 `f4e334207809258c73e60215cb9bf2eab7c8fd591203090b5509b0f956a84dc5`;
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

When #84 is integrated, its `t7/u7` plane should remain outside the present
`t5/u5` field and `t6/u6` restore descriptors. The allocator must then replace
the separate family totals with one extra-resource total and one per-slot sum:
field + horizontal restore + Lanczos input, including create-before-release
overlap. Until that shared accounting exists, two nominal 256 MiB family caps
must not be described as a 256 MiB overall cap.
