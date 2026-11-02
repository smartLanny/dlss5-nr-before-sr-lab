# Minimal 3D development bench: current results

The maintainer reopened bench exploration after the previous official-Sample STOP. This is a small project-owned analytic scene on the already successful native D3D12/NGX host. It uses the complete Core translation unit from `45df68dc80138e89778e544d49f15109ec437a4f` / src tree `7fe3819be240c0fad0eb16526e9d4b3ce0a67f87` and the pinned real Feature18 model. It does not substitute NRD, and it does not claim the addon DLL itself was loaded. Product source, frozen PRs, deployed Beta16, configuration and runtime files are unchanged.

## Supported local v0

The simple local entry runs identical camera/input/seed/jitter sequences at R=1280x720 and O=1920x1080, one NR layer. Capture and measurement use separate fresh processes, each with 120 warmup and 120 observed frames. It exports short videos, common-transform crops, GPU timestamps and process-local memory before creation, during observation and after exact retirement/model/pool release. It preserves raw HDR captures and previous runs. No game is started or stopped.

The four actual arms passed: capture NR off/on, then perf NR off/on. GPU median was 0.849536 / 3.327488 ms; stable process-local memory was 411.65625 / 849.9296875 MiB. Independent image review found slightly smoother/softer NR-on images and no clear overall advantage. These are research low-resolution NR -> one RR results, not the default product RR -> NR route, pure NR cost, game FPS, a 0.4.7 regression diagnosis or flicker acceptance. The 124 memory samples exclude creation/warmup transients, so their maximum is an observed-window sampled peak.

Detailed source, exact identities and measurements: [minimal-scene-r2](scene_bench_review/minimal-scene-r2/README.md).

## Official-Sample initialization

The final bare-native control used the same official-Sample SDK/runtime/device tuple without Core or injection. It still returned NGX `BAD00001` at `HWCheck130`, before capabilities/Create/Eval. This disproves the narrow hypothesis that removing injection alone restores that tuple. The root cause remains unknown. No hardware check was bypassed, driver/SDK was changed or parameter matrix was scanned. That initialization branch remains stopped; its failures are preserved.

## Default product-entry slice

One strict master-off process stopped at the first scene command-state capture. Native bootstrap/capability/optimal lookup had passed, but scene Dispatch and SR/RR/Feature18 Create/Eval were all zero. The native list was registered; the first snapshot lacked previously observed binding/Reset state. The renderer resource observer cannot supply a complete command-state envelope retroactively. NR-on did not run, and no retry or forced observer/work flag followed.

This is unsupported admission in this harness, not a product NR failure or a default-route pass. Earlier research NR-off enables a CPU observer and cannot be relabelled as this strict product-master-off control. No new ownership architecture was introduced to work around the result.

Actual failed stdout SHA256: `91bd8bf8a175a83fdc3099c444471c835987cb10a7909b01005196c5454dd8a4`.

Independent review receipt SHA256: `b9f2c0d9a3dbba552ebf097f7406e441e8d9c78648670ab5cedbd4387cb60708`.

Own-source review subset: [default-post-r2](scene_bench_review/default-post-r2/README.md).

## Remaining route gates

Approximate two-RR effective editing has passed in the separately labelled flat fixture, with raw-guide reuse explicitly approximate. Same-scene two-RR has its own bounded three-frame gate. Neither preparation nor a functional pass establishes a completed product route or quality benefit.

### Same-scene approximate two-RR functional pass

Root executed v1b once: two warmup frames and one observed frame. The child DLAA RR, Feature18 NR and original parent RR each evaluated three times, with five exact recording retirements and four scene producer dispatches. The original scene, seven guide planes, matrices, input alpha=1 and source/runtime tuple were preserved. Raw guides are explicitly approximate for clean color. No production route was enabled.

Observed frame 2 had 920,768 legal pixels, 112 identity pixels and 720 refused boundary pixels out of 921,600. Actual RGB changed at 919,087 pixels (99.727322%). All alpha u16 values and non-edit RGBA bits matched raw, no change was outside the legal domain, and legal values were within one FP16 ULP. Predicted and actual changed masks differed at 1,996 pixels (0.216580%); this is not an exact GPU write-mask claim. Frames 0/1 were warmup and had zero edits. A short functional pass does not establish quality or performance benefit.

Frozen v1b packet: `8db1a88160ae91226508eeebd524354165b6c924a25c571d2dcb62c5a34e7fad`; executable: `b41cb872f09233e8e2681c48dbb5758bee2f5fd5322e68b0f662432f55c70ebd`.

Actual manifest: `e1b3647909cc1b75112c63b06231a24b9ec533dc5b5b403c1bafcd58d2414b7a`.

Independent CPU review manifest: `59ded1dd9ed721a49107ac8c96367ed214158f25b9304adaed46024a739496fe`. It rehashed all 69 actual files and 85 prepared files, verified the real runtime/retained owners and recording identities, checked 48 readback planes, and independently recomputed the composition without importing the GPU composition implementation.

The first CPU compilation's accidentally removed helper prefix was restored before any GPU run; its failure remains preserved. A CPU-only v1 receipt incorrectly looked for a three-entry phase-array declaration instead of taking three frames from the unchanged 32-entry schedule. The old parser demonstrably raises IndexError; v1b changes only that parser constant, with the same executable/shaders/scene. The old 80-file packet and new 85-file packet are separately preserved. Real launch/collision checks use the local no-kill helper; the legacy timeout-kill helper is not called. No failed GPU arm was retried.

Own-source subsets: [scene-two-rr-v1](raw_nr_probe_review/scene-two-rr-v1/README.md), [v1b parser correction](raw_nr_probe_review/scene-two-rr-v1b/README.md).

### True Feature1 SR-off first execution

Root executed the frozen SR runner r3 once. Native Init and the selected SuperSampling capability succeeded (Available=1, NeedsUpdatedDriver=0, FeatureInitResult=1), but the host stopped with exit 41 at its callback-module identity check. Scene Dispatch and SR/RR/NR Create/Eval were zero. The process exited and released the GPU; no later arm or retry ran. This is a host identity refusal, not evidence that SR is unsupported or that a model Create failed.

The retained callback's address, module path and file hash had been computed but were printed only after the failed check, so the first result cannot identify the refused owner. A separate minimal Init/capability-only observation is required to resolve that missing evidence without widening the check or invoking the callback/model.

Native SR packet: `0bdbc29d5ebbd0c566df3daf4a569b0e301c09bb7d8c1daf81a6c2639f5cc36b`; executable: `154f272aa59ad1005904e55ec815265892c2cadb80a0674eab122ceaeabf053d`.

Actual execution receipt: `dd577926b795f79e02dbe46fbc1423f93d28183b676345b360dfa3ba19ff7508`; failed stdout: `bde46f008b0e658b43359e5094ceb787c2b83d21fa0254a2292dff8c1aaa133e`.

Own-source subset: [native-sr-r1](scene_bench_review/native-sr-r1/README.md). The original failed host/runner packets remain unchanged.

Shared main integration remains Claude's responsibility. This document is execution evidence for review, not an issue closure or release authorization.
