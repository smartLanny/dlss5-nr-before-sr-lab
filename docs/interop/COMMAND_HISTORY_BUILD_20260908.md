# Command-state history and separate post-runtime build

`8aa80e9` integrates the production history fix from Sol's `d110a66`.
Previously, a later successful capture for the same command-list token could
replace its earlier rejection. History now coalesces only matching token,
captured state, reason text and route text. Different outcomes survive within
the existing 12-entry bounded history; lock contention and dropped-entry
accounting remain bounded. This improves F8 evidence, not rendering, and does
not establish a fix for the reported #121 flicker.

Focused Windows checks on the original `d110a66` source passed: five production
history assertions, four integration-pin tests and 693 existing null-guard
predicate assertions. These ran without GPU work:

```powershell
./scripts/validate-command-state-native.ps1 -ToolchainRoot $Toolchain -HistoryOnly
python -m unittest tests.multi_api.test_picture_integration -v
$env:CXX = $ClangPath
python -m unittest tests.test_null_guard_reasons -v
```

Private `command-history-d110a66-20260908` validation SHA256 is
`86e5f219f845d8f3f2f10f9fab56954c63fa7b1c596c6df70c30e463919edf91`;
the history-only log is
`384a5b284e90176730ff8dab48cc6c12a15fd184636aabf6d0568707fd655865`.

The separately frozen build source is
`d4cb94a70484bbd40ff9f27178d417078725bea4`, including that history change and
the corrected NGX parameter comment. The preparation pins and generated
manifest passed; 115 generated files matched their hashes after linking.
Both Core languages and nrchain linked with the existing LLVM-MinGW toolchain.
Both Core binaries embed that source commit and preserve `NR_SOURCE_DIRTY=1`
for generated integration source. Existing compiler warnings remain in logs.

From the clean source checkout, with the fixed bridge/ReShade/toolchain paths:

```powershell
python scripts/prepare-multi-api-beta.py --core . --upstream $Bridge --output $Stage
& "$Stage/core/scripts/build.ps1" -ToolchainRoot $Toolchain -ReShadeSourceRoot $ReShade
```

| Artifact | SHA256 |
| --- | --- |
| Generated source manifest | `dcc427bca522d2d2352a35bfcbb0d5fad2390074c12977226729fe1b7edbe2e0` |
| Build validation | `b57fcefe619a60b12e4b1995a43c0dc383a4cbc5fb9157a1f6fa5bf06414f437` |
| Chinese Core | `9420d0a9678cf205a16179aaece4698dcd9db3a15d8075e1dee88a1552721939` |
| English Core | `62c3616551f65f3a5ada2505da4150e83abd2f6dfcb7f6083686fb439fe89448` |
| nrchain | `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb` |
| Link log | `053c116895313b1017a764c22b3a0334fb9e6605523d96d29083f3981ed8df11` |

The parent task independently rehashed all seven entries in the private
`SHA256.json`. Private evidence is retained under
`history-core-link-d4cb94a-20260908`; no binary, raw log or local path is added
to this source summary.

An initial evidence-summary script used an unsupported PowerShell
`Select-Object -Single` parameter and wrote incomplete metadata. Those files
remain as `*.failed-attempt.json`. Explicit one-file count checks and rehashing
produced the final metadata above. This was a corrected evidence-summary error,
not a compilation or linking failure.

This is **compile/link validation only**. No WARP, NVIDIA NR, game or hosted-CI
success is attributed to these binaries. The separately tested `e7df0fc`
external-provider hardware build remains the evidence for that earlier runtime
slice; it does not contain this later history change. No release, tag, Manager
payload or installed game was changed.

## Related old game reports: source status, not retest results

#139/#140 both bind their old `null-pso-policy-disabled` logs to `9282e6ba`.
Current source inherits the later closed-world null-state admission: published
hooks, observed successful Reset and narrowly tracked state are required;
unknown or tainted state still refuses. Thus the old policy gate already has a
real source replacement. Reimplementing it from those old logs would repeat
completed work. They do not prove the current DX11 carrier, NR invocation,
shared-resource copyback, reset or exit lifecycle; both game reports stay open.

#148 is also the old `9282e6ba` runtime, despite its installer label. The native
period stays at `waiting-feature1`, lacks the SR DLL in its inventory and has
no native Feature1 evaluation. Its separate feed log does not establish the
native carrier route. First establish the actual SR provider and Feature1 on a
safe current candidate, then NR/copyback; no additional archive request or
relaxation of command-state guards follows from this report.
