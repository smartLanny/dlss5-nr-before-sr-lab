# #82 remaining small-size admission: CPU planning only

This isolated maintenance slice starts at live #175 head
`e8e5fa06b5517d43388d76a2fcc99628f9c1df5f`. It imports no 0.5 algorithm and
does not change Core, Manager UI, INI defaults or the product's 50–100% range.

## Existing implementation and actual constraints

| Source entry | Current contract |
| --- | --- |
| `ResolveNrWorkExtent` in `src/nr_before_sr.cpp` | WorkMode 0 returns exact R; Custom uses saved scale; output-relative presets require live O; scale clamped to 0.5–1. |
| `ResolveExternalWorkExtent` in `src/interop/nr_external_provider_core.inl` | External renderer has the same scale bound and alignment; missing O preserves full R. |
| `AlignWorkExtent` / `SelectAlignedWorkExtent` | Existing 32-wide / 8-high candidate selection minimizes aspect/area error. Values below the alignment are kept, so these are not model minimum dimensions. |
| `nr_post_compat::Work` and `patch-post-compat.py` | Post requests accept 50–100, round Npost/O, then use the same alignment. 100 retains exact O including odd extents. |
| `nr_precision_ui::Resolve` | 0 means master off with retained precision; 1–49 rejected; 100 selects Reference; editing never implicitly enables F6. |
| `nr_post_settings::PrepareRequestLocked` | Invalid post percent retains prior value and logs rejection; 0 disables and retains precision. |
| `NormalizeConfig` | Historical front CustomWorkScale normalization still clamps below 0.5. Hand-written invalid INI and UI request rejection are different paths; this remaining inconsistency is not silently declared fixed. |

There is no model minimum-width/height capability query in these paths.
`BindNrParameters` supplies equal N input/output dimensions; actual feature
creation/evaluation remains authoritative. A coordinating-task report of a
successful 427×240 NR run disproves a presumed universal 256-pixel height
minimum for that configuration; it cannot establish all smaller sizes or
other runtimes/models. This planner imports no hardware evidence and leaves
the model minimum and model alignment requirement explicitly unknown.

Current front defaults remain WorkMode 1 (Quality) and saved CustomWorkScale
0.75, including INI defaults. Quality is output-relative; that saved 0.75
does not mean the active preset runs at 75% R. `LegacyEstimate` preserves the
distinction until an explicit slider edit. PostWorkPercent defaults to 100.
New-install 100% migration is still a separate unimplemented acceptance item.

The main precision control exposes 50–100. Developer options display sampled
game R/O, not a coherent current R/N/O tuple. This slice does not label those
independent atomics as actual N. A future actual-size display should consume
a synchronized submitted-frame snapshot with route/generation and timestamp,
not infer N from historical inputs or a saved percentage.

## Delivered executable planner

`scripts/plan-nr-size-admission.py` compiles and runs the actual production
alignment functions using the existing extraction parser. It consumes the
actual front/post percent policy headers and checks legal post rounding
against `nr_post_compat::Work`. No new GPU shader or runtime entry is added.

For each requested percent the JSON distinguishes the rounded request,
aligned candidate N, actual horizontal/vertical and pixel percentage, current
percent-policy admission, and unknown model-size admission. Below 50 it
calculates an experimental candidate but explicitly rejects product admission.
It does not substitute a 50% plan. 0 emits no allocation dimensions; 100
bypasses alignment and is exact R/O. A 1–32768 host input bound is explicitly
not a model capability. Output files cannot be overwritten. The report hashes
all relevant source inputs and records its commit/dirty state and zero GPU/NR
calls.

Example, with a cached C++ compiler supplied through `$Compiler`:

```powershell
python scripts/plan-nr-size-admission.py --compiler $Compiler `
  --width 640 --height 360 --route both --self-test `
  --output build/size-admission-plan/plan-640x360-final.json
```

For a fixed 640×360 R, the current algorithm plans:

| Request | Rounded request | Candidate N | Product percent policy |
| ---: | --- | --- | --- |
| 100% | 640×360 | 640×360 | existing |
| 75% | 480×270 | 480×272 | existing |
| 50% | 320×180 | 320×184 | existing |
| 49% | 314×176 | 320×176 | rejected |
| 40% | 256×144 | 256×144 | rejected |
| 25% | 160×90 | 160×88 | rejected |
| 1% | 6×4 | 6×4 | rejected |

The table shows why a rounded request must not be presented as actual N.
For example, the 49% candidate has 50% horizontal and about 48.89% vertical
extent; calling that an executed 49% model would be inaccurate.

## Minimal separate NR experiment proposal; not executed

Use one fixed deterministic finite SDR source and frozen runtime/model/driver
identities. Start with a known working NR 1:1 control. For an initial bounded
size-admission batch, consider only exact candidates 320×184, 320×176,
256×144 and 160×88 from the plan. Generate each model input from that same
source using a documented fixed resampler and retain its bytes/hash. Create a
fresh NR feature/history for each exact N; set NR input/output both to N and
provide explicitly sized depth/MV, exposure and zero-jitter contracts.

Record every Create/Evaluate result, actual bound dimensions, warmup and
transfer counts, queue/fence completion, finite output/alpha and raw hashes.
Use bounded frames/timeouts and one active feature; stop at an allocation,
device or queue failure and preserve the original result. Release resources
only after their actual completion boundary. Do not change a failed request
to another N or reuse another size's model output.

This establishes only per-size NR admission. The product's R→N filter,
guide conversion, full-resolution reconstruction, temporal changes and cost
must be checked separately before opening lower ratios. A few successful
sizes do not justify accepting every intermediate integer extent. A future
dynamic policy needs an explicit runtime/model-scoped admissible set or
authoritative lower/alignment capability, plus a measured input/guide support
bound; on rejection it should retain the prior request and state the actual
reason. There is no reason to guess a universal 256 minimum or to require a
player to test a product range that has not been implemented.

## Local validation

The initial compiled CPU helper could not load its llvm-mingw C++ runtime
(`0xC0000135`); the original `planner.log` is retained privately. The tool now
adds the compiler directory to its own process PATH, without changing the
machine environment. The corrected run completed 170 CPU contract cases:
seven tiny/odd/normal reference extents, both routes, 12 percent cases, exact
427×240 reference and an explicit no-hidden-50%-substitution check. There
were no GPU or NR calls, no Core DLL build, no product parameter change,
and no NVIDIA/game or performance claim.

The final planner source is `4638cf88419181e3ae14370cfe38805fe37dfebb`.
Private `build/size-admission-plan/plan-640x360-final.json` SHA256 is
`cca5427e291e8f136e98e821bdd7d957b6abe589d3be85affc1c25b0ef5b34dd`.
Its dirty field is true because this handoff document was still untracked at
execution; all six hashed planner/production input files were committed and
unchanged. The earlier successful clean-source plan at aeb3a9b is preserved,
as is the initial runtime-loader failure. This is host-planner provenance,
not a Core source-manifest or model validation result.
