# 0.5.2 Beta 12 local experimental candidate

Adds two spatial experiments, both off by default: guided four-tap reconstruction and local NR-edit denoising. They process NR edits rather than replace the NR model, and do not establish temporal denoising. The four-tap experiment can weaken thin edits; it remains available for manual comparison rather than becoming a recommended default.

The requested option, submission status and refusal reasons are reported separately. Unsupported paths keep the ordinary result. Disabled experiments add no GPU passes or texture allocations. Private SR and external temporal history remain separate comparisons.

Retains Beta 11's concise UI, detailed memory sampling, Esc timing markers and private-SR budget preflight. Esc transitions are not claimed fixed. Native RR followed by NR remains distinct from the unavailable NR-to-private-RR path.

Prepares a common external measurement procedure for D13 and the new candidate at one and two NR layers, recording memory, frame times, versions, runtimes and actual workload. Memory savings and whole-frame cost remain unproven until measured. See the delivery record for installation and completed checks.

Cumulative changes since 0.5 D21 remain: independent NR layers, reduced-workload restoration, private-SR and temporal experiments, actual-route and local GPU timing reports, and subsequent lifetime/memory-admission fixes. See the [Beta 10 cumulative D21 notes](052_BETA10_CHANGELOG.en.md). These features do not imply accepted image quality or whole-frame cost.
