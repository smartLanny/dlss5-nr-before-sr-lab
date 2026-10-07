# Raw NR probe: source review excerpts

These four files are a source-review aid for the private probe changes. They are **not a portable or buildable host**, and are **not a complete source/provenance package for the actual executable**. No production Core source is changed here. No NVIDIA SDK headers, libraries, DLLs, models, binary shader data, captures or raw runtime/tester logs are included. API names/parameter keys and SHA pins in the code are call/identity references only.

The original private sealed sources and diffs remain unchanged. The three full diffs preserve every code hunk, except the following deliberate review-only substitutions: all `---`/`+++` absolute-path labels use relative `a/` and `b/` stage labels; the sole absolute Core include in the v3 context is `#include REVIEW_ONLY_CORE_INCLUDE`. That placeholder is intentionally undefined. No build/run instructions or executable are supplied. The private audit reversed these substitutions and reconstructed each original diff byte-for-byte.

The committed review copies additionally normalize CRLF to LF; the code hunks remain identical after that newline normalization. The directory's Git attributes retain LF on checkout. The table below records the committed copies' digests; the original private archives retain their exact bytes.

## Original and review-copy identities

| Private archive label | Original SHA256 | Review file | Review SHA256 |
|---|---|---|---|
| raw-nr-model-safe-host-v3/critical.diff | `5dd055f569bbd35bbc89a61743d6e86a099fc54ef2b01322a502bd120f86eb42` | raw-nr-safe-v3.diff | `d927797be423eb9b5a28df02f7650becfca81ef0f9cce72418319ba94757105c` |
| raw-nr-model-safe-host-v4/critical.diff | `79c63b2ef088c06daf21ab4dbdbc27a552658ed8d81c3d736d8273b1e7c5517c` | raw-nr-safe-v4.diff | `9a22d8d6539c860d6b9ededfb50f51cece7f14cbd11acf60ea53b394d501dbec` |
| raw-nr-model-execution-v1/critical.diff | `c300285c0cc68ff4c29293fc7acf8971f64f768e98ee6f6f95426dadd332ae51` | raw-nr-model-execution-v1.diff | `e7cc810099310aba86d0ec87e195b78dad7f628ffc2a62221030020adc81f48f` |

## Delta chain and scope

- v3: project-owned terminal `_Exit` discipline, shared opaque failure sink, CPU descriptor/resource owner and the no-NR control branch.
- v4: explicit fresh-factory/observer state provenance and carry only after exact flight/fence/Reset retirement; retains the v3 control and model continuation.
- execution-v1: cold NR init and actual export-owner/device/domain receipts plus pre-Create/Eval checks. The helper uses existing production NR services; its source is not proof that model inference ran.

The new helpers needed for these deltas are included in full as added-file hunks. Full inherited hosts, generated entry TUs, SDK dependencies, binaries, preparation/run/seal scripts and logs are deliberately excluded. Therefore unresolved types/includes and private prerequisite fixtures remain external; these excerpts must not be described as independently reproducible.

## Existing CPU terminal receipt summary

These are summaries of already sealed CPU receipts, not new execution and not GPU/model acceptance:

| Stage | Original receipt SHA256 | Observed terminal exits | Reference |
|---|---|---|---|
| raw-nr-model-safe-host-v3 | `74d205a18aa4c98c1a025a6d38cb664309552d6bfdc9f2acfd83a499118daf90` | check:41, hr:2, fail:42, parent-stop:42 | std::exit:42 |
| raw-nr-model-safe-host-v4 | `fea3acc9e181ce7d9e2e2b9e209ff47767300f80973ec3bf79a64a32e637d43f` | check:41, hr:2, fail:42, parent-stop:42 | std::exit:42 |

For all four routes in each listed receipt, own stdout/stderr were flushed and registered atexit/static-destructor sentinels did not run. The isolated old `std::exit` reference ran both sentinels. This proves only the tested CRT terminal discipline; it does not prove GPU failure closure, vendor partial handles, device loss, failed Reset, NR init-domain success, model phase, quality or performance.

The older v3/v4 CPU-preparation READMEs are not copied here and must not be used as current actual execution status. Actual EXE identities, runtime tuples and full private execution receipts remain separate evidence.
