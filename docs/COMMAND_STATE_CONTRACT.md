# r4 native command-state envelope

The RE9 investigation identified a crash in the game's next compute root-table
setter after a successful Feature 18 evaluation, including the warmup return.
This change preserves the game's D3D12 compute bindings around the additional
NR and codec commands. It changes no image-processing parameter or shader.

## Boundary and identity

- NR→SR snapshots before plugin work and restores **before** native SR. An
  inactive substitution still passes through restoration: failed creation,
  evaluation, identity output and warmup can all have recorded commands.
- SR→NR takes a new snapshot **after native SR** and restores that exit state.
  If it cannot snapshot and SR used an intermediate Output, native SR is run
  against the restored original Output; no unsafe plugin copy is attempted.
- Extra NR/codec commands and restoration use the registered **native** command
  list. Native SR retains the caller's original interface. This leaves ReShade's
  private wrapper caches untouched by the plugin and never feeds native heaps
  to a wrapper expecting ReShade heap proxies.
- ReShade object discovery provides `get_native()`. An independent private-data
  token joins that identity to the NGX carrier, including a caller-side wrapper.
  Destruction removes the record; native-address reuse cannot inherit its state.
- Only DIRECT lists are admitted. COMPUTE/COPY lists receive no plugin restore
  calls. Distinct CL4/CL10 tear-off interfaces are currently refused.

## Recorded and restored state

Native setter hooks record the active ordinary or DXR PSO, compute and graphics
root signatures, both shader-visible heaps, and both stages' descriptor tables,
partial 32-bit constants and root CBV/SRV/UAV addresses. Changing a root signature
clears only that stage's arguments; setting the same signature preserves them.
Changing the heap set clears tables in both stages while preserving constants
and root views. Reordering the same heap set is not a change.

Restoration sets heaps, signatures, valid arguments in both stages, then the
active PSO. Current records and frozen snapshots hold COM references to PSOs,
root signatures and heaps. The snapshot is allocated with `nothrow` on the heap;
allocation failure passes through to native SR. Whole-state aggregate temporaries
are avoided because some game workers reserve only 64 KiB of stack.

Setter hook publication is bounded by four native implementations per method,
pins each target module, and uses the same serialized queued MinHook publication
lock as the existing NGX observer. Publication failure does not disable an
already-observed implementation. Existing exact-fence retirement and successful
native Reset lifetime handling remain in place.

## Unknown state and recovery

Installing hooks during an existing recording does not establish its earlier
state. The first affected carrier remains native; a later successful native
Reset or ClearState establishes a baseline. A failed Reset never does so.
An unbound PSO is not restored through an illegal `SetPipelineState(nullptr)`.

Device creation observes command-signature descriptions. ExecuteIndirect clears
only the observed signature's affected root constants/views, as D3D12 specifies.
An old or otherwise unknown signature makes the recording untrusted until Reset.
ExecuteBundle and CL10 SetProgram similarly invalidate the recording; work-graph
program state is deliberately not reconstructed. CL10 availability itself is not
a reason to bypass NR. The SetProgram IID, slot 84 and pointer ABI are checked
against the local Microsoft Agility SDK 1.714.0-preview header/IDL.

Each device's enumerated meta-command initialization/execution dirty masks must
fit `0x1038`: descriptor heaps, compute/graphics root signatures and PSO. A mask
outside that coverage, enumeration failure or unavailable metadata prevents
additional plugin work. InitializeMetaCommand/ExecuteMetaCommand mark the covered
domains dirty. Such an SR exit cannot be snapshotted until its actual native
setters re-establish those domains. A same-pointer root rebind after meta dirtiness
clears old arguments, while an ordinary same-pointer rebind preserves them.

This is a compute-state envelope with the graphics root/table preservation needed
after heap changes. It does **not** claim arbitrary opaque NGX commands preserve
all IA, rasterizer, output-merger, predication or work-graph state. The inspected
RTX 5090 Laptop device enumerated 16 meta commands, all with init/execute mask
`0x1020` (compute root signature and PSO). Runtime behavior and future driver/model
changes still require game validation.

## Validation at the first r4 candidate

- Full production module compiled into a native D3D12 WARP/MinHook harness:
  30 checks passed, including late instrumentation/Reset recovery, both heap
  stages, partial constants, failed hook publication, indirect metadata,
  strict wrapper-heap separation, COMPUTE refusal, meta dirty/rebind behavior,
  SetProgram invalidation and native-address reuse.
- A 64 KiB worker ran two simultaneous scopes repeatedly. Compiler stack-use
  reports cover 107 command-state functions, each at most 4096 bytes.
- The production restore model ran in 12 WARP GPU scenarios: four native
  controls and eight restoration cases. Actual compute, graphics draw and
  plugin outputs were read back. After restoration the simulated game updates
  only root-table index 2 before dispatch. A deliberately corrupted restoration
  negative control was rejected by the independent output checks.
- D3D12 debug-layer availability was not claimed: it is absent on this machine.
- English/Chinese/bridge builds passed. The frozen first candidate has Chinese
  SHA-256 `BFF74C6E4254C95B1AB1CF2DCB7C7684BD6A760665CA5CA20BAE8C229594F9F5`.
  Game crash recovery and continued visible-transfer progress are separate live
  acceptance gates, not established by these offline checks.

Primary contracts: [root signatures](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-a-root-signature),
[descriptor heaps](https://learn.microsoft.com/en-us/windows/win32/direct3d12/setting-descriptor-heaps),
[indirect drawing](https://learn.microsoft.com/en-us/windows/win32/direct3d12/indirect-drawing),
[meta-command dirty state](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_meta_command_desc).

## Indirect-signature followup candidate

The BFF candidate's RE9 test remained in `unknown-indirect-signature` bypass:
1800 Mode 2 carrier requests produced zero Feature 18 evaluations. This was not
a compatibility pass. The original diagnostic did not distinguish an unseen
signature, private-data failure and a rejected newer argument type.

The next candidate logs bounded CreateCommandSignature observation counts,
successful objects, metadata write results and invalid-description counts. On
unknown use it logs the actual GetPrivateData HRESULT, returned/expected byte
counts, metadata validity, argument count, selected stage and all bounded type
IDs. Missing metadata and genuinely unknown types continue to refuse NR.

The classifier explicitly supports Microsoft's defined types 9, 10 and 11:

| Type | State interpretation |
| --- | --- |
| DispatchRays (9) | Terminal operation using compute/global root arguments. |
| DispatchMesh (10) | Terminal operation using graphics root arguments. |
| IncrementingConstant (11) | Clears exactly one destination DWORD after ExecuteIndirect, in the terminal's stage. |

One recognized terminal operation must be last. Ordinary constants and root
views are cleared only in their specified ranges/stage; other arguments, the
other stage, PSO, roots and heaps are retained. Type 11 does not have a third
`Num32BitValuesToSet` field. Compile-time ABI checks verify the older MinGW
structure's size/alignment and matching first-two-field offsets; the count is
explicitly one. An already-successful native creation establishes device
support, so observation does not invent a new feature-support requirement.

The followup native harness passes 46 checks, including Rays/Mesh stage mapping,
pure terminals, unknown-type rejection, side preservation and a type-11 test
whose third union DWORD deliberately contains garbage. GPU restoration remains
12 scenarios passed. Candidate Chinese SHA-256 is
`2869D7EE12BCB73C4FADC4E2673A7BF3290FD2D90A294B2437E64B72C1BDD9E8`.
These API contracts are verified. The subsequent RE9 run passed both active
NR orders and normal exit; its first 16 signature diagnostics contain only
types 0 and 1. The actual signature that blocked BFF therefore remains
unidentified and is not inferred from the API support change. See the
[live acceptance record](history/0.3-0.4-and-early-0.5/RE9_COMMAND_STATE_INVESTIGATION.md#local-acceptance-addendum-candidate-2869).

Sources: [DXR indirect signatures](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#createcommandsignature),
[mesh shaders](https://microsoft.github.io/DirectX-Specs/d3d/MeshShader.html),
[incrementing constants](https://microsoft.github.io/DirectX-Specs/d3d/IndirectDrawing.html#incrementing-constant).
