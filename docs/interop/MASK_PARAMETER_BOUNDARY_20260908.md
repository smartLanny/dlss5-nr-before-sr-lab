# #162: bounded parameter-path check, visual bug remains open

The maintainer's repeated-toggle face protection failure and sensitivity to
overall strength remain real reported bugs. Existing successful parameter
rebinding, instance retirement, NR evaluation and composition do not prove that
the model identifies the face or that final face pixels are independently
protected. No repeat game capture was requested or game launched.

A narrow additional check examined only the parameter boundary of the same
local runtime used by the controlled Core experiment: version 310.8.SF.0,
SHA256 `6eb209e764f39872625debd6abaf45e2bb6322f6f270f781f70c059ae30b3927`.
Private binary inspection/evidence was retained locally; no vendor code or
binary is exported in this note.

The consumer reads UseAutoMask through the integer parameter getter, skin
strength through the float getter, and ControlMask through a pointer getter.
ControlMask is consumed as an input, not an exported face-segmentation texture.
The current Core does not supply it. This adds no evidence of a usable mask
output for independent final composition.

A tiny MSVC host performed official NGX initialization and real parameter-object
allocation, without loading our Core, creating SR/NR, or submitting GPU commands.
The committed probe `tests/multi_api/ngx_parameter_roundtrip.cpp` was compiled
and run from `d4cb94a`. For both UseAutoMask and UICorrection, values 0 and 1
written with Set(int) and Set(unsigned) were each read successfully through both
integer overloads: eight cases passed with exact matching values. Parameters
were reset between cases; destruction and shutdown completed. This is one
factory/runtime configuration, not a guarantee across every NGX version.

The private executable SHA256 is
`8cad01e5112473d35e9ce74c5d3cbb0343c3895c4170a311007dba5f9bab1157`;
its result log SHA256 is
`552f26681c05a0b92c4f0f286da096938537213cc5eeff48c24b922df8f61dd6`.
These are parameter-probe artifacts, not a replacement for the `e7df0fc` Core
hardware build or a face-protection visual test.

Therefore a signed/unsigned mismatch is **not supported as the cause here**.
An old Core comment claiming the unsigned writer was mandatory was corrected;
the working writer itself is unchanged. The existing mock test is now clearly
described as checking our writer's shape/boolean values only, not validating the
vendor's getter type or visual effect. No mask fix is claimed from this check.

Next architecture boundary remains the one already identified in #166: obtain
a real engine/model segmentation output with frame/coordinate/validity identity,
or evaluate an independent segmentation source with explicit cost and temporal
edges. Only then can final face strength be independent of whole-image strength.
Skin-color thresholds, dividing skin strength by the global multiplier, and
silently clamping the whole image are not equivalent fixes. This batch does not
introduce any of them or close #162.
