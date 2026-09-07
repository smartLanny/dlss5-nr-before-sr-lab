# Feeder D3D12 recovery — source development only

Status: **not included in the unified player test package; not a supported game route**.

Recovered on 2026-09-07 from the real, uncommitted draft in worktree
`dlss5-nr-before-sr-lab-product-ef4fc8a6`. That worktree was read only. The recovery
branch starts at compatibility integration `289a8e56493e58eef39f976e37118288146957e3`.
The external Feeder source remains pinned to
`26c002d5156d178c2db438327194077c9ad94418` and is copied before transformation.

## Implemented in the recovered slice

- The same-device D3D12 producer has CPU preflight before texture lookup,
  allocator, guide probe and copy work. Disabled/uncertain/unavailable status
  declines. Once delegation succeeds, discovery failure cannot restore private
  NGX NR. Repeated Claim preserves consumer history.
- Provider-only resource readiness no longer requires a private Feature1 handle.
  Resource teardown releases provider ownership and advances a real resource
  epoch. A failed `EndCommands()` cannot lead to NR output copied home.
- Consumer admission and retirement serialize across Submit/Claim/Release.
  Native Feature1/RR prevents Claim; native evaluate also checks external owner
  before work and again under the chain lock. Failed admission restores the
  previous sequence/epoch snapshot instead of inventing a preceding frame ID.
- The consumer uses the existing Feature18 core, codec, NR contexts, post-SR
  transfer, queue observation and fence retirement. The actual native Reset hook
  installer is reused for the producer-owned command list. No fabricated Reset,
  stable-submission count or access-violation catcher was added.
- The initial GPU slice accepts whole, single-mip, single-sample, same-device
  D3D12 textures with FP32 depth and FP16 motion. Unsupported contracts decline
  before barriers. COMMON state (`0`) is preserved as a real state value.

The normal compatibility generators are unchanged. The opt-in
`scripts/prepare-external-provider-recovery.py` emits a new translation unit and
refuses an existing output. It is an independent development seam, not a release
switch. `scripts/patch-feeder-external-nr.py` stages the pinned Feeder and adds its
producer include path; Vulkan/GL/D3D11 frame implementations are unchanged.

## Actual Windows validation

| Check | Result | Scope |
| --- | --- | --- |
| Full core TU with recovered consumer | Compiled and linked | LLVM MinGW Windows, development DLL |
| PE exports | Four provider exports present | Query / Claim / Release / Submit |
| Host admission executable | 46 assertions passed | Ownership, provenance, F6, exact rollback |
| Production producer executable | 23 assertions passed | Real Windows module/export discovery, preflight, sticky delegation |
| Real-source transforms | 2 tests passed | Core and pinned Feeder; repeat rejection; legacy frame preservation |
| WARP transport control | Passed | **Test-only deterministic copy, not Feature18/NR evidence** |
| Feeder `build.bat` | Failed before compile | MSVC C++ toolset absent |
| Full Feeder LLVM compilation, after official SDK fetch | Passed | Real producer TU, MinHook and Windows version resource |
| Full Feeder LLVM link | Failed | Official NGX static library requires missing MSVC C++ runtime import libraries |

The first LLVM attempt lacked `nvsdk_ngx.h`. A read-only search found no installed
SDK, so actual build dependencies were then fetched from official repositories:
[NVIDIA DLSS](https://github.com/NVIDIA/DLSS/tree/a291cc7d2cc642a51566f3dfd5376f635cd1b284)
at `a291cc7d2cc642a51566f3dfd5376f635cd1b284`, and
[Khronos Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers/tree/ee2ec5fd83dafce291024683b50dc89219333076)
at `ee2ec5fd83dafce291024683b50dc89219333076`. Their source pins and NGX file hashes
are recorded in `FEEDER_BUILD_DEPENDENCIES.json`; no runtime/model DLLs were fetched.

Two standard C++ `GetProcAddress` pointer casts were required in the staged
upstream hook header. Full source compilation then passed. The remaining link
failure is substantive: the official library declares `_MSC_VER=1900` and
`RuntimeLibrary=MD_DynamicRelease`, requests `msvcprt`/`OLDNAMES`, and contains
references to `__CxxFrameHandler3`, `_CxxThrowException`, `_Init_thread_*`, and
MSVC-decorated `std::_Xbad_alloc`/`std::_Xlength_error`. The current LLVM-MinGW
toolchain does not supply the required MSVC C++ import library. No empty library,
fake header, renamed runtime or exception shim was substituted to force a pass.
The development consumer DLL is not a player artifact.

`scripts/build-feeder-recovery-llvm.ps1` reproduces the actual full producer
compile/link with a prepared stage, the existing toolchain and a new output
directory. It fails on a link error and retains both compile and link logs.

Evidence is saved under
the private `feeder-recovery-20260907` evidence archive:
`core-compile-final.log`, `core-exports.log`, `external_provider.log`,
`external_provider_client.log`, `transform-tests.log`,
`warp-transport-only.log`, `feeder-msvc-build.log`, and
`feeder-clang-syntax.log`, `ngx-msvc-abi-symbols.log`, `ngx-msvc-directives.log`,
and `full-llvm-attempt/compile.log` / `full-llvm-attempt/link.log`.
`original-draft/` holds 12 original source/test files,
their SHA-256 manifest and the tracked draft diff.

## Required before inclusion

1. Complete the pinned Feeder link with the matching MSVC toolchain/runtime
   libraries, or separately establish a supported SDK/compiler combination.
   Actual official SDK headers and the full LLVM-produced object now exist;
   the final producer binary still does not.
2. Exercise the real producer -> shared Feature18 -> same-frame copy-home route
   on NVIDIA hardware. The test copy does not answer visual correctness,
   temporal warmup or whether all command-state hooks are ready in a game.
3. Fault-test command close, queue Signal failure, teardown/drain timeout,
   resize/device recreation and native/external ownership transitions. In
   particular, upstream Feeder's `DrainGpu()` timeout and unchecked queue Signal
   path still require lifecycle review before this route can be shipped.
4. Connect external provenance, recorded-vs-submitted counters and bypass cause
   to the unified F8 TXT. A Submit result describes recorded commands, not fence
   completion. Existing native F8/game results remain separate.

No Vulkan/Legacy support, second NR implementation, 异环 fix, or player acceptance
is claimed by this recovery. The owner should test the separately delivered
unified native compatibility candidate first; this source branch waits for the
next development instruction.
