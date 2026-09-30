# D19 R3 continuation and owner acceptance

R2 and the developer controls were checkpointed as
`4542731470b48ba2493aed22fd8bec856a6fc439`, whose direct parent is the D18
review ancestor `305074d6f786064071fda24ac7f81d99645ab338`.
R3 input archive SHA256:
`348f6adf2bb1fdc6050b0cd81d052106f4206d70c5ead12f1608a33cb8ac2de3`.

`stage_increment.py --component all` identified seven locally extended R2
files. Each was compared with the R2 snapshot before declaring its ancestor.
Two overlapping edits were resolved locally: startup sealing plus descriptor
profiling, and motion compensation plus advisory consumer hints. Both
resolved files passed a repeat R2-ancestor three-way merge without changes.
No R2 rollback, directory replacement, or old #236 branch merge occurred.

## What remains and what changes

Address lookup indexes, descriptor-copy batches, real owned guides, immutable
receipts, geometry inference, configuration/UI persistence, startup-only low
overhead mode, and conservative/forced motion compensation remain.

R3 treats absent or incomplete consumer information as a reason to continue
observing, not a permanent failure. Any suitable consumer permits snapshot
preparation. Exact admissible receipts are selected before inferred scaled
ones; ambiguity still rejects within each tier. Heap membership indexes
bound retirement and unknown-range invalidation to relevant slots.

Only the #236 wait helper is imported: parent blob
`300ae92b81217121790a24429f6b89d93d3aed3f`, candidate blob
`8f96c99d1af2f4b5e9b034a60950129704ae9080`. An earlier event on the same fence
may be followed by registration of the exact requested target under the
original timeout budget. Notification alone is never completion proof.

## Two build variants

The full D18 generation entry produces one canonical source snapshot.
`scripts/build.ps1` builds regular bilingual Core into `core/build` and,
with `-DescriptorProfile`, measurement Core into `core/build-profile`.
Each directory records its compiler profile flag and actual artifact hashes
in `build-variant.json`; variants must not overwrite each other.

Regular uses `NR_DESCRIPTOR_PROFILE=0`; measurement uses 1. Defaults retain
full GPU descriptor mirroring, conservative layer motion units, and all
existing inference sizes, strengths and pass counts. Developer experiments
are available but should remain at defaults for the initial comparison.

Measurement F8 includes batched metadata CPU times, visit counts and profile
state. Times include metadata lock acquisition, exclude the original D3D12
call, and are not GPU time or additive frame time. Disabled measurements are
unavailable, not measured zero. Neither variant claims a game FPS gain.

## Concentrated owner tests

1. Use regular Core first. In Onimusha, keep the previous display/scene and
   settings; select Present and compare F6, then a static view, slow pan,
   fast turn, occlusion and HUD. A 4K Present at 100% is still 4K NR.
2. In a suitable game, start with DLSS disabled. Enable it after 30 seconds
   and after 5 minutes; cover F6 initially off and initially on, then DLSS
   off/on, quality changes and resize without a restart or extra unlock.
3. Recheck the previously working exact Present route in Cyberpunk, NTE's
   three routes, Onimusha before/after, and the existing typed/typeless case.
   Keep ZZZ's mihoshade loader location.
4. Watch Dogs: same scene/display/limits and DLSS settings; compare ReShade
   only, addon initially F6 off, F6 off after successful NR, and NR on.
   Capture 60 seconds three times per condition. If off remains slow, use
   measurement Core and send new F8 plus the raw frame-time CSV files.
5. A real RR scene: keep R/N/O, model, precision, FG/Reflex and limits fixed;
   compare D18 and this candidate with effective NR adoption maintained.
   Do not increase workload merely to reach a GPU utilization target.
6. Repeat F6/route changes, resize and exit with pending work; report the
   first new failure and one new F8, not another copy of existing logs.

Proposed frame-time targets and full owner procedures remain in the supplied
R3 acceptance document. #252 and #234 remain game-unverified until these new
measurements exist. Win32 event tests use real Windows waits but a CPU fence
fixture; WARP, NVIDIA controlled hosts and games are separate evidence.
