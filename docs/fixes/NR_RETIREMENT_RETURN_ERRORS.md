# NR retirement returned-error ownership fix (unreleased)

Refs #106. This is a concrete ordinary-error defect found during the September feedback audit, not proof that #49/#97/#98/#99/#101 share a crash root cause.

The old production DestroyContext cleared the NR handle and freed dependent parameters/textures even when ReleaseFeature returned an error. DestroyParameters errors and missing teardown exports were also ignored. The new path clears each owner only after its corresponding success, retains all remaining dependencies on failure, and uses the existing process-lifetime quarantine. Subsequent retirement calls do not reenter the provider after quarantine. F6 cannot clear the existing lifecycle latch.

No SEH/AV is caught, no exception handler or driver is patched, no blind retry/rebuild is added, and fences/queues/resource admission are not weakened. This does not implement the entire hard-fault/poison state machine in #106. Previously admitted work and real driver fault behavior need separate analysis/runtime testing.

Regression: actual production function extraction with ordinary Release/DestroyParameters failure, missing exports, repeated cleanup, pre-existing quarantine and successful cleanup controls. A pinned historical implementation must fail exact ownership oracles; the current one must pass. Full existing CI remains required.

Risks: conservative quarantine retains the bounded existing pool until process exit and disables further NR admission instead of attempting recovery. FeatureNotFound is not treated as success for an owned NR retirement. This may skip NR after an otherwise recoverable provider error; it avoids guessing ownership. No game/NVIDIA/runtime performance validation has been performed here. No Release or tag is changed by this PR.
