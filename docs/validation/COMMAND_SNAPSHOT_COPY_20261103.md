# Command-state snapshot copy: CPU evidence, 2026-11-03

## Change and scope

Baseline: `main@024f9cc32778c1056db364d76d087ee257da6502`.
Candidate `src/nr_command_state.inl` SHA256:
`a9824cad1ffef6e2991a3213be5133f4035ed86ef4f805a41449d72ad4a94188`.

`CopyNativeCommandState` runs when capturing the game's command state and when
restoring the recorded shadow. Previously it copied all 128 root-argument
records, including 32 KiB of constants payload regardless of binding kind.
The existing scalar setters already invalidate the constant mask instead of
clearing this inactive payload; the snapshot path had retained the blanket copy.

`CopyCommandRootArguments` now always copies each kind, scalar value and mask.
It copies constants only for a Constants binding with a nonzero mask. Masks
confined to words 0–15 copy a fixed 16-word prefix; masks with higher valid bits
copy the complete 64-word array. This also avoids excess copying for legal
signatures containing many one-word constants. No binding, replay operation, COM reference or valid constant is
omitted. Empty slots replace destination tags/masks, so repeated snapshots
cannot resurrect older bindings. The enclosing descriptor-copy failure path,
owned RTV/DSV handles, lock scopes and resource lifetime behavior are unchanged.

This is a CPU copy optimization. It does not lower GPU work, memory allocation,
resident VRAM, model resolution or NR strength. The snapshot still has the same
allocation and layout. It makes no game-FPS claim.

## Production-path validation

The new fixture extracts the actual production state types,
`CopyNativeCommandState`, `CopyCommandRootArguments`, setters and release helper.
It supplies only CPU D3D12/COM doubles. It does not replace the implementation
with a handwritten copy algorithm.

```sh
python3 tests/test_command_snapshot_copy.py
NR_SNAPSHOT_BENCH=1 python3 tests/test_command_snapshot_copy.py
ASAN_OPTIONS=detect_leaks=0 NR_SNAPSHOT_SANITIZE=1 python3 tests/test_command_snapshot_copy.py
python3 scripts/validate-command-state-hotpath.py --compiler g++ --output <new-directory>
python3 scripts/validate-command-state-lockperf.py --compiler g++ --output <new-directory>
```

Environment: Linux x86-64, GCC 13.3.0, C++20, `-O2`.
The fixture uses the canonical Core warning policy (`-Wall -Wextra -Wpedantic`);
it does not disable the pre-existing GCC class-memaccess warning from
`ReleaseNativeCommandState`'s dynamic-state memset.

| Check | Result |
|---|---|
| New portable test file | 4 tests pass |
| Actual capture/restore state machine | 20,000 sequences; 16,290,350 assertions per unmodified implementation run |
| Mask edge cases | Both stages, slot/bit 63, prefix boundary bits 15/16, full → scalar → partial, zero-count constants, repeated dirty destination, self-copy pass |
| Enclosing production behavior | Descriptor ownership, failed descriptor copy, nonargument metadata and COM balance pass |
| Five deliberately broken candidates | Each compiles; missing tag/value/mask/wide-constant/short-constant copy fails its specific runtime assertion |
| Previous blanket-copy call | Semantic tests pass; inactive-write budget fails with 0/8,192 untouched constant words |
| Current scalar-only copy | All 8,192 inactive words untouched: 32,768 payload bytes avoided per snapshot |
| 128 one-word constant roots | All 6,144 words beyond the copied prefix stay untouched: 24,576 payload bytes avoided |
| Address/undefined-behavior sanitizer | 4 tests pass with leak detection separately disabled for the environment limitation below |
| Existing command-state hotpath validation | Pass; original setter/cache negative controls continue to fail as expected |
| Existing lockperf validation | 27 scenarios pass; old global-lock negative control fails as expected |

The initial combined sanitizer run failed at LeakSanitizer shutdown because the
sandbox cannot open `/proc/<pid>/task`. That is not recorded as a product pass.
The subsequent run retains address and undefined-behavior instrumentation but
uses `ASAN_OPTIONS=detect_leaks=0`; leak detection remains unverified. Explicit
fixture COM reference accounting passes independently.

## Microbenchmark

The benchmark executes the full production `CopyNativeCommandState`, using the
previous blanket-copy statement as the comparison. Checksums match. Each cell
is the median of five runs of 100,000 copies, with already allocated states and
no descriptor/COM changes. Timing is informational, not a CI wall-clock gate.

| Binding scenario across both stages | Previous copy, ms | Candidate, ms | Local copy-time change |
|---|---:|---:|---:|
| 64 scalar CBVs, other slots empty | 82.181 | 12.327 | −85.0% |
| 2 × 4-word constants + 60 CBVs | 81.032 | 12.518 | −84.6% |
| 8 × 8-word constants + 32 CBVs | 76.645 | 13.341 | −82.6% |
| 32 × 2-word constants + 32 CBVs | 76.228 | 16.133 | −78.8% |
| 128 × 1-word constants | 71.759 | 40.941 | −42.9% |
| 2 × 64-word constants | 71.139 | 16.425 | −76.9% |
| Synthetic stress: 128 × 64-word constants | 75.130 | 84.496 | +12.5% |

The first six scenarios each fit the 64-DWORD budget of each separate stage's
root signature. The final synthetic stress case exceeds that budget; it exposes
the branch/copy overhead when no inactive payload can be skipped. It is retained
and reported, not hidden by a favorable average. These timings do not include allocation, actual driver calls,
GPU execution or the rest of the frame. Real-game benefit depends on how often
the snapshot path runs and the game's actual binding mix.

## Remaining acceptance

Windows canonical Core build, actual D3D12/WARP state replay, NGX inference,
game rendering and frame-time acceptance were not run for this change.
Use the existing command-state GPU harness and default native SR/RR routes,
including F6 toggles, nested capture, multiple command lists and FG. Preserve
the same binary/runtime identities when comparing frame-time measurements.

Related work: #277 (CPU/frame-time overhead), #272/#273 (D20/D21 performance).
This patch does not resolve those game-level issues by itself.
