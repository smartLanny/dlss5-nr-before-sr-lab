# One native RR completion slice: source-review excerpt

This directory contains the complete project-owned critical delta for the frozen private one-RR host and this README. It is a **review-only excerpt, not an independently buildable or portable host, and not a complete source/provenance package for the actual EXE**. No execution result is asserted. Planned `PASS` messages and validation checks in the source are not evidence that they ran.

## Frozen source identities

| Item | SHA256 |
|---|---|
| Original private archive `raw-nr-one-rr-host-v1/critical.diff` | `f01ba7901c350294ed27b070d4d333fc7caebe37e546bb7c57e0a35b4ffa3a1a` |
| Review copy `one-rr-functional-v1.diff` | `41ae7e382c5a5b227032391d93fb54f23929fc34b620c99b6c5709cebd7e514c` |
| Frozen private packet manifest | `a3eb6beccfdbb3cbbf527134d12f3422d89ff1d4d2cc21ce603a4acd1cf13d2a` |

All five diff sections were reconstructed exactly from the sealed before/after files: inherited files under `frozen-model-v1/host/` map to the private model-execution-v1 host; `one-rr-v1/host/` maps to the new frozen host. The two new `.inl` files have an empty before image. Their declared source hashes and frozen manifest entries match. These are provenance checks of the delta, not compilation or GPU validation.

The original diff already uses relative stage labels and contains no private absolute path or absolute include in its hunks. Therefore no path placeholder or code replacement was necessary. This review copy only normalizes CRLF to LF; all other original diff bytes and code hunks are unchanged. The original private diff, host, packet and historical files remain untouched. Retain LF for this review copy when committing it.

## Narrow planned slice

The new helpers add a pre-Create original capability/optimal gate; a real Color-only native RR transaction with non-Color checks; an O-sized sentinel/readback RGB-write check; and same-recording NR-producer/RR-consumer exact-fence retirement. The planned two independent histories use fixed R1280x720/O1920x1080 and the existing J schedule: baseline raw-to-native-RR, and production Feature18/codec editedStored-to-native-RR, with one native RR evaluation per frame. This describes source intent only; final run results are not included here and no PASS is claimed.

Production Core routing, Feature1 adapter semantics, runtime binaries, user configuration and game files are outside this change. This excerpt does not activate a game route or complete the separate two-RR clean-guide objective. Product integration, actual game contracts, general 3D quality, performance, vendor partial-handle failure, device loss and failed Reset remain outside this review copy.

## Excluded dependencies and evidence

Only the full critical diff and this README are exported. Complete inherited host/entry source, SDK headers/static libraries, vendor implementations, DLLs/models, generated EXE/object/shader bytes, raw images/tensors/captures, runtime/tester logs, compile/run scripts and machine-specific receipts remain private or in their existing licensed locations. API names, parameter keys, SHA pins and generated output filenames in the diff are references, not bundled vendor payloads or actual raw logs.

Unresolved types, includes, owners and production functions depend on those excluded sources. Do not relabel a sealed EXE as built from this review directory or from a later repository head. Any actual result must retain its own exact source/EXE/runtime/command/counter/fence evidence and be reported separately.
