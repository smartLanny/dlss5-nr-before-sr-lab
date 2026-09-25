# SF9.8.5 comparison: Auto receipt safety candidate

Based on the owner's D14 branch at `49b0deb0d09b0a442784e0ebf1073f9498eaf157`. This directory contains a standalone, executable route policy/dispatcher and regression tests. It does not register production hooks, call NGX, provide missing guides, change Core defaults, or implement a working Present-to-Core consumer.

Refs #190 #226 #113. This is the independently validated part of the consolidated SF9.8.5/full-multipass/Auto source handoff. The complete downloadable handoff also contains the previously delivered full-reference wrapper, Windows owned-command-list helper, exact-source patch generator, and local integration instructions. Those broader pieces are not claimed as this commit's production changes.

## Corrected contracts

- A receipt marked Recorded cannot be downgraded to RejectBeforeWork to release its source claim. Contradictory use poisons this device generation instead of running NR again at a later entry point.
- Successful completion without Recorded is not accepted as execution proof. Opaque failure still poisons even when it happened before Recorded could be published.
- Current selected, last recorded and last confirmed source IDs are distinct. Late callbacks cannot relabel old work as the newest source.
- Before, After and Present must share a nonzero, verified presentation lineage. Present counts, texture pointers and repeated jitter alone are not valid cross-entry identity.
- Invalid enum values do not index arrays. Native guides take precedence over a simultaneously offered synthetic path.
- Manual choices remain strict; Auto only chooses registered, presently admissible providers. Command-state refusal is not a lifecycle fault. No hidden old-image reuse, SR retry or external NR owner is introduced.

## Build and checks

From repository root:

```text
g++ -std=c++17 -O2 -Wall -Wextra -Werror experiments/sf985_auto/tests/test_route_receipts.cpp -o route_receipts
./route_receipts
```

The same 213 checks passed in this session with GCC 14.2 and Clang 17; the Clang run enabled AddressSanitizer and UndefinedBehaviorSanitizer. A separate minimal regression compiled successfully against the previous delivery's policy and returned the expected failure code 19; the corrected policy returned 0. These are CPU/synthetic state tests, not a reproduction of any game's GPU fault.

No Windows/Core/HLSL/WARP/NGX/game/performance or hosted-CI result follows. ROG was not accessed. No PR, deployment, main merge or release is requested before the owner tests the completed local candidate.

## Binary findings, with limits

The uploaded file named 9.8.5 has SHA256 `c575f3f7c8ca6f4ef3baa2194ea8e37e06e8ea32ee0701ecfb99035291304b00`; the prior attachment is `9cba146b53129880e58eb372272e8cfd7c9974857cf1235558f4c1f2d0a41b18`. Neither was executed.

Static inspection found request layer indices 0..9 and scoped switching of parameter/Feature pointers in 48-byte per-pass records. This supports independent persistent layer slots, not a blanket shared-history claim. The selected old/new Evaluate functions each contain 572 instructions with matching normalized instruction structure; this does not prove the entire DLL or its closed model is equivalent. The major embedded DXIL containers differ in bytes, but the precise image-processing difference was not established.

The old attachment's visible hook choices are Upscaled and Present; the newer attachment adds Render. A pre-SR 1080p multi-pass is not the same image or pixel budget as post-SR 4K multi-pass. First match placement, actual work dimensions, pass count and effective parameters. Keep the label 'own-Core full-reference experiment', not 'bit-identical RenoDX'.

Render/Upscaled/Present are NR insertion stages. They do not decide whether the game performs SR. A game without native SR does not acquire DLSS upscaling merely by enabling Present NR.

No proprietary binary, extracted shader, model, raw user log or private local path is committed.
