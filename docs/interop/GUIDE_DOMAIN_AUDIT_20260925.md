# 2026-09-25 — guide domain audit and bounded safety patch

## Scope / current source

This is a small follow-up to #307 at `e9313af1d4a8640e9f3d2f89a47aa54b634d7d9f`, on top of #306 / #300 unified10. It does not replace Reconstruction1, integrate third-party runtime code, or create a new product route. `main` at `dae86e9898399c20e52ada54a5ec4350da4d4fa1` is not the development baseline.

The #307 head already contains actual opt-in reconstruction/chroma algorithms; the older CURRENT paragraph calling that branch research-only is not its latest runtime state. Existing native DX11/carrier, provider/transport, RR post, multi-pass, temporal AMDOF, Manager and in-flight retirement work is retained.

## Source findings, including a correction to the preceding research report

| Topic | Actual current code | Action |
| --- | --- | --- |
| Native/NGX MV spatial sampling | `ResolveNrGuideBinding` + `ResampleMotionCS` already resample the logical MV rectangle to N, retaining vector values. Physical padding is not the logical rectangle. | Keep. Test low/high-resolution fields and nonzero atlas origins. |
| Post-SR / post-RR N/R scale | `scripts/patch-post-compat.py` expands both resampling and **N/R value conversion** from mode 2 to mode 1. Reading only the raw C++ misses the final post behavior. | **ALREADY IMPLEMENTED**; do not duplicate the external low-resolution-MV fix. |
| High-resolution MV storage | Source values use the NGX render-pixel convention, not automatically output-pixel units. | Keep the measurement grid independent from the storage grid. Add a negative control that substitutes storage width as the denominator. |
| Depth at low N | Depth remains the declared render rectangle R; RR linear-depth conversion also preserves that grid. | **PARTIALLY COVERED / NEW DELTA** as an experiment, not a proven defect. No production Depth→N rewrite or default change in this patch. |
| Converted MV numeric validity | The old function checks raw scale validity but not finiteness after multiplication. Finite float-max with post N=2R produces infinity. A zero N axis was also accepted. | Reject at guide resolution, before context selection / command recording. Preserve original output; report `guide-contract-invalid` and F8 instructions. |
| Capture observability | Existing capture has final depth/MV sizes and raw/final scales, but not the independent R, N and original logical MV rectangle. | Add optional, immutable `guideDomain` metadata to existing F7 deep capture. No new GPU readback. |

The overflow is a deliberately constructed production-function boundary counterexample, **not a demonstrated cause of the user's face noise, boiling, 99/100 colour difference, or a particular game crash**. Existing absent/zero/nonfinite raw-scale fallback remains unchanged. Legitimate signs and existing float arithmetic remain unchanged. The new rejection is not a permanent capability latch: a later valid Evaluate is still admitted.

### Primary references

- [OptiScaler-NR PR #109](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/pull/109), inspected head `dcb7d882de58608f41a710a3b90a1b65f12386ff`: actual guide matching plus low-resolution MV reference fix, with author-reported Fable/Onimusha A/B. No maintainer acceptance or our game verification is inferred.
- [NVIDIA Streamline sl.dlss implementation](https://github.com/NVIDIA-RTX/Streamline/blob/e8aaa6eaac968711fb62473d4ae8256dde20919b/source/plugins/sl.dlss/dlssEntry.cpp): `renderWidth/Height` derive from input colour extent; `MV.Scale` is built from the common MV scale and those render dimensions. Pixel-space camera-motion processing is a separately declared path. This evidence does **not** permit inferring vector units from texture dimensions.
- #87 was closed as **deferred / consolidated into #71**, not completed algorithm delivery. Do not reopen it or claim its old checkbox list is implemented. Current integration/testing remains #294 and picture/working-scale tracking #274 / #277.

No external implementation is copied. The change is independently written against the existing repository's contract and capture code.

## Capture interpretation

The existing `.f8c` container and frame metadata remain schema 4. `guideDomain` is an **optional additive schema-1 object**, so older captures remain readable. `known=0` means unknown; it is not a 0×0 valid measurement. Recorded fields:

- `renderWidth/Height`: the declared **NGX input-contract R**, not necessarily the game's true render resolution for an external/Synthetic provider.
- `workWidth/Height`: actual NR N at guide resolution.
- `mvSourceWidth/Height`, `mvSourceX/Y`: logical incoming MV raster and its original atlas origin, before point resampling.
- `gameMvLowResolution`: the original NGX create flag, before NR's own flags are normalized.
- `mvToWorkX/Y`: the actual value-domain conversion applied; spatial sampling does not multiply vector values a second time.
- `mvFallbackX/Y`: whether the existing invalid/missing raw-scale fallback was used.

Existing `depthWidth/Height`, `mvWidth/Height`, `gameMvX/Y` and `nrMvX/Y` remain final bound sizes and raw/final scalar observations. `boundary="ngx-evaluate"` and `nativeProvenanceVerified=false` deliberately avoid promoting a feeder to Native/Extracted. Determine provenance from the paired provider/source receipt. No pixel confidence, control mask or MV-pixel capture is claimed; the old `motionAvailable=false` remains unchanged.

## Reproducible automated tests

Use a supported Windows LLVM-MinGW C++ compiler. All output directories must be new; old evidence is preserved.

```powershell
$Cxx = '<LLVM-MinGW>\bin\x86_64-w64-mingw32-clang++.exe'
python scripts/validate-guide-domain.py --compiler $Cxx --output '<new-base-evidence>'
python scripts/prepare-colour4-edge.py --upstream '<pinned-carrier-checkout>' --output '<new-generated-stage>'
python scripts/validate-guide-domain.py --compiler $Cxx --source '<new-generated-stage>\core\src\nr_before_sr.cpp' --output '<new-generated-evidence>'
python scripts/validate-f8-controller.py --compiler $Cxx --source '<new-generated-stage>\core\src\nr_before_sr.cpp'
```

Commit reviewed source/pins before the canonical exporter: it intentionally refuses dirty or unreviewed integration inputs. Do not disable those checks. `build-unified-candidate.ps1` now runs the new guide gate on both raw and **final generated** source, in addition to every existing cumulative gate; full face/FFX SDK/cache inputs remain mandatory.

`validate-guide-domain.py` compiles and runs the actual extracted `ResolveNrGuideBinding` via the existing guide/history test, then compiles three isolated inverse mutations. A negative test passes only when the binary compiled and failed at the expected assertion—not when compilation failed:

1. Remove empty-grid rejection → the zero-width call must touch no resource getter. This catches a late rejection even when the second numeric guard also rejects the zero scale.
2. Remove post-conversion finite/zero rejection → the float-overflow checks must fail.
3. Replace measurement R with MV storage width → the high-resolution-MV coordinate check must fail.

The positive matrix includes pre/post source generation, low/high-resolution MV, atlas origins, N at 100/99-rounded/90/70/50 percent, signed scales, depth staying R, ordinary raw-scale fallbacks, recovery after rejection, existing jitter/history/submission behavior. The test also compiles the **actual C++ capture serializer** and independently parses its JSON: unknown vs known domains, full-resolution storage ≠ R, fallback flags, nonfinite JSON→null, and no invented Native provenance. Staged headers are checked against this revision rather than silently mixing versions.

These are production-function/serializer **CPU fixtures**. They do not run NGX, test real game MV pixels, prove temporal stability, or replace the mandatory full build/WARP gates.

## Owner game verification after integration (not yet performed)

First use one offline/native-DLSS game and the same save, driver, signed/runtime identity and model. Do not change reconstruction/chroma/colour parameters during guide testing. Keep dynamic resolution off for the first scan; fix the game's real R and output O. Do not lower the game's RenderScale when changing only NR N.

| Pass | Settings / observation |
| --- | --- |
| Baseline | NR off; then NR on at N=100%, one layer, temporal extra filter off, FG off. Verify original chain and actual NR adoption. |
| Low N | At the same game R, test UI work percentages 99%, 90%, 70%, 50%; use recorded integer extents rather than assuming exact percent after rounding. Pre percentages reference R; post percentages reference O. |
| Motion | Each sample: 2 s stationary → 5 s steady horizontal camera motion → 2 s stop → object/face motion plus occlusion. Include foliage, thin geometry, highlights and a cut. Record video and paired F8 TXT; use existing F7 deep capture for `guideDomain`. Do not include capture overhead in timing. |
| Other routes | Repeat two layers, SR→NR, then successful RR→NR independently; only then test HDR and FG on/off. Native guides and Synthetic/AMDOF comparisons are separate cases. No new FG algorithm/backend/router is added. |
| Lifecycle | Change N repeatedly, resize/DRS, Alt-Tab, F6 off/on and exit. Old capture/descriptor/model retirement and #306 Create admission must retain their prior behavior. |
| Failure | `guide-contract-invalid` must leave native output working and be visible in status/F8/log. Stop, retain the report and roll back on persistent bypass, black frames or new instability. Do not deliberately feed arbitrary float-max into a game. |

Inspect a deep capture with the existing reader:

```powershell
python scripts/parse-f8-bundle.py '<F7-deep-capture.zip>' --output '<analysis.json>'
```

Use that script's ordinary analysis output to inspect each record's `metadata.guideDomain`. Seeing Depth R ≠ N only establishes **raster mismatch**, not its cause or harm. No final-output freezing, guessed dummy depth, driver/module patch, unknown runtime retry or game whitelist is introduced.

## Deferred matched-depth experiment: precise acceptance gate

Before adding a Depth→N runtime option, reuse the existing immutable descriptor, exact-fence, resource-budget and same-device mechanisms. Hardware depth's typed view and RR linear-to-hardware conversion are different contracts. Do not just set `linear_depth=true` to reuse RR allocation: that would change projection semantics. Per-layer N, DRS/subrect, unknown projection and device generation must be in the plan.

The first GPU fixture should compare point-matched Depth+MV at N with the unchanged path using a controlled moving foreground/background discontinuity, normal/reversed depth and nonzero atlas origins. No min/max rule is selected without the depth convention. Then compare actual model/game output at fixed R/N on both routes. Failure must retain the original path without stale proxies or hidden CPU waits. A default change requires reproducible visual benefit and cost, not just successful Create/Evaluate or an external author's report.

## Verification status

Implementation-time base-function tests and the three inverse controls passed on Windows; the pre-fix source failed the zero-grid and converted-scale assertions. Generated-source, full-core compilation and capture regression results are recorded in the accompanying PR after execution. **No game/NGX success, performance benefit, completed hosted CI or new product release is asserted by this document.**
