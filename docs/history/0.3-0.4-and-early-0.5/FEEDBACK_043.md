# Layered feedback for the 0.4.3 candidate

This describes PR #56, stacked on the unmerged PR #48. It is not a released
0.4.3 build. Keep the source commit and add-on hash when comparing candidates;
the maintainer will freeze release identity and signatures after integration.

## Player workflow / 玩家入口

F8 / 采集核心反馈包 is the recommended first step for bypass, no effect and
compatibility reports. It records bounded CPU state and log summaries, with an
8 MiB ZIP ceiling. It does not request screenshots or GPU readback, turn on NR,
switch rendering modes or write picture settings. Once a Present has queued
the worker, zero successful NR frames and no further Present do not prevent
completion. The observer samples for about 750 ms; log processing follows.

F7 / 采集图像补充包 is for color, lighting, flicker and detail problems. It first
creates a new core package, then uses the existing exact-fence image collector.
Submit both new paths shown in the UI. Both ZIPs may be named feedback.zip,
but their directories and session metadata distinguish them. A paired request
is not a same-frame claim. Display screenshots and internal NR frames retain
their existing separate identities. The image package is not limited to 8 MiB.

F6 is reserved for the master toggle; F7 is reserved for images. The configurable
core shortcut rejects these two values and uses F8 instead. The permitted key
can still be changed explicitly in the UI.

取消 / Cancel retains an explicit partial or failed result. A new request clears
the old path immediately. Queueing, allocation, output-directory and config-lock
failures never report the previous ZIP as the new result. A busy config lock
produces snapshot.valid=0 and a missing record rather than blocking rendering.

## Evidence and limits

The core contains build identity, add-on hash, executable leaf name, GPU/API/
driver, dimensions, requested settings, historical applied NR parameters,
start/end counters and available guide/exposure/jitter metadata. Historical
applied values are not proof that the current frame ran NR. Unavailable FG/MFG
state is reported as unavailable, not inferred from displayed frame rate.

Known log locations are summarized into startup, first distinct events,
recent important events and tail windows. Each summary is capped at 256 KiB.
A prefix scan is capped at 32 MiB per source and shares a three-second scanning
budget; large inputs also get a bounded tail. scan.complete=0 explicitly means
that the first error may lie outside the scanned range. Synchronous filesystem
stalls are not an absolute wall-clock deadline guarantee.

Excerpts are ASCII so truncated multibyte lines cannot create invalid text.
Non-ASCII free text is therefore lossy. Local Windows paths, pointer-sized
values and recognizable sensitive lines are removed. This is not a universal
privacy guarantee for arbitrary third-party logs: inspect before sharing. Raw
logs remain unchanged on the player's machine. Core packages exclude images,
full configuration files, runtime/model bytes and crash dumps. No automatic
upload is performed.

F7 retains the existing image budgets, missing-data and restoration policies.
Only an explicitly selected comparison temporarily changes settings. Native
final-SR and complete motion/depth pixels are not newly provided by this PR.

## Verification

Use `python scripts/parse-core-feedback.py core.zip` and optionally append
`--image image.zip`. The parser validates byte budgets, member names, CRC,
SHA256, manifest consistency, session and build pairing without extraction.
Byte consistency does not establish official authorship or game compatibility.
The existing parse-f8-bundle.py remains available for legacy image packages.

The Windows test includes the actual core coordinator, log-file IO and ZIP
writer. Scheduler, runtime properties and NGX counters are test seams, not
real-game validation. Cases cover zero NR, no later Present, cancellation,
busy snapshots, worker/PIN/directory failure, stale paths and archive budgets.
Portable tests cover summary retention, bounded memory, reserved shortcuts and
malformed archives. Latest PR Checks determine compile/CI status.

## Integration and remaining scope

Integrate #48 before the incremental #56 changes. Do not merge the independent
Manager product tree, DX11 research or FG drafts into the core as part of this
feedback change. Shaders, render sizes/formats, RR/NR ownership, queue/fence
contracts, unknown HDR admission and the default null-PSO restriction remain
unchanged.

Before release, test F8 with NR on/off and continuously bypassed; F7 current
and explicit comparison; cancellation/retry; simultaneous hotkeys; settings
changes; language/DPI and read-only output paths. Confirm paired identities,
restoration and the absence of new image requests on the core-only path.

Collect-Feedback.cmd and Collect-Crash-Feedback.cmd retain their independent
legacy rescue format. They are not covered by the runtime F8 small-package
promise. Issue #47 remains open for release-wide documentation/CMD alignment
and game verification. The driver crash and null-PSO game reports also remain
open: improved feedback is not a fix for their underlying rendering contracts.
