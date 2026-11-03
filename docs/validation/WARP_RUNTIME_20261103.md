# Fixed software runtime for the public Windows candidate

## Observed same-program comparison

Run [41571881421](https://github.com/smartLanny/dlss5-nr-before-sr-lab/actions/runs/41571881421)
checked out `7e87582e0faa093b0b97cc838e9b57b46462b204`. The public portable and
both host-compiler jobs passed. Windows PowerShell 5 process/receipt contracts,
the pinned full dependencies, and the packaging contracts passed. The original
post-scaling preflight failed; the workflow therefore remains **failed** and
did not create a Core candidate.

Both native executions used exactly the same EXE bytes:

`e7dfecdce61299866e8806badc94ba3f228d38bf6a4a3ab4c71d61f7e942e6f5`

The source, generated harness, template, arguments (none), and working directory
were checked. The second execution copied that EXE beside the independently
pinned WARP DLL. It did not rebuild the program or change the shader, scenarios,
pixel thresholds, debug-layer policy, or 30-second submitted-work wait.

| Evidence | Original system WARP | Official standalone WARP |
| --- | --- | --- |
| Actual file version | `10.0.20348.5756` | `1.0.22.0.20261027.1` |
| DLL SHA256 | `94d57b2b1a50bb921fb86e0cca1dad488ada6ba88dccedd3dd02c0dc7717ddb3` | `bb8ee56fd0f7ee665abb61178301e612181e5e2f57af0e5ab64e456b8f1a8c4f` |
| Native result | Exit 1, incomplete | Exit 0, complete |
| Scaled-work cases | All 60 individual cases passed before the later failure | All 60 passed, final summary present |
| Lighting/colour work | Case 61 / submission 241 timed out | Completed through case 106 / submission 286 |
| Final device-destruction marker | Absent | Present |
| Receipt `warp_verified` | `false` | `true` |

The original failure preserved `WaitForSingleObject=0x00000102`, target fence
241, and completed fence 240. Its logged `hresult=0` is the fixed argument for
that wait-error record, not a successful `GetDeviceRemovedReason` observation.
The repaired failure path did not unwind resources while work remained pending.

The successful comparison additionally completed the existing production
CombineV2 lighting-edge/reduced-grid checks and CombineCS post-lighting and
colour-strength checks. The observer confirmed exactly one loaded WARP module
at the pinned temporary path, its file version and SHA256, and unchanged system
D3D12/DXGI/compiler module identities. The original receipt's hash remained
unchanged. Neither execution performed NVIDIA inference or game rendering.

Evidence artifact: **13202935331**, 61,105 bytes, SHA256
`a11c30729a25fd286efaf24548566fd16b8c2ffdca65bc55af99e518df5ea857`.
It contains the original failure and the separate successful comparison under
`post-warp-preflight/` and `warp-runtime-comparison/`. GitHub's retention is seven
days; the final handoff must preserve the relevant evidence separately.

This establishes a runtime-dependent outcome for this controlled workload. It
does not identify a particular internal WARP defect, prove an FPS improvement,
or turn the failed run into a successful full-canonical build. Most process
time also includes HLSL compilation; it is not a game cold-start benchmark.

## Subsequent candidate validation

The following source pins Microsoft.Direct3D.WARP **1.0.22** as a CI test
dependency. The official package SHA256 is
`afc058f4d1066edc4231a38f3ab3592b1930f7287d4ac700e6a056e0874bb0be`;
only `build/native/bin/x64/d3d10warp.dll` is extracted. Package and DLL bytes are
checked independently. Vendor files stay in a unique `RUNNER_TEMP` directory,
outside all uploaded candidate/evidence globs.

`prepare-warp-runtime.ps1` records the file identity and helper hash.
`verify-warp-runtime.ps1` establishes a fresh context per GPU step and rehashes
the DLL after the tests. The native test helper explicitly loads the absolute
DLL path before device creation, retains its reference, and enumerates actual
modules after each WARP device is created. A missing, unexpected or duplicate
WARP module, incomplete configuration, or unwritable evidence record fails the
test. Success is silent on stdout/stderr so existing JSON and PowerShell 5
consumers keep their original output contracts.

Native evidence contains the EXE, process ID, host family, actual DLL path, and
the expected hash checked by the parent. It does not pretend the native helper
calculated a file hash. Each step checks all records and its required host
families. CPU-only helpers do not need to load WARP. Explicit hardware/fallback
paths are rejected while the CI runtime context is configured; unconfigured
local tests retain their original behaviour.

The real Windows loader probe covers direct and Python-child launches, the
unconfigured local path, and rejection of preloaded system WARP, missing DLL,
incomplete configuration and missing evidence directory. These checks precede
the expensive full post-scaling preflight. They are loader/device contracts,
not shader correctness tests.

All original canonical gates, face/ORT checks, actual temporal consumer and
negative controls must still complete on the new exact source before packaging.
The same-program diagnostic remains a separate failure-only step and cannot
resume packaging after a failed preflight. No production loader, shader,
default feature setting, package payload or NVIDIA runtime is changed by this
test dependency. The new source's Windows result is pending its own run.

## Primary references

- [Microsoft WARP 1.0.22 package and changelog](https://www.nuget.org/packages/Microsoft.Direct3D.WARP/1.0.22)
- [Microsoft WARP package licence](https://www.nuget.org/packages/Microsoft.Direct3D.WARP/1.0.22/License)
- [LoadLibraryExW](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexw)
- [GetModuleHandleW: same-name module ambiguity](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandlew)
- [SetDllDirectoryW and child processes](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setdlldirectoryw)

The package is used for internal Windows testing/development. Its DLL and NuGet
archive are not redistributed in the Core-only candidate.
