# Feeder completion and current MSVC verification

This is an opt-in development slice for #164, based on the complete #166
hotfix source `c6389f7` and the recovered Feeder implementation. It does not
change native DLSS routing, install OptiScaler, publish a player package, or
claim NVIDIA Feature18/game acceptance.

## Fixed production failures

The pinned Feeder `26c002d5156d178c2db438327194077c9ad94418` ignored the HRESULT
from queue Signal, so EndCommands could return success without a retirement
fence. DrainGpu also cleared allocator fences after timeout, allowing teardown
to release resources still in flight.

`17fb033` checks Signal and quarantines the submitted resources on failure.
Drain only clears allocator fences and releases resources/provider ownership
after confirmed fence completion. Resource constructors and Shutdown propagate
a failed drain; session initialization and command-list reuse reject quarantine.
Recreate drains before replacing the old feature handle. A subsequent successful
drain can retire the retained objects; no CPU signal manufactures completion.

The external D3D12 path now waits for its actual submitted fence before allowing
copy-home. Stale event wakes, timeout, event failure and device-removal sentinel
cannot report completion. This bounded wait has a performance cost that still
needs measurement in a game. On an unretired queue, objects remain retained;
runtime unload/device recreation under real driver failure remains unverified.

`07c219c` adds provider owner, provenance, bypass, admission/recording counts and
backend kind to the existing single F8 TXT in the opt-in build. Recording is
explicitly distinguished from submission/completion and visual correctness.
The core snapshot does not invent producer completion counters. Existing F7,
F8 controls and RR fixes are inherited through the hotfix source generator.

## Verified sources and evidence

| Item | Actual source and result |
| --- | --- |
| Full producer MSVC compile/link | `48598dc284f8149cc6cb23ea0f568a95a9135b99`; VS 2022 17.14.39, official pinned SDK; passed |
| Full generated core + consumer/F8 DLL | `07c219c9c3c516f700f0a98c99a26066da919c38`; LLVM Windows compile/link, four exports present |
| Existing native hotfix regression | `48598dc`; 40 local unified Windows/compile/WARP gates passed; does not automatically cover opt-in consumer execution |
| Actual transformed EndCommands/DrainGpu fault harness | 13 assertions passed: Close, Signal, drain failures and eventual retirement |
| Completion helper | 22 assertions passed: scripted fault controls plus real WARP queue/fence/event |
| Existing provider admission | 46 CPU assertions passed |
| Existing producer client | 23 Windows export/discovery/preflight assertions passed |
| Existing transport pixel check | Real WARP deterministic-copy test passed; neural evaluation is a test double |
| Actual-source transform tests | Core/F8, pinned Feeder, repeat rejection, legacy frame preservation passed |

Producer DLL SHA256:
`0bb679b7239f78f5da45ce59780821a1ebf5ff95dade9194cbb61acdced15e9e`.
Core development DLL SHA256:
`013eafce704d0bbc4b666878a560badf13ca572d8740870fe23a9455454677bf`.
The generated core contains source identity `07c219c` / clean; later documentation
commits do not change its source identity. The private evidence archive is named
`provider-transport-20260908`; `msvc-final/validation.json`,
`core-final-validation.json`, and `native-regression/validation.json` separate
these three builds. Compiler outputs, staged dependencies and binaries stay
outside source control.

The initial producer attempt lacked SDK headers; using the real pinned SDK
cache resolved that. Full MSVC linking passed, so the old missing-MSVC/link
blocker is obsolete. An initial direct raw-core compile lacked generated
post-setting integration; compiling the normal hotfix stage plus the opt-in
consumer fixed that without dropping the hotfix. Failure logs are retained.

## Reproduction and remaining integration

Use `patch-feeder-external-nr.py` with the clean pinned Feeder checkout. Populate
the stage with the real dependencies pinned in `FEEDER_BUILD_DEPENDENCIES.json`.
Run `build-feeder-recovery-msvc.ps1 -FeederStageRoot <stage> -VcVarsAll
<VS-vcvarsall.bat> -OutputRoot <new-directory>`. The script checks staged source
and header hashes, native compiler exit status, and records the binary hash.

For the core, first run the existing `prepare-multi-api-beta.py`, then run
`prepare-external-provider-recovery.py` on its generated `core/src/nr_before_sr.cpp`.
Compile that generated TU with the staged source include directory and the
normal core MinHook/ReShade build inputs; preserve source identity definitions
and the Windows version resource. The normal release generator remains opt-in
free and does not silently ship the provider experiment.

Set `NR_FEEDER_SOURCE` and `CXX` to run the three actual-source Python tests in
`test_external_completion` / `test_external_provider_recovery`. Build
`external_completion.cpp` with Windows D3D12/DXGI libraries for the fence test.

The four-export consumer uses the same project Feature18 core, codec, context
history, transfer and fence-retirement implementation. Native/Extracted inputs
can use FrameV1 directly; FeederMap labels its own fallback Synthetic. Supported
contracts currently require same-device D3D12 whole single-mip/single-sample
textures, matching color/output size, FP32 depth and RG16F motion. No complete
FSR/XeSS dispatch interception, SR injection, FG presentation, Vulkan consumer,
or game-wide guide extraction is claimed here.

NVIDIA model execution still needs a real supported host. The DLL's ReShade
registration requirement is preserved; ordinary LoadLibrary alone is not a
host. The next bounded experiment is a clearly labelled host invoking the
production exports, separating host stubs/WARP from actual NGX/GPU results,
followed by a real non-DLSS game with source-frame copyback evidence. #164 stays
open. No main merge, release replacement or game installation was performed.
