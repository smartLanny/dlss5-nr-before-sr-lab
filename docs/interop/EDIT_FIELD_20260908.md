# #11 low-grid luminance EditField: first production slice

This separately authorized branch starts at the completed dev5 handoff
`bd2b3b1f83a9990daa1f62033c2545d76ccb4fef`. Its current production source is
`6062a0be77be561d1936c204d975ca4890f21453`, beta0.5-dev6 / PE 0.5.0.6.
Destination remains `next/0.5`. Existing dev5 DLLs and the completed #169 raw
evidence remain in their separate worktree, unchanged.

## Actual implemented path

`LowGridEditField=0` defaults to composition A. The optional B path applies only
to Mode2 and requested ColorStrength <= 1e-6. Nonzero colour requests retain A;
the experiment does not silently remove colour following. Master zero,
identity mode and warmup do not allocate/record this optional work.

Each eligible slot can own a typed R32G32_FLOAT texture at actual N extent.
BuildEditFieldCS reads the existing Proxy/Neural stored values and reuses their
current decode functions; finite values produce
`(clamp(log2(max(Yn,1/4096)/max(Yp,1/4096)),-8,8),1)`.
Invalid stored samples produce `(0,0)`; identity produces `(0,1)`.
The original input kernel and model call remain unchanged.

After successful same-frame NR and the existing neural UAV->SRV transition,
the optional pass records N-sized field writes, a real UAV barrier and its
SRV transition. CombineV2CS bilinearly restores the signed field using the same
pixel-centre mapping/clamped 2x2 footprint. Every validity member must equal 1,
including zero-weight neighbours; there is no valid-weight renormalization.
Invalid footprints retain the stored source. Existing per-pixel zero-model-edit
and finite checks remain before the new field branch.

Only the raw log-luminance term changes. DetailDecision, LightingLock and its
source-relative bounded attenuation, original-source near-black/EdgeGuard,
matched fullProxy/highlight energy, strength/layer/extra-boost budgets, alpha,
subrect and final gamut/storage guards stay in final full-resolution composition.
F7/F8 constants carry actual recorded flag 8192, and the existing detail plane
uses the selected raw edit. INI/config-generation/load/apply/UI wiring is real.

`U(log(N/P))` is not `log(U(N)/U(P))`; edge differences at 1x are expected and
must be measured. This first slice retains original full-resolution sampling
needed by safety, protections and diagnostics. No net speedup is claimed.
No Catmull/Lanczos, colour EditField, face mask, repeated NR, alternate input
kernel or temporal history was added.

## Resource and failure bounds

- Allocation requires the existing `CanMutateSlotLocked` fence/recorded-list
  gate before any new descriptor is written. Already-owned fields follow the
  same slot usage and retirement as existing neural/result resources.
- Driver-reported R32G32_FLOAT ShaderLoad/typed UAV store support is required.
  `GetResourceAllocationInfo.SizeInBytes` is limited to 64 MiB per field and
  256 MiB total owned field allocation per Core, including allocation alignment.
  This is the field-texture quota, not a promise about all driver/PSO memory.
- No new unbounded slot/context pool exists. On allocation/budget/format failure
  the slot retains A until its normal recreation. Optional PSO failure does not
  poison the base codec. A busy slot waits for safe mutation while using A.
- Turning the option off stops the optional pass; already owned storage remains
  bounded until normal slot retirement. Quarantined contexts retain both their
  dependencies and quota; no early free is introduced.
- The main descriptor layout expands to six SRVs/six UAVs, with 35 root constants
  unchanged. Production shared descriptor writers and the fixture are updated,
  including valid typed-null descriptors when no field exists. Old source,
  motion and exposure typed-view transformations remain present in generated code.

## Minimum verification completed

The original source pins/required ancestry passed clean generation. All original
manifest-listed generated files were independently hashed. Generated Core C++
frontend compile and all seven production SM5 entries passed. The 64-pair actual
production A/B fixture compiled with `-CompileOnly`; no GPU device was opened.
The source wiring contract checker passed. This is not a linked DLL or a WARP,
NVIDIA, game, performance, Reset/DRS or allocation-failure runtime result.

An initial direct canonical frontend attempt failed on the known missing
build-time `nr_post_settings` transformation; the actual generated source
compiled successfully. Both logs are retained privately. Existing ReShade
attribute/exposure warnings and the new compiler's redundant-isfinite warning
are retained rather than suppressed.

Generated Core SHA256:
`eeed38d1f369da4735bf5232762d1beb9b6ba953866d4939781b920c110356b9`.
Source-manifest SHA256:
`70b69dba816d906d6ec474b875b0e608142468bb110a82233f2ea95f5875dbf9`.
Compile-only fixture executable SHA256:
`6f028105e7292da425feed04aa703121686c0cc9800e0b8aefad05937a703f1a`.
The private `build/editfield-minimal-validation/validation.json` binds the
runtime source, logs and exact checked artifacts; later docs are separate.

## Coordinated next GPU action

The prepared fixture compares the same raw Original/Proxy/Neural at N=13x13 and
7x5, eight fixtures and strengths 0/1/2/4, with colour/protections off for the
first attributable A/B. It preserves actual R32G32_FLOAT edit/valid data after
the real completion fence, plus the original/raw-N/two outputs within 4 MiB.
Python independently reconstructs every raw-N log and validity value, sizes,
source hashes, finite/alpha/identity boundaries, same-grid tolerance and actual
noninteger reconstruction differences. Cases include identity, invalid neural,
structured edges, bright/dark edits, explicit linear HDR and near-black.

After coordination releases the GPU, run once with a new output directory:

```powershell
./scripts/validate-codec-gpu.ps1 -ToolchainRoot $Toolchain `
  -SourceRoot "$Generated/core" -EditFieldOutput $NewCaptureDirectory
```

No A/B execution has happened at this checkpoint. Further required work includes
runtime allocation/PSO/slot-reuse/Reset/DRS fault controls, unchanged-source A
regression evidence, protection-on ablations, actual GPU timing including the
additional pass and bandwidth, and genuine NVIDIA/game sequences. #85/#86
follow only a measured retain/reject decision; #84 new inputs require a new
model run and cannot reuse this fixture's Neural as purported new-kernel output.
