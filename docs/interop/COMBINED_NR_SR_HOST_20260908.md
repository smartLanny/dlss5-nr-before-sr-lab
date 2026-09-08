# Controlled production Core NR to direct DLSS SR

Two independent NVIDIA host runs completed the combined route at
R/N=640x360 and O=1280x720. NR-off completed eight SR evaluations; its first
three output hashes exactly matched the separate B SR-only baseline. NR-on
completed eight SR evaluations, with five frames consuming this project's
actual NR/composited output after queue training and NR warmup.

This is a synthetic renderer and test-only ReShade registration host. It does
not simulate game callbacks, Present or ImGui calls. NGX initialization,
Feature18, SR Feature1, command queues, completion fences and readback are real.
No product DLL, ABI or lifecycle protection was changed for the experiment.
This proves a controlled combined GPU path, not FSR dispatch interception,
full Feeder game integration, temporal game quality or FG presentation.

## Exact source composition

- Combined host: `46c3b5e883bd196190ce2c6b750bf3aaeebf75a5`.
- Production Core: `53fb7fb5bfee49bf37bfc7d50a45f62a54182088`, unchanged from
  the separate external NR hardware experiment. DLL SHA256:
  `081c61f293710face535f0cdcf9011cdc257e6d319595dfb99ae2fb064627a18`.
- B production mapping snapshot SHA256:
  `230614cf224e6161905ea16e006bec023ebcac1cc51bc425aad5ce89c572bc95`.
- B fixture snapshot SHA256:
  `4b7f6880f8424d2b1f064fe1910a06fee49352bc3bbe5a5cb0bb32989f244486`.
  These exact files were copied read-only into the private evidence snapshot
  before compilation. Their later repository commit does not alter this
  original byte identity.
- SDKs remain fixed at AMD FidelityFX `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`
  and NVIDIA DLSS `a291cc7d2cc642a51566f3dfd5376f635cd1b284`.
- SR runtime SHA256:
  `be6e434a94ca32499515eb62ca0e6c274526055d568d0426e4c652dcdfb6ee6e`;
  NR runtime SHA256:
  `6eb209e764f39872625debd6abaf45e2bb6322f6f270f781f70c059ae30b3927`.
  Vendor binaries and raw logs remain private; none are source attachments.

## Observed pipeline and ownership

The renderer uploads FP16 linear HDR color, FP32 hardware depth, zero RG16F
motion, and 1.0 exposure. Jitter is zero because this static renderer does not
jitter its input image. B's production mapper checks these typed resources,
states, active dimensions and actual NGX quality range.

The host performs official NGX Init/capability query, loads the production Core,
waits for its actual native observer startup, Query/Claims the external owner,
and only then creates the SR Feature1. The Core trace records one successful
Feature1 contract at 640x360 to 1280x720, quality 0, flags 11. Both core and
wrapper Create entry logs represent the same nested call and same handle;
the contract is recorded once. `observerGap=0` and `observerFault=none` were
observed before Create.

Each source frame calls the production NR Submit once, then the official SR
Evaluate once on the same command list. A successful NR recording selects its
R-size output as SR input, with explicit UAV/read transitions. Training,
warmup and disabled results select the original renderer color. The complete
batch passes its real fence before O readback. The external owner remains
claimed throughout SR Evaluate, so the frozen Core's existing early and locked
owner checks bypass native Feature1 NR processing. The NR-on trace creates
one Feature18 instance for the external context and no second native NR
instance; the initial NR evaluation counters align with the warmup and first
composited frame. The host does not claim an additional exported total-driver
evaluation counter that does not exist.

| Zero-based frame | NR-off | NR-on | SR |
| --- | --- | --- | --- |
| 0 | Disabled, no NR recording | Queue-affinity training, original color | Success, complete, read back |
| 1–2 | Disabled, no NR recording | Real NR warmup, original color retained | Success, complete, read back |
| 3–7 | Disabled, no NR recording | Real NR/compositing recorded, consumed by SR | Success, complete, read back |

All sixteen O readbacks contain 3,686,400 finite FP16 channels with spatially
varying RGB. NR-off's first three output hashes are
5324495337928740927 / 10969640350692307865 / 16404233325737454114, exactly the
same as B's `direct-sr-gpu-20260908-final2` SR-only run. NR-on's five combined
output hashes are 1420279513407584453 / 12412663188803251572 /
7826444518598743562 / 2673495806138528168 / 13158955251635925957.
Changes to artificial images are execution evidence, not a visual-quality
recommendation.

SR release, parameter/capability destruction and official shutdown all returned
success in both runs. The external provider was released after SR. The Core
retirement trace still uses its existing deferred-resource mechanism; this
short process is not a long-duration unload/rebuild proof. Both processes exited
and were checked absent. No game files, main branch or release were modified.

## Preserved negative control and integration gap

The first NR-off attempt created SR inside the Core's asynchronous hook-install
window. Query(force) returned Starting/epoch=0, but its internal module poll
returns early until native hooks are ready. Two SR frames completed, then the
real unknown-handle guard rejected further NR admission as observer-uncertain.
The process exited with code 2. This evidence was preserved.

The test-host-only fix waits for this run's actual `hooks active` trace in a
fresh directory before Query/Claim/Create. It does not turn off or rewrite a
guard. A production combined host needs an explicit reliable native-observer
readiness contract; the feeder-only four-export Query currently cannot be
assumed to provide it. The successful retry is not proof this product startup
gap is solved. NR-off/on are separate startup runs, not a live F6 toggle test.

## Reproduction

Build `tests/multi_api/combined_nr_sr_gpu.cpp` with MSVC and the frozen B test
directory on the include path; it reuses `direct_sr_gpu.cpp` helpers and the
production mapping without duplicating their implementation. Use the same
official SDK includes/library as B and this repository's provider ABI header.
Prepare a new private directory per scenario containing the frozen Core,
matching nrchain, NR runtime and SR runtime. Set Enabled=0 or 1, Mode=2,
WorkMode=0, TransferStrength=1.0 in its local configuration. Run
`combined_nr_sr_gpu <absolute-core-path> <runtime-directory> off|on`.

The private `combined-nr-sr-20260908` archive contains source snapshots, compiler
output, the initial `nr-off` rejected run, and `observed-off` / `observed-on`
successful runs. It is a source/fixture experiment, not a player package.
