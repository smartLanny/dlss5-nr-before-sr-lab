# F8 evidence and candidate packaging recovery (2026-09-07)

The unfinished F8/packaging draft from worktree `dlss5-nr-before-sr-lab-product-921457ee`
was read without modifying that worktree. Only the feedback include, settings test,
and standalone packager/CI wiring were recovered and revised. Its old WARP regex
changes were already superseded by the reviewed `extract-post-gpu.py` implementation;
its stale delivery README was not copied over current development status.

## Command-state blend observations

`CommandStateEvidence` copies all four native blend factors while holding the same
state lock that samples the dynamic mask, reason, token, and sequence. The observation
is valid only when the tracked `kDynamicBlend` bit is set. F8 start/end and distinct
carrier history export `blendObserved` and `blend`; absence is written as `unobserved`,
and a contended live snapshot as `unavailable`. Default zero-initialized storage is
never presented as measured zeroes. History remains historical, not a frame identity.

These factors are native D3D12 graphics state. They are unrelated to the user's
`requested.blend` NR effect setting. Recording a setter does not prove submitted or
completed GPU work. The existing finite/range gate, Reset provenance, native fallback,
and hard-exception behavior are unchanged. A new F8 TXT can now supply the previously
missing values; the older game logs still cannot establish them retrospectively.

## Post settings in the same F8 TXT

The generated candidate includes `interop/nr_post_feedback.inl` in the existing
`FeedbackAppendActivity`. It records the requested RR state and work percentage,
settings revision/readiness, persistence failure/rejected request, RR attempts,
NR recordings, native failures, and last-attempt age. Counts are explicitly sampled
atomics; a request or recording is not GPU completion, presentation, or game approval.
F8 remains a single named TXT; F7 is an optional separate image/archive capture.

## Packaging contract

`package-multi-api-candidate.ps1` uses the staged manifest version and companion
identity. It requires matching source commits, clean matching documentation/licenses,
all staged source hashes, bilingual/core/bridge/carrier hashes, the unchanged default
INI, and exact post-WARP result/core/template/generated-harness/stdout hashes.
Baseline WARP alone does not satisfy post validation. Each language ZIP includes one
core and the matching companion, with the optional INI under `defaults/` so an existing
user INI is not overwritten. The script does not install into a game or publish a Release.

Packaging tests use synthetic files and validate only this packaging contract; they do
not claim those fixtures are executable. They reject changed source, binary, INI,
post log, generated harness, missing post checks, and mismatched identity before an
output package is published.

## Local verification scope

On Windows with llvm-mingw 20260826, the actual core-feedback coordinator, writer,
real file IO and independent TXT/ZIP inspection passed 113 checks. The native
command-state suite passed WARP, restore negative controls, the 64 KiB worker, and
the stack gate (197 functions at most 4096 bytes each). Added checks cover measured
four-value copying/history, unobserved defaults, observed NULL setter, ClearState
invalidation, and busy snapshots. The post settings seam passed 44 checks.

The multi-API Python suite passed 65 tests with one explicit skip for an unavailable
pinned carrier checkout in this recovery worktree. Full generated bilingual builds,
repinned source identities and candidate packaging must be rerun from the final
integration commit. None of these local tests is NVIDIA Feature18 or game validation.
