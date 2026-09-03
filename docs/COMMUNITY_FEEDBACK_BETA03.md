# Community feedback triage for beta0.3

This is a sanitized engineering triage, not a compatibility claim. Reports from
beta0.1/beta0.2 do not prove the current candidate has passed that title; they
identify contracts that beta0.3 must either support explicitly or bypass with a
clear reason.

| Reported title/path | Earlier symptom | beta0.3 response | Current status |
|---|---|---|---|
| Cyberpunk 2077 | `NR -> SR` exposure drift and crashes after repeated order changes | Original HDR base plus bounded neural residual; exact-fence retirement; debounced settings; RR ownership exclusion | A pre-current candidate sustained >99% Feature-18/carrier execution in both orders, survived five Enabled cycles, and passed 2,400+ RR frames with zero Feature 18. The current affinity-marker revision still needs the minimum gate. |
| Wuthering Waves | Enable appeared ineffective; `SR -> NR` rejected format 26; beta0.3.1 then reported permanent RR observer uncertainty although no Feature 1 had run yet | `NR -> SR` is the default; R11 input and output are adapted through controlled FP16 intermediates. beta0.3.2 also distinguishes a harmless startup-deferred `_nvngx`/DLSSD module from a true post-Feature-1 observer gap, installs its lifecycle hooks during a one-frame native warm-up, and fails closed only when coverage or ownership cannot be proved | Both concrete logged causes are addressed in source. Fresh title evidence is required; this is not yet a compatibility claim. |
| The Witcher 3 | Only `NR -> SR` appeared effective; later HDR feedback reported a slightly darker result | Mixed-resolution post-SR guides remain supported when the live runtime accepts them. A supplied beta0.3.1 ColorDiag log shows the proxy mean follows the original (about -0.06%) while the NR output carries a broad negative luminance residual (roughly -13% to -17% in the later samples) and the final Mode 2 image retains about -5% to -7%. beta0.3.2 labels this distinction directly and exposes the existing opt-in Lighting Match ablation | This is not evidence for adding an arbitrary exposure gain. The exact beta0.3.2 build still needs a fixed-camera pure-SR / default `NR -> SR` / Lighting-Match comparison before deciding whether the correction is preferable. |
| GTA V Enhanced | Pass appeared inactive around exposure changes; settings appeared to return to defaults | The matching log showed valid Feature 1 and active subrects, but the earlier build rejected a 1x1 `R32G32B32A32_FLOAT` ExposureTexture. beta0.3 samples the red channel for supported one-to-four-channel float exposure textures; INI writes are checked and a visible warning replaces silent persistence failure | Exact logged cause is addressed in source. A fresh GTA run is still required. |
| Hogwarts Legacy / UE titles | No effect, weak effect, or large order-dependent face/lighting difference | Per-Evaluate active extents outrank stale Create dimensions; high-resolution MV is resampled; color diagnostics separate model delta from composition delta | Both orders ran locally on the preceding candidate. Exact visual equality is not expected because render-resolution NR has less source detail. |
| Games reporting “waiting for compatible/valid DLSS frame” | No recognized Feature 1 carrier | Explicit waiting/bypass state and hook-source diagnostics | Not automatically fixed. Translation layers, DX11, non-Feature-1 paths, or a different NGX module remain outside this native D3D12 carrier contract. |
| R8 output titles | Post-SR path bypassed or did nothing | Linear and sRGB conversion paths exist, selected by an explicit storage contract | Unknown R8 encoding still fails closed; this is intentional rather than a game-name whitelist. |
| RTX 40 reports | Old packages failed or never reached Feature 18 | Same beta0.3 add-on source is used on RTX 40/50; only the external runtime track differs | Old RTX 40 packages are obsolete. SF-v2 remains a community test dependency and is not redistributed here. |
| RR/path tracing | Extra NR was ineffective or degraded an already reconstructed image | Feature 13 is never used as a carrier; while RR owns a live handle, companion Feature 1 calls are native passthrough | Safe coexistence by exclusion, not joint RR+NR. Joint integration remains a separate research branch. |
| “FPS loss but no visible change” | Feature 18 may run while its residual is near zero, or the final transfer may be suppressed | Sparse four-stage RGB/luminance/hash diagnostics, model/final delta counters, zero-transfer native bypass, and threshold-labelled proxy/model/composition attribution | New logs can distinguish runtime rejection, model near-no-op, a neural luminance residual, composition near-no-op, and normal low-resolution weakness without per-frame log spam. |
| Settings snap back immediately | An older INI or failed write can overwrite the live UI choice | Writes are verified; on persistence failure disk polling is stopped for the process and the overlay shows the Windows error | The silent overwrite path is addressed; directory permissions still need correction by the user. |
| Garbled Chinese name/version | Source/resource encoding or a stale copied filename obscured build identity | Explicit UTF-8 compilation, Windows version resources, startup filename logging, and separate Chinese/English filenames | Addressed in the build; report the exact filename shown by the overlay. |
| Dragon's Dogma 2 / long-session crashes | No matching dump proves whether the cause is hook ordering, command-list reuse, another injector, or this add-on | Reset-success gating, exact-fence retirement, duplicate/replay diagnostics, failed-create quarantine, and hook-source logging | Risk is reduced, not proven fixed; a sanitized matching log/dump is required. |
| Palworld stutter but effective | Temporal resource churn, shader/runtime warm-up, or another injector may dominate | Contract generations, warm-up/native frames, submitted-transfer totals and lifecycle diagnostics are now visible | Optional current-build follow-up; no title-specific workaround is claimed. |

## Logging policy

The overlay always displays the current execution/bypass state. The file log
records startup identity, Feature 1 generations, accepted/rejected live
contracts, lifecycle quarantines, the first three diagnostic samples, then one
summary every 600 samples. Every 600 carrier frames also records Feature 18
success plus transfer `prepared`, command-list `recorded`, lifecycle `bound`,
native-queue `submitted`, and bounded queue-affinity totals. Bypass changes are logged for
the first twelve transitions and then every 600th transition. This keeps a
useful bug-report trail without writing per-frame state.

Every report should include the add-on filename/version shown in the overlay,
GPU/runtime provenance, game API, RR state, selected order, and the smallest
matching sanitized log window. Appearance alone cannot establish whether
Feature 18 ran.
