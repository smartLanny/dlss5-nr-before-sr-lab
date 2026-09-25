# Compatibility post-processing / 兼容后处理

Refs #190 #226 #113. This source candidate continues the owner's D14 `49b0deb0d09b0a442784e0ebf1073f9498eaf157` through the preceding standalone Auto/UI experiments. It does not modify the production D14 checkout or install an addon. The runtime includes below are real D3D12/own-Core implementation, not fake provider callbacks. Production integration, Windows compilation and runtime validation remain required.

## What is implemented

- Actual ReShade present/init_swapchain/destroy_swapchain callbacks; one observed native D3D12 swapchain, one node, full supported typed output.
- A private command-list/allocator/fence ring on the ACTUAL presentation queue. No extra guessed queue or swapchain and no native SR/RR call from this runtime.
- Current backbuffer copy, SDR/scRGB/PQ-to-canonical adapters, existing own Core EncodeV2, signed Feature18, existing CombineV2 and RT draw into the same backbuffer. No swapchain UAV requirement.
- Independent per-layer Feature/create params and per-flight evaluation params. Actual Create fence completion precedes Evaluate. Existing reduced auxiliary and full-reference 2-4 pass components can be reused.
- Explicit COLOR-ONLY experiment: constant far depth, zero motion, Reset on EVERY Evaluate. This is not valid native temporal input or a recreation of RenoDX's unknown dummy-history policy.
- Master off/zero strength stops new work; existing work drains. Retirement APIs run outside the Core chain lock. Opaque/Close/Signal/device failures quarantine, not retry. Bounded teardown wait only; no per-frame CPU wait.
- Third menu name is 兼容后处理; old PRE/POST choices and saved Mode=1/2 remain. FG-off confirmation is session-only. Independent F8 and exact-fence GPU timestamp evidence distinguish submission/completion from visible quality.

## Reference and limits

The owner's 9.8.5 attachment has SHA256 `c575f3f7c8ca6f4ef3baa2194ea8e37e06e8ea32ee0701ecfb99035291304b00`. Static strings describe available native/FG resources versus an optional dummy-temporal mode, swapchain color space, same-adapter D3D12 transport for D3D9/11, and producer/queue rejection paths. A rejection-string code reference was located; this is not full equivalent-source or runtime proof. The attachment was NOT executed. No vendor binary, extracted shader, model or user log is included here.

This candidate implements only the own-Core native D3D12 Present color-only lane. Native guide leases/coordinate mapping, reliable FG application-frame identity, cross-API transport, multi-swapchain/GPU and production three-boundary Auto are NOT completed. An observed FG handle blocks even with manual confirmation. Color-only mode is explicit and off by default, not an automatic universal fallback.

## Integration

Copy the eight src files into the current Core source staging, retaining companion includes. Include `nr_compat_present_policy.hpp` outside the Core private namespace. Forward-declare Snapshot/TrySnapshot/Register/InlineDrainReady inside that namespace before consumers. Include runtime late, after actual NGX/codec/guide/lifetime definitions, before settings UI. This is not a standalone addon translation unit.

Add ChainConfig fields `compat_present=false`, `compat_color_only=false`, `compat_fg_off_confirmed=false`, `compat_percent=100`. Wire Normalize/SameConfig/History/Reload/Save/restore and frozen snapshots. Persist only CompatibilityPost, CompatPostColorOnly and CompatPostPercent (50-100); never persist FG-off confirmation. Retain the old Mode value.

Under the existing chain lock, both PrepareBeforeSr and PrepareAfterSr must refuse new inline NR while compat_present is selected or !nr_compat_present_runtime::InlineDrainReady(). Original SR/RR still runs once; do not add another native SR evaluation or bypass global fault/owner rules.

Register the new callbacks after the existing OnPresent and unregister in both cleanup paths. Preserve original F6/config polling. Replace DrawProcessingModeBlocks with the included UI; switch the precision/auxiliary UI and live status to the compatibility versions when selected. The auxiliary controls require the preceding full-reference config fields. Insert feedback include at FeedbackAppendActivity using nonblocking TrySnapshot. Block the old native-stage F7 matrix in both UI and RequestWithOptions while selected, rather than mislabelling Present output as native stages.

The owner's consolidated source handoff includes `compat_post/tools/stage_compat_present.py` plus the previous full-reference package. That generator produces a unified `changes.patch` in a new external directory after exact D14 pins pass; it has not been run on a complete checkout here. Do not downgrade newer source to satisfy pins. Review the current generated-source allowlist, repin only reviewed changes, build bilingual Core and all production shaders. Never release a same-name D14 binary with different bytes.

## Actual checks and next gate

`g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic experiments/compat_present/tests/test_present_policy.cpp -o present_policy` then run it: 195 CPU policy/extent/fence/color-reference checks passed. The same suite passed Clang17 with AddressSanitizer/UndefinedBehaviorSanitizer. Eight Python source-fragment/staging refusal tests passed in the consolidated package; an earlier repeat-application test failure was corrected and retained in its evidence. None of this compiles or executes the Windows runtime.

NOT DONE: Windows whole Core, utility HLSL, WARP, real NGX, actual ReShade UI, games, performance or hosted CI. The Windows shader harness is provided but not run. First validate full surface conversion/identity/alpha, private queue ring and error/resize/disable lifetime on WARP; then single/double/full-reference NR with FG off. Test a non-DLSS D3D12 title and a native SR/RR title. No PR, main merge, game deployment or release before owner validation. #252's existing F6-off hook overhead is separate and is not claimed fixed by this addition.
