# RR retirement-event handoff candidate — #234

Parent: PR #232 / `edb2262bf5eab3c378c4be830219a803328d1b51`.
This is an independent Core performance change, not Manager #235, a new RR algorithm, or a claimed resolution of the owner's 80–90% GPU-utilization report.

## Change and invariants

`WaitForNrSubmittedFence` previously switched to Sleep(1) polling whenever another target occupied the queue's single event. For a strictly earlier registration on the SAME fence, consume that event and then register the exact requested target, using the original deadline throughout. Completion still requires the requested fence value; a notification alone does not prove it. Later/foreign fences retain the existing safe fallback. Outstanding registrations retain their reference and event through timeout/removal. No extra queue Wait, slot, NR history, or skipped NR frame is introduced. The stable-same-queue admission path is unchanged.

`earlier_event_waits` is returned by the helper for direct diagnostics/tests; it is not yet a new F8 field. Existing F8 method/fallback/timing fields continue to identify event vs polling. Do not claim the handoff occurred in a game from source inspection alone.

## Executed verification

GCC 14, C++17, `-Wall -Wextra -Werror -pedantic`: 18 deterministic fake-Win32 cases execute the production `.inl`. Same run with AddressSanitizer and UndefinedBehaviorSanitizer passes. Parent helper reconstructed byte-exactly to Git blob `300ae92b81217121790a24429f6b89d93d3aed3f`; the same first scenario fails its no-polling assertion on that parent. This proves the local fallback change, not a GPU speedup.

```
g++ -std=c++17 -Wall -Wextra -Werror -pedantic tests/submitted_fence_handoff_portable.cpp -o handoff-test
./handoff-test
```

No ROG connector was available in this execution. No Windows DLL build, WARP, NVIDIA, game, or hosted-CI result is claimed. No binary, capture, personal path, or vendor payload was added.

## Fast local acceptance / next discriminator

Use the actual #232 build/source as baseline. Run the existing Windows `tests/submitted_fence_wait.cpp` gate and bilingual build before deploying only to a backed-up test installation. Compare baseline-event and candidate-event with fixed scene, FG/MFG off, same model/precision/pass count and actual R/N/O, frame limiter/Reflex unchanged. Preserve NR success/effective submission rate and final output; report base frame-time percentiles plus per-Feature13 retirement waits/method/fallback and time.

Then use #232's existing normal/paused-sampling and event/legacy-polling controls. If waits coincide with GPU queue gaps, investigate exact queue provenance and bounded in-flight scratch reuse. Never infer a future list's queue from the previous frame, remove lifetime fences, or increase work merely to show 99% utilization. If waits are negligible, move to actual GPU timestamps and CPU/sampling cost; do not continue speculative scheduler rewrites. #234 stays open pending same-scene evidence.
