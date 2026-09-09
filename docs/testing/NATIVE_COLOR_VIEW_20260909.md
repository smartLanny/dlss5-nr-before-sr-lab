# Native color-view production fixture

`validate-native-color-view.py` compiles the complete staged Core translation
unit. It borrows the existing queue fixture's WARP device/list/fence helpers and
the post-output fixture's upload/readback helpers without editing either file.
The new process requires WARP; it never falls back to a hardware adapter.

Native view evidence comes from real D3D12 descriptor creation inside
`NrCallNativeWithFormatObservation`, through the installed production MinHook
SRV/UAV hooks. The fixture never writes successful entries to the view ledger.
The callback is constructed test code, not a native NGX model invocation.

The focused 2026-09-09 run passes 11,037 checks, exit 0:

- 16 typed-28/typeless-27, pre/post, identity/nonzero neural and full/half-model
  cases. Each case also rebinds to a differently sized real resource.
- Actual CreateSlot, FindOrCreateSlot, nine-resource slot recycling,
  RebindSlotExternal, complete descriptor rebuilding and post snapshot writers.
- Actual Encode/Combine and packed native-output snapshot shaders. Both resource
  interpretations produce byte-identical final storage with the same
  ModelEncodedValueDomain contract and no profile override.
- Pre Color requires no UAV and remains byte-identical. Nonzero subrects retain
  all outside pixels; post alpha remains exact, while pre replacement alpha is
  compared within its FP16 quantization.
- Unknown, unsupported-only and conflicting interpretations refuse reuse without
  partial rebind or additional native callbacks. A subsequent draw checks the
  previously valid descriptor contents. Feature generation cannot inherit a
  stale slot key.

The focused input uses Core SHA256
`aac684bbba18970a8006922525ec9f016f959f6760e72668a92ef7e1657a2d0b`
and an explicit latest runtime overlay SHA256
`d352abfa5e07bf0d5afcbb1107b43f0183e7f7f0fcf60631632ed6d79b2b81b2`.
The runner snapshots and hashes every source input and records the overlay,
fixture and executable fingerprints in its result. The final unified candidate
must rerun against its complete canonical staging without a focused overlay.

D3D12 debug-layer and InfoQueue interfaces were unavailable in this run; the
result is not a debug-layer pass. The test executes constructed neural planes
and 56 constructed native callbacks, not NVIDIA inference or game integration.
DS2, ZZZ and Watch Dogs Legion still need their own actual game acceptance.
