# Compatibility Verdict policy (#203)

`src/nr_compatibility_verdict.h` exposes the pure, header-only
`nr_compatibility_verdict::Resolve(const Input&) -> Verdict`. It reads no global
state, chooses no resource conversion, runs no recovery and relaxes no guard.
The caller captures real evidence and formats the returned values into F8.
Verdict owns its text, so input string views need only remain valid during Resolve.

## Integration values

| Input | Producer contract |
| --- | --- |
| `current` | A coherent `Scope{available, config, feature_generation, requested_mode, effective_mode}`. Modes are 1=post and 2=pre. Available means the scope was observed; zero Feature/effective mode is valid during waiting or a real early block, but cannot certify recovery. |
| `first_block` | `available, unresolved, scope, reason, definitive`. Supply the earliest **currently unresolved** block. A resolved historical block must not remain unresolved. Re-observed sticky guards can describe the current scope; do not re-stamp old frame evidence as current. |
| `resource` | `available, scope, admitted, definitive, state, reason, missing_evidence, next_action`. Map the resolver outcome explicitly to `ResourceState`: Ready, UnknownSemantic, QueryNotAttempted, QueryFailed or Rejected. Unspecified stays unknown. A typed exposure rejection is Rejected, not QueryFailed unless an actual query failed. |
| `recovery` | `available, scope, attempted, state`, explicit current `attempt_id`, `proof_attempt_id`, `correlated_window`, descriptor verification, native/NR streaks, matching submissions and writeback verification. `failure_reason/failure_definitive` describe the current failed attempt; `first_failure/first_failure_definitive` retain history. |
| `observer`, `command`, `queue` | Scoped `FactEvidence` with Unobserved, NotRequired, Complete or Incomplete. Missing facts are not considered complete. NotRequired is an explicit current-route fact, not a substitute for missing observation. A current incomplete gate may include its actual reason and definitive flag. |
| `visual` | Optional scoped fact and `proof_attempt_id`. Complete means actual visual verification for that attempt; Incomplete means a known failed check. Unobserved leads to F7 only after complete execution proof. |

`SameScope` compares config, Feature generation and both modes. A producer must
start a new attempt/reset its proof window on failure, resource-contract change,
new retry or scope change. `correlated_window` must never be derived merely from
nonzero process totals: streaks and submissions must belong to the identified
attempt. A changed resource contract needs a new attempt even if config/Feature
values happen to remain unchanged.
Resource `missing_evidence` describes required unresolved proof, not incidental
notes; a nonempty value prevents a successful recovery verdict.

## Decision rules

The current first-block receipt outranks later resource/queue failures and all
success counters. Unknown reason vocabulary maps to `stage=unknown`; the module
does not guess from words such as device, format or exposure. Without an ordered
receipt, multiple distinct active failure sources produce an explicit missing
first-block-order verdict rather than a guessed root cause.

Technical recovery success requires all of: current admitted/definitive resource
evidence, current gate proofs (or explicit NotRequired), current attempt and
correlated proof IDs, an actual descriptor check, at least three consecutive
native successes, three consecutive NR successes, three corresponding submissions
and actual verified writeback. Reported Succeeded alone, one success, stale
scope, old attempt evidence, unavailable state or a failed attempt cannot pass.

`evidenceComplete` means enough to decide the next action. Technical success with
unverified final appearance has `recoveryResult=succeeded`, `stage=visual`,
`nextAction=needs-visual-F7`; it is not visual acceptance. Resource, observer,
command or queue rejection never requests F7. A known failed visual check requests
code work and revokes success. `NotNeeded` is an explicit producer decision and
is not inferred from absent attempts or zero counts.

Verdict contains `stage`, `reason`, `autoRecoverable`, `recoveryAttempted`,
`recoveryResult`, `firstDefinitiveFailure`, `evidenceComplete`, `missingEvidence`
and `nextAction`. Also write `firstFailureHistorical` and `firstFailureScope`
when retaining history, so that it cannot be mistaken for the current cause.
Use overloaded `Name()` for stage/result/action labels; actions are restricted to
none, auto-retry, needs-code and needs-visual-F7. An auto-retry verdict grants no
GPU admission and does not bypass the runtime's bounded retry budget.

## Validation boundary

`scripts/validate-compatibility-verdict.py --compiler <host C++ compiler>
--output-dir <new evidence directory>` directly includes a frozen copy of the
actual header. It covers the #2 waiting/evidence/retry sequence, #202's next
exposure gate, #229 typed exposure versus query failure, observer/queue guards,
failed attempts, stale config/Feature/modes/attempts, historical causes and visual
handoff. Three negative controls weaken the streak requirement, ignore config
scope, or omit writeback verification; each must fail its specific oracle.

The runner records input hashes, source commit/dirtiness, compile commands and
binary hashes. This validates the pure CPU policy, not production evidence
capture, F8 integration, any GPU route, a recovery attempt or game appearance.
