# D21 colour/texture branch — execution milestones

## Frozen baseline and delivery state

- Original D21: `6c3a26fb610fb0b4d3de661fe7db25479a0a24f9`.
- Selected D21 development baseline: #279 uniform3, `58ce637c1fbb03d0e7d93b90d26b3e4bce7b03df`.
- Branch: `codex/d21-colour-texture-lab-20260917`.
- M1 candidate: `beta0.5-dline21-colour1`.
- CPU #280/lockperf changes are NOT included. Models, layer count, uniform working raster, LightingLock, FG, guides, resource access and queue lifetimes are unchanged.
- Source implementation and local host tests exist. Windows Core compile, complete upstream staging, CI and runtime validation are NOT completed at publication. Do not label this a tested installable binary.

## M1: colour admission and visible A/B controls (implemented)

`ColourLabMode` under `[NRBeforeSR]`:

| Value | Mode | Meaning |
|---|---|---|
| 0 | Legacy | Exact inherited colour-admission policy, including the existing AllowUnverifiedHdrColor request. Other settings are unchanged. |
| 1 | Colour priority (default) | Only the existing UnverifiedHdr *visual* colour restriction is opted into. Same finite/coefficient/amplification guards and same shader math. |
| 2 | Conservative | Unverified HDR remains luminance-only even when the legacy HDR opt-in is saved. Verified colour contracts remain usable. |

Invalid mode IDs normalize to Conservative. No INI is deleted and no existing slider values are overwritten. The candidate's separate mode key defaults to 1 even with an old INI, and the regular SaveConfig transaction persists it. A legacy Core ignores this key.

The normal light/colour group gains a policy selector, warning, one-click Legacy, and requested/effective recorded-coefficient display. It uses the existing pending transaction and source-generation snapshot. An inactive, stale, bypassed or pending record is not presented as a current success. Recorded constants are not GPU completion, presentation, or proof that a model changed colour.

No RGB option is displayed: M1 does not implement RGB restoration or change codec/shader math. No extra NR evaluation, texture, fence wait or per-frame logging is added by this milestone.

### Why a generated-source overlay

The repository deliberately uses a reviewed, pinned integration exporter. M1 adds an explicit `prepare-colour-lab.py` entry which calls the unchanged uniform3 exporter (all pins, clean-tree, ancestry, upstream, and publication checks stay enabled), patches the temporary generated Core with exact one-occurrence anchors, copies two committed module inputs, and recomputes the source manifest before publication. It never edits the checkout or installs a game mod. Any anchor drift aborts.

`prepare-multi-api-beta.py` still produces the original uniform3; use **prepare-colour-lab.py or build-colour-lab.ps1 for this candidate**. The extra manifest section carries the colour1 identity, base and source commit, mode IDs and overlay hashes. The inherited PE numeric version is retained; runtime version/build identity and source manifest distinguish colour1. Do not distribute unlabelled uniform3 outputs as colour1.

### Windows build entry

Use the existing pinned upstream v1.4.12 checkout and the project's existing toolchain environment. From this branch on Windows PowerShell:

```powershell
python .\tests\test_colour_lab.py --repo . -v
.\scripts\build-colour-lab.ps1 -UpstreamRoot '<existing pinned upstream checkout>' -OutputRoot 'D:\ChatGPT\DLSS5\build-colour1'
```

Required environment/parameters are the same as the existing build: `DLSS5_LLVM_MINGW_ROOT`, `DLSS5_RESHADE_ROOT`, optional existing `DLSS5_FACE_RUNTIME_ROOT`, `DLSS5_NGX_SDK_ROOT`, `DLSS5_VCVARS_PATH`, and bridge reuse root. No bash is needed. OutputRoot must not already exist. Face support is inherited from the supplied build inputs, not silently enabled or replaced.

The wrapper invokes the existing staged `core/scripts/build.ps1`; inspect its ordinary dual-language/API outputs and reports before creating the owner test package. Do not copy all add-ons into one game: install only its matching Core, retaining the known-good bridge and fixed NR DLL. No installable package is supplied until a real build succeeds.

### M1 owner acceptance

Use the same scene/settings, initially FG off. Test NR→SR, SR→NR and RR→NR separately; Present is a separate compatibility path.

1. Start with a backup of existing INI. The new candidate defaults to colour priority. Verify the candidate version and actual path.
2. Compare Legacy / Priority / Conservative without changing NR parameters, working dimensions, layers or LightingLock. When old HDR colour was already enabled, Legacy and Priority may legitimately match.
3. In Priority, vary AI colour 0/0.5/1. The expected proof is requested/effective coefficients plus captured output, not that every scene must change dramatically.
4. On internal HDR/PQ input, watch hue, black level, highlights, exposure changes and temporal stability. On abnormal colour select Conservative, collect F8 and file/update the game issue. GPU safety bypasses still require F8 feedback.
5. Check F6 off, resume, reload/save, invalid mode ID, resize, scene/camera cut, mode switches and return to Legacy. No texture allocation or NR history rebuild should be introduced solely by this control.

### Verification presently run

- C++20 portable policy compile/run: 10 static assertions and 402 runtime comparisons.
- Python transform/UI tests: four passing unit cases; actual repository-source test is provided but was not run in the cloud container because the full pinned checkout is not mounted.
- Syntax checks for the Python entry/patch modules.
- Actual GitHub source anchors were inspected, but the complete generated stage and Windows translation unit have NOT been compiled here.

## M2: matched RGB restoration (next implementation node)

Keep old and M1 modes untouched. Add an explicit RGB recovery recipe, shared by pre-SR and post-SR consumers, using matching input/output/model domains and source subrect/jitter. First fuse bilinear difference sampling into the existing final pass; do not default to a full-resolution residual surface. Retain original alpha, identity path, colour amount, LightingLock, faces and gamut/finite guards. Unsupported domains must show a reason and fall back explicitly, never claim the requested recipe ran. Add zero-channel, black, HDR, negative/edit, identity, edge/subrect and resize tests. Compare against the same NR outputs to isolate composition, then compile shaders and collect owner A/B.

## M3: paired input/restore curves

Add source-reference reconstruction and analytic inverse as *paired recipes*, not arbitrary independent curve controls. Mark high-end inverse amplification risk. Reset NR only when input preparation/model history contracts change, not for composition-only switches or normal per-frame exposure. No private RenoDX code/shaders or binaries are committed. Use independent implementations and public mathematical references; do not claim binary-equivalent RenoDX behaviour.

## M4: uniform lifecycle/memory and faces

Forward-integrate only reviewed #279 memory/retirement fixes with identity noted. Keep all layers following the first by default. Verify in-flight peak, failed reconfiguration rollback and retired resource cleanup before more aggressive aliasing. Face-backend delivery and model AutoMask hot-edit regression are separate acceptance items; no new face inference by default in this colour experiment.

## M5: private residual SR/RR and integration

Reuse existing #258/#259/#265 assets, but do not describe uniform3 as completing private residual SR. Maintain same-frame/source pairing and independent history. Combine proven colour and CPU slices on a temporary integration branch early; retain four builds (uniform, +colour, +perf, +both). Textually clean merging is not resource/queue/thread safety validation. Formal merges happen only after the relevant compile and owner game checks.

Related tracking: #274 colour comparison; #109/#103 colour usability; #279 uniform work; #280/#277 performance remain independent. Keep issues open until their real runtime acceptance is met.
