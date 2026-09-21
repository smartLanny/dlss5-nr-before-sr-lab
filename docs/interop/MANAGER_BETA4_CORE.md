# Manager Beta4 Core adapter

## Identity and integration

Base: cumulative unified `303b2058411972c333b393001bc72ad5b6f95314`.
Reference ABI/consumer: `619876803c20355597302ae1e376e671f57cad58`.
Manager: `211a85570bdf5ef610c4104d345d290459013d52`, version 0.5.0-beta.4.
This change does not modify Manager, replace the cumulative picture pipeline,
install into a game, or publish a release.

The reference is a D15-era implementation. Only its external ABI/admission and
consumer adapter are ported; current five-layer, work-scale, source-claim and
retirement implementations remain authoritative. External sources are recorded
as external owners, never fabricated native results or native guide provenance.

## Interfaces and component boundary

| Component | Interface | Current acceptance boundary |
|---|---|---|
| Native D3D12 / existing DX11 carrier | NGX D3D12 Feature1 | Existing route; current adapter regression required |
| Bridge 1.4.13-pre8 official | NGX D3D12 Feature1 | Manual candidate, not a provider-v1 component; specific Bridge run pending |
| Feeder stable external OF r3, 0.15.1 | NRExternalProviderV1 | New Core adapter; real component/GPU acceptance pending |
| Vulkan external D15 r3 | NRExternalProviderV1 | Transport owns Vulkan-to-D3D12 conversion; acceptance pending |
| Legacy host D16 r3, 0.15.1 | NRExternalProviderV1 | Host/relay owns conversion and IPC; acceptance pending |

Wire ABI is `0x00010000`, x64: Resource 72, Frame 448, Status 56,
SubmitResult 96 bytes. Exports are QueryV1, ClaimOwnershipV1,
ReleaseOwnershipV1 and SubmitFrameV1, all with the `NRExternalProvider_` prefix.
Native-observer query is bit 2; it does not grant NR ownership.

The four Beta4 component inventories were verified against all 78 file sizes
and SHA-256 values. This establishes artifact identity, not runtime ABI success.
The Feeder contracts additionally require same-frame-output,
source-frame-claims, external-exact-fence-completion and
present-color-depth-motion; Vulkan also requests multi-pass-nr. Do not advertise
those routes as accepted from a manifest name or CPU fixture alone.

## Synchronization and lifetime

- Producer owns list execution, Signal and resource lifetime through its exact
  declared fence. Submit returning success only means commands were recorded.
- Require a DIRECT list/queue, same-device typed whole textures, declared states
  restored unchanged, required depth/motion, and a future non-removed fence.
- Queue training is limited to the exact declared live list/queue. Unrelated
  batches do not enter the old global Feeder training/QI scan.
- Source identity includes provider, view, epoch and frame. Duplicate recorded
  content cannot run twice. Resize/epoch changes require ownership release and
  reacquisition after pending completion; dimensions are independently checked
  by the current transport and multi-layer consumer.
- Claim excludes live native SR/RR/FG owners and unfinished prior Core work.
  Native inline, FG and generic Present paths decline while external ownership
  is held. Present rechecks before recording, including previously prepared work.
- Release refuses pending external receipts. Current Core retirement checks all
  primary/auxiliary and pending-destroy contexts before freeing transport images.
  Missing submission/fence completion remains blocked; it is never fabricated.
- Non-unity external exposure or an exposure texture is explicitly unsupported
  until a real exposure consumer is adapted. The old reference ignored these
  fields, which is not a usable colour contract.

## Validation and delivery gate

Keep separate receipts for CPU, generated Windows Core compilation, GPU,
real signed Feature18, actual packaged Bridge/Feeder, and real games.
Exercise native/Bridge/Feeder ownership, one-to-five layers, scale/dimension
changes, master OFF/resume, release/exit, missing Signal and device removal.
CPU policy or unchanged shader hashes do not clear the component/game gates.

The handback must be a **complete first-install directory**, not an OTA:
both language Core variants, same-source nrchain, the matched carrier where
validated, complete face runtime/models/licenses, appropriate loader/runtime
and the selected verified transport. Include exact source identity, capability
status, byte sizes and SHA-256 for every file; re-extract and verify the package.
Do not claim an unbuilt carrier or omit required companion files.

Ship the complete current `config/nr_before_sr.ini` plus its generated-source
parsing contract. Core uses Windows profile API semantics, not first-text-section
selection: duplicate/BOM-containing files must be validated through that API.
Preserve existing INI on update; do not silently replace saved choices. Document
primary plus NR2–NR5 settings, the single chain WorkingScale, defaults/clamps,
colour-policy precedence, and the provider's separate configuration. New/missing
energy modes remain OFF. No new Core INI key is introduced by this adapter.

Until the complete build, component and packaging gates pass, this is an adapter
source candidate. Beta4 should retain its current working payload and mark the
new unverified routes as pending adaptation.

### Initial local checks

Production source `0a4f2e0151cd02060322757e6377ff469e2b85e4` completed the
canonical colour/unified export; all 256 source-manifest file hashes matched.
Generated Core compiled to a real C++20 COFF object with FaceBackend=1, and all
four external-provider export directives were present. This is not a linked
DLL or full bilingual build. Compiler output retained eight existing warnings.

`scripts/validate-external-provider-core.py --compiler <Windows clang++> --output <new-directory>`
runs the ABI/ownership test plus the **actual extracted** pending-completion
and exact-training consumers, source registry (407 checks), and actual Present
admission/pre-record consumer (91 checks). Fence/COM objects in these tests are
CPU fixtures. They prove incomplete-fence retention, exact completion,
device-removal handling, duplicate rejection, and unrelated-list exclusion,
not a real component or GPU queue submission.

The existing uniform-layer INI/Feature18-tuning host passed 29 checks. The four
generated user-control tests, including real Windows profile API load/save,
passed after supplying `DLSS5_TEST_CXX`; the original generation log's three
compiler-unavailable skips are preserved and superseded only by that rerun.

Next runtime entry: reuse the actual signed-runtime FG/compat host with
`NRExternalProvider_ClaimOwnershipV1` / `SubmitFrameV1` / exact Execute+Signal /
Query / Release, then repeat using each pinned component itself. A controlled
host's synthetic guides do not certify the component's guide extraction,
Vulkan/IPC synchronization, motion semantics, or game output. No GPU run was
performed for this adapter batch.

## Complete component staging for Beta4

`scripts/stage-manager-core-component.py` consumes a completed canonical build
and the two packages already accepted by `package-multi-api-candidate.ps1`.
It does not bypass that packager's validation or manufacture a validation file.

```text
python scripts/stage-manager-core-component.py
  --build-root <complete-build-root>
  --packages-root <verified-bilingual-package-root>
  --manager-components <Beta4-components-manifest.json>
  --manager-source <exact-Beta4-source-checkout>
  --output <new-component-directory>
```

Each language directory contains its Core, same-source nrchain, matched carrier,
the exact generated first-install INI, seven pinned face files, licenses and
sanitized build/configuration receipts. The root `manager-core-handoff.json`
records every delivered file's byte size and SHA-256. Source manifest, binary
receipt, face build receipt, fixed face assets, input package inventory and all
copied outputs are independently checked. Duplicate file identities, unsafe
paths, modified assets, incomplete face packages and duplicate INI templates
are rejected. Existing user INI files are never parsed or overwritten by this
tool; their actual parsing authority remains the Core Windows profile API.

The result is a complete **Core component**, with explicit Manager-owned
ReShade and RTX-family NVIDIA runtime dependencies. It is not a standalone
installer. The carrier is included for the selected compatible route; it must
not be installed automatically into native D3D12 games. Provider-v1 exports
are recorded only if present in the actual x64 PE. External route automatic
enablement stays false pending actual component/GPU/game validation.

The tool executes the exact Beta4 identity and reset code read-only. Beta4
`211a8557` recognizes the uniform settings identity through its two fixed
unified3 Core hashes; a newly built Core requires an identity registration.
Its current field contract does not expose `ColourLabMode`, and its historical
reset defaults cannot stand in for current generated colour defaults. The
handoff includes the actual detected identity, pristine template values and
reset differences, with `managerEditingReady=false`. The Manager owner must
import the new identities and updated settings contract before full editing.
No Manager files are modified by this adapter or staging tool.

The staging tool was exercised against the existing fully validated bilingual
unified3 build: both component inventories were re-read and verified. That
reference validates packaging mechanics, not a new adapter binary. The final
unified5 stage must be regenerated from its own completed build and packages.
Seven focused Python input-rejection tests pass; they make no runtime claims.

## Real provider ABI GPU host (2026-09-21)

The independent `validate-external-provider-ngx.py` runner compiles the entire
frozen Core into a host, with its actual linked objects and precompiled shaders.
It verifies the complete source manifest, full-face build receipt, artifact
hashes and current NVIDIA Authenticode signature before running. It supplies
synthetic colour/depth/motion through the public provider ABI; it does not
replace a production runtime function or invoke a packaged Feeder.

```text
python scripts/validate-external-provider-ngx.py --core <frozen-built-core>
  --source-commit <exact-manifest-commit> --compiler <Windows-clang++>
  --reshade <pinned-ReShade-source> --nr-runtime <signed-nvngx_dlssnr.dll>
  --output <new-private-evidence-directory>
```

Frozen source `9ff3555a7bfdedd3389731bdb349224223b8f35f` passed on an actual
RTX 5090 D. The source manifest SHA-256 was
`6716aa6c4c43f364ec34c9704882eeb01787d95b452c771f0b82788286b99c4d`;
all 256 source files matched the canonical source before and after the run.
The host used a separately built, verified full-face copy of that exact source.
Its seven linked object hashes and 26 shader hashes are retained privately;
this is a same-source host test, not a claim that it loaded the eventual
packaged Core DLL or completed the full canonical release gates.

- One layer: 378 checks; five real primary model evaluations, six completed
  external receipts and three changed output frames.
- Two layers: 463 checks; seven primary and three secondary evaluations using
  distinct real Feature18 handles, eight completed external receipts and three
  changed frames on which the secondary actually evaluated.
- Both cases exercised Claim, exact-list queue training, Submit, real Execute
  and Signal, Query and Release. A separate real queue-wait fence deliberately
  held submitted commands incomplete: Release was refused and model/transport
  resources remained retained until the producer's declared completion fence.
  A scope guard opens that test gate on exceptions; completion waits are bounded.
- Duplicate frames were refused. OFF admitted only identity, added no model
  evaluations, preserved every output pixel and allowed completed retirement.

The passing validation receipt SHA-256 is
`acca8d63f095c896ba7c7d03aa4b2297a4c927d6364c95bf50f6dfb38602098b`.
Earlier signature-environment failures and a failed two-layer fixture remain
preserved. The latter only wrote in-memory settings; the actual ABI's
SnapshotConfig reloaded missing INI defaults to one layer. The corrected host
uses the real SaveConfig/ReloadConfig path and asserts the requested layer
count after every Submit. No production change or weakened assertion was used.

This validates the bounded current external consumer, not real game quality,
native SR/FG, Bridge, Vulkan/IPC, or actual packaged Feeder guide extraction.
Non-unity exposure remains explicitly unsupported by the current Core ABI
consumer. Per-component and game validation are still required.
