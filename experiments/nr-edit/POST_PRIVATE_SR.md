# Owned post-NR private SR backend

This component connects the existing post-final-residual transport to the actual
private Feature1 lifecycle. It is an experimental library with executable GPU and
NGX tests, **not yet a callback in the shipped game Core**. No new UI switch,
game hook, native Evaluate, feature18 owner, default or product version is added.
The active rendering baseline remains main; #309 owns the integration work.

## What is now connected

`nr_post_private_sr_dx12::Record` accepts a same-source pair B/Z: clean native
output B and the final controlled NR candidate Z, already at the same output
extent and proved to share an SDR-encoded FP16 domain. Z may include every NR
layer; the backend does not subtract only the final layer or rerun the earlier
layers. It records exactly this sequence:

```
Area(Z - B) at N -> bounded FP16 carrier + FP32 quantization tail
              -> owned Feature1 N-to-O Evaluate
              -> decoded edit + tail + immutable B -> target
```

Only its own successful Evaluate can provide the reconstructed texture and
success token to the compose step. Feature creation is not reconstruction:
the first recording leaves the existing Z target unchanged and awaits exact
creation submission/completion. Invalid inputs, ordinary capacity pressure and
unsuccessful composition retain Z. An uncertain vendor/submission/discard failure
poisons the private context and retains its referenced resources; the caller must
quarantine/report it, not immediately release or pretend it is an ordinary bypass.

The module allocates three independent carrier/tail/reconstruction slot sets but
only one private SR feature/history. Each recording has its own parameter block
and immutable shader packet. GPU fence completion **and** retirement of the exact
recording must both hold before slot reuse. No fixed frame-age release, CPU wait,
queue flush or unbounded texture cache is introduced. The reported allocation
budget counts the nine scratch planes using real D3D12 allocation sizes; it does
not claim to include opaque NGX, descriptor or driver allocations.

## Producer contract and current restrictions

The caller supplies original-API ownership and a mandatory recording-retirement
callback. Their storage and referenced loader modules must outlive the context.
The caller serializes CPU access and owns its complete native command-state scope;
NGX and the shader dispatcher may overwrite root/descriptor/pipeline state.

Only a single-node device and a proven direct queue are accepted. Input color,
output color and their subrects are distinct where required; target may alias Z
only at Z's exact rectangle. Target must already contain the fallback candidate.
Depth/MV are non-aliased, known-state, typed, zero-origin N-sized guides with a
proved same source. Merely matching texture dimensions is not proof of their
units or phase. Post-SR zero jitter and normalized N-grid motion units must be
explicitly proved by the producer; this backend does not reuse raw game jitter.
Autoexposure, jittered-MV and unreviewed creation flags are rejected. Valid signed
motion scales are retained; nonfinite/zero scales, stale sources, failed native
results, mismatched encoding/guide sources and unknown domains do not record work.

View, configuration, encoding-generation and carrier-range changes reset private
history. A skipped predecessor fails the existing motion-reference match and also
resets history. Caller reset information remains necessary; repeated source or
same-recording adoption cannot run the feature twice. Opaque model/internal state
and the original game's history must never be shared.

These producer contracts are intentionally not fabricated from the old F8 sample
or from an available resource pointer. The production connection must identify
B/Z after the latest final color/light/region/reconstruction policy, preserve
all layers, freeze the paired guide/phase evidence and register this context in
the actual recording submission/cancellation/retirement path. Existing legacy
`TryProcess`/`stage_edit_integration.py` is a pre-native Deferred experiment, not
an interchangeable post-final callback. Do not deploy that overlay over a new
Core and call it completed post-residual SR.

## Verification

Run `tools/validate_post_private_sr.py` with an explicit compiler and fresh output.
It compiles the actual post shaders, owned module and test host. Default tests use
real WARP pixels with a labelled API fixture; `--nvidia` repeats on NVIDIA hardware.
The fixture verifies the first creation cannot compose, same-source/guide/domain
admission, signed edits, zero edits and tiny FP16 tails, unchanged B and alpha,
candidate aliasing, reset on encoding changes, three held independent slots,
backpressure/recovery and no release before recording retirement. Wrong submission
queue and discarded recording are separate child processes; uncertain resources
are left to process exit instead of unsafe destructors.

Two independently compiled negative controls remove (1) the Evaluate prerequisite
and (2) the post-phase prerequisite. They must fail at the intended runtime
assertion, not at compilation. The test-only altered sources are in the private
output directory, never production macros or defaults.

Supplying both `--nrchain` and `--sr-runtime` copies the explicitly selected existing
DLLs into that test directory, records their hashes and runs actual Feature1. It
uses the shared checked Microsoft x64 parameter ABI and original `_nvngx` exports.
It never downloads a runtime or installs into a game. Static artificial B/Z,
depth and zero motion are used. Each of five signed/neutral cases runs four
actual evaluations. Zero and small tail-only edits require exact FP16 final
pixels despite private SR's carrier drift. Large signed edits permit a declared
0.075 per-channel absolute error; this is **not** image-quality acceptance.

The prior residual shader/transport suite separately covers 60 nonzero-atlas,
odd-size, 25-to-100%, nonfinite, quantization and identity scenarios, plus private
lifecycle CPU tests. Those analytic reconstruction results must not be presented
as real Feature1 measurements. Final receipts distinguish both scopes and include
exact source and binary hashes. Initial test failure for a tiny edit used range=1
(which represented the edit in the carrier); correcting the intended tail-only
fixture to range=4 is recorded, not a relaxation of the production mathematics.

## Previously dirty archive reconciliation

The three modified files and one untracked script in the preserved
post-residual worktree were compared with current main. All four match current
tracked files exactly; nothing needs to be copied again:

- `scripts/validate-colour-nonzero-gpu.py`
- `tests/colour_nonzero_gpu.cpp`
- `tests/compat_present_ngx_host.cpp`
- `scripts/validate-nr-preset-observation.py`

Their historical dirty/untracked state was real and the archive remains intact;
it is no longer an outstanding migration blocker. These are observation/tests,
not proof that a model consumes a parameter merely because Set/Get echoes it.

## Still required before product adoption

The missing production callback/guide-phase integration is real development work,
not something to mark complete because this host passed. It stays in #309 and
must be implemented against current main, with same-source native-output evidence.
Only then can controlled full-Core tests and actual game regressions determine
quality/cost, HDR domain extensions, multi-layer color fidelity, camera cuts,
occlusion, dynamic extents, FG and multi-GPU behavior. No current OTA contains
this new backend. It is not private residual RR and does not fix model black spots
or the other plugin's reported layered ghosting by itself.
