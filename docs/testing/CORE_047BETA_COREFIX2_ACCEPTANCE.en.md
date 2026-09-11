# 0.4.7beta-corefix.2 acceptance candidate

This candidate follows corefix.1. It adds one-time migration of experimental NR jitter to OFF, event-based waiting for already submitted GPU work, and session-only flicker/timing comparisons. Local compilation and software GPU tests are separate from NVIDIA model, game-image and performance acceptance.

## Update and settings

Close the game before replacing Core. Keep only one language variant and use the matching bridge DLL. Back up the previous Core; preserve game settings, NR runtime and other components.

Old configurations without `JitterPolicyVersion=1` migrate to `JitterMode=0`. The migration marker is written only after the OFF value succeeds. New installations and explicit default resets use OFF. A later manual opt-in remains saved across restarts. Native SR jitter and the known-zero coordinates of the resolved post-SR color grid keep their existing contracts. Other picture, route, scale and warm-up preferences are preserved.

## Flicker comparison

In **Feedback and diagnostics → Flicker comparison**, compare **Normal sampling** and **Pause sampling**, about 30 seconds each in the same scene with the menu closed. Save an F8 text report after each segment and describe whether enhancement remains visible and whether flashes continue. Change no other setting between segments.

The choice lasts only for this process. NR continues processing; the switch neither writes INI nor resets model history. Pause prevents new automatic GPU diagnostic samples, including recovery probes. Pending readbacks still retire with their original phase. Explicit image captures are independent, so do not use F7 during this comparison. F8 text remains available.

For Death Stranding, keep stationary/photo-mode and movement results distinct. For Cyberpunk 2077, report ordinary SR and actual RR activity separately. Requested RR alone does not prove an active RR evaluation. Processing/submission counters do not prove correct displayed pixels.

## Stutter comparison

Use **Waiting strategy** to compare **Event (default)** and **Legacy polling (comparison)**. For each strategy, start three independent 30-second timing segments, close the menu, keep the same scene/settings and save F8 after each segment. Both strategies retain the same completion checks and 50ms budget.

Timings describe cumulative CPU observations within a segment. Nested stages overlap, percentile values are histogram bucket bounds, and the retained call list is limited. Mixed settings/scopes are explicitly marked. These observations do not measure GPU inference, FPS or complete input-to-display delay. Check real input response and NR coverage separately; reducing NR work is not a performance fix.

## Forza Horizon 6 feedback

Shadow trails and headlight color bleed currently have only a verbal report. Preserve the reporter's route, model parameters, resolutions and HDR state. With experimental jitter OFF, repeat slow movement followed by a stationary view at extra enhancement 1.0 and the reproducing strength. If unknown, 2.0 is only a labelled test starting point.

Take F7 separately if needed to compare Original, Neural and Composite. The exported Neural plane is spatially restored rather than the raw model-size texture, and the export is not a same-frame final display capture. No image-algorithm repair is claimed without a reproduced and attributed defect.

This package does not publish or replace a stable release. See its build-info and SHA256 manifests for the exact candidate identity.
