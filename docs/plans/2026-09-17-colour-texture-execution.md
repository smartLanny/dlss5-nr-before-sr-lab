# D21 colour/texture branch — implementation and owner acceptance

## Frozen baseline and present delivery state

- Original reproduction: D21 `6c3a26fb610fb0b4d3de661fe7db25479a0a24f9`.
- Development base: #279 uniform3 `58ce637c1fbb03d0e7d93b90d26b3e4bce7b03df`.
- Branch / review: `codex/d21-colour-texture-lab-20260917`, PR #281.
- M1 parent: `406e7acb0aecc1f0d2b854532491fdbdddd9836f`.
- Current candidate identity: `beta0.5-dline21-colour2` (M2 + M3a).
- #280/lockperf is not included. No game files or Windows workspace were changed by this implementation session.

**Source candidate, not an installable owner-test delivery yet.** Production math has portable CPU checks. Full upstream generation, Windows Core/shader compilation, CI success, GPU execution and owner game acceptance are still outstanding. This session did not obtain a callable NewWin-Devspace interface after tool discovery and plugin search. That does not establish whether the computer itself is online.

## Implemented selectors

All keys are under `[NRBeforeSR]`, loaded/saved through the existing transaction. UI is in the normal light/quality group.

| Key | Values | Default | NR history |
|---|---|---|---|
| `ColourLabMode` | 0 inherited colour policy; 1 experimental colour priority; 2 conservative unverified-HDR restriction | 1 | No new reset |
| `ColourRestoreMode` | 0 inherited D21 compositor; 1 matched RGB/source-reference restoration | 1 | No new reset |
| `ColourInputCurve` | 0 inherited D21 input; 1 paired N2 HDR preparation | 0 | Reset when this input selection changes |

Invalid colour-policy IDs resolve conservatively. Invalid restoration/input-curve IDs resolve to their old paths. The Legacy button selects 0/0/0, retaining other picture/model/layer preferences. Switching from N2 back to Legacy therefore resets input history; an output-only switch does not. Existing saved HDR permission still applies in colour-policy mode 0.

`ColourLabMode` controls visual colour admission only, never resource/view/subrect, ownership, device, command-list, queue, fence or writeback safety. Mode 1 can expose internal HDR colour even on an SDR display; it is not proof that an input contract is correct.

## M2: matched RGB and original-source recovery (implemented in source)

Both actual `CombineCS` and `CombineV2CS` call the same production helper. It consumes the matched proxy/model values already sampled by that compositor, including the existing final multilayer accessor. Per-tap model decoding remains before linear interpolation; no gamma-domain residual texture is introduced.

For a positive source with a proportional preparation:

```
source_scale = peak(prepared_full_source) / peak(full_source)
source_edit = (matched_model - matched_proxy) / source_scale
reference_candidate = full_source + source_edit
```

The full original remains the base. The low-resolution processed picture does not replace it, so existing source texture is not discarded. Source-reference recovery can create a colour in an originally zero channel; per-channel multiplication cannot. The source's forward scale is used, not an analytic inverse evaluated on the edited neural output.

The existing luminance path supplies lighting/detail/edge/region/face protection. Its bounded protected/raw relative edit becomes a gain on restored edit luminance. Colour is separated from nonzero total strength, then constrained by the requested colour amount, confidence, region/face controls, a whole-vector budget, and luminance-preserving gamut projection. Actual RGB composition uses the independent CPU colour budget, avoiding an extra inverse-strength attenuation. MaxLumaRatio and finite/original-image fallbacks remain.

At neutral controls and representable in-gamut samples, T=C=1 reconstructs the source-plus-edit reference. Protection, near-black confidence, ratio/gamut limits and unsupported signed originals can intentionally depart from that reference. This is not a claim of RenoDX bit equivalence or measured game quality.

### Correct preparation provenance

Final composition PSO and requested UI mode are not proof of input encoding. In particular, post-SR/RR may use CombineV2CS while its proxy came from EncodeCS.

Each slot stores a small CPU encoding receipt only after its actual EncodeCS / EncodeV2CS / LanczosVertical dispatch is recorded. The receipt contains the current nonzero slot-use serial and effective input-curve choice. Composition requires a matching serial before selecting RGB, and uses the actual producer's forward preparation for the full-source reference. Missing provenance retains the baseline consumer and is explained in the UI with F8/BUG guidance. This is a command-recording receipt, not GPU-completion evidence; existing lifetime/submission checks remain authoritative.

There are no new textures, descriptors, root constants, NR invocations or GPU passes. Arithmetic and generated shader size change, so zero performance cost is **not** claimed. The existing 60-DWORD constant block and descriptor-table root ABI remain unchanged.

## M3a: optional paired N2 HDR input (implemented in source)

`ColourInputCurve=1` uses the same positive HDR preparation in both existing encoder families:

```
prepared = source_in_working_units / sqrt(1 + peak(source_in_working_units)^2)
```

RGB restoration uses this original-source scale, not the model output's inverse pole. The existing exposure, paper-white and PQ storage conversions remain in their own source paths. Bounded SDR/model-value inputs keep their existing preparation; no unknown gamma is guessed. Requested/effective input choices are displayed separately.

Input selection participates in SameHistoryConfig; output restoration does not. Default input selection 0 retains D21's inherited preparation. No analytic-neural-inverse switch, gamut calibration, signed-HDR recovery or claim of complete RenoDX codec reconstruction is included.

## Integration and diagnostics

New production files: `src/nr_colour_restore.hlsli`, `src/nr_colour_prepare.hlsli`, `src/nr_colour_restore_policy.h`. The overlay `scripts/patch-colour-restore.py` is applied after the unchanged pinned uniform3 exporter, compatibility adapters and M1 overlay. The normal checkout is not rewritten.

Exact-once anchors reject missing/duplicated drift. All transforms validate before temporary-stage writes. Existing ancestry, source pins, clean-source, upstream and publish checks remain. Manifest hashes and colour2 identity are recalculated. The old `prepare-multi-api-beta.py` still builds uniform3; **use the colour-lab entry for this candidate**.

Recorded codec flags: bit30 (1073741824) RGB; bit29 (536870912) actual post EncodeCS preparation; bit28 (268435456) paired N2 HDR. F8 records requested choices from its coherent configuration snapshot, and existing actual codec constants retain the selected flags. UI does not treat stale/pending/bypassed records as a current result. No new per-pixel atomics, readbacks or per-frame log writes are added.

Negative original channels, invalid recovery or native safety bypass require collecting F8 and submitting/updating a BUG. The implementation retains the exact original on rejected pixels; no signed recovery success is claimed. A permitted colour coefficient alone does not prove the model supplied a visible change.

## Verification executed in this implementation session

| Check | Result |
|---|---|
| Production HLSL mathematical sections through a minimal C++ float3 adapter | Clang and GCC each passed 200,077 runtime checks and 18 test static assertions |
| M1 policy C++20 test | Passed (10 static assertions; 402 runtime comparisons) |
| M2/M3a Python wiring/atomic-stage/UI tests | 7 passed; 2 full-source/export tests skipped |
| M1 Python tests | 4 passed; 1 full-source test skipped |
| Python module syntax checks | Passed |
| Actual GitHub source connections | Inspected for both consumers, source preparation, final-neural accessor, DispatchCodec and slot serial; not a full translation-unit compile |
| Complete raw-source transform and generated stage | Not executed; full pinned checkout/upstream absent in cloud container |
| Windows Core, real cs_5_0 compiler and all face/dual variants | Not executed |
| GPU, NGX, game, FPS, temporal quality or RenoDX parity | Not executed |

Math checks compile the production sections, not a separate CPU algorithm. The adapter is not an HLSL compiler or a GPU precision model. Random tests cover neutral recovery, independent colour, colour/ratio/gamut bounds, source-reference HDR, zero-channel and identity cases; they do not validate texture addressing, barriers or actual presentation. Wiring fixtures are explicitly not a complete generated Core. See `docs/validation/2026-09-17-colour2.md` for commands and test matrix.

## Next milestones and gates

1. **Build/owner-test gate (first):** from a clean branch checkout on Windows, run build-colour-lab.ps1. It requires full-source tests, a successful pinned stage, generated-source tests and the existing full Core/shader build. Produce a labelled bilingual test package with hashes and exact source identity only after those succeed. Check old-mode image parity before claiming a usable candidate.
2. **Owner A/B/C:** compare inherited composition, RGB with identical input, and RGB + N2 HDR with settled histories, separately for NR→SR and SR/RR→NR. Keep all models, layer count, sizes, lighting controls and game settings fixed. Compare against the same RenoDX DLL/model and equivalent source stage.
3. **M3b:** use owner evidence for domain/exposure/white-point/negative-colour calibration and dark/highlight boundaries. Only add an analytic inverse as a separate bounded experiment with its own acceptance; no dummy switch now.
4. **M4:** forward-integrate reviewed #279 retirement/memory fixes, handle independent face-backend delivery and AutoMask hot edits, and preserve the requested auxiliary 50%-efficiency versus true-100%-reference distinction. No mixed-resolution or retirement changes are smuggled into this colour patch.
5. **M5:** evaluate existing private residual SR/RR work before reuse; it is not already delivered here. Early integration branch should compare uniform / +colour / +perf / +both after each component's build gate. Clean textual merging is not runtime validation.

Tracking: #274, #109, #103, #104/#71; #279 base; #280/#277 performance separate. Keep issues open until actual game acceptance. Draft status remains until the build gate is satisfied.
