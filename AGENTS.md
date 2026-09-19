## Active 0.5 quality workflow (2026-09-20)

For this quality branch, read `docs/interop/QUALITY_HANDOFF.md` first. It
supersedes historical main-only and colour3/4 experimental-default guidance
below. Continue the existing cumulative quality PR #289 rather than opening a
new stacked PR for each knob. `next/0.5` remains the integration target; do not
merge unverified quality code into main/stable. Performance and Windows GPU
work keep their own owner; #294 coordinates the combination.

Owner feedback is a delivery gate: RGB modes currently add contour halos and
are research-only; colour-priority + D21 composition is the new-config baseline.
Preserve explicit existing INI choices and warn; do not silently migrate them.
The N2 + legacy-compositor recipe is an unvalidated combination. Preserve all
old binaries, logs, native model parameters, work sizes, and runtime boundaries.
A completed milestone requires reproducible Windows build artifacts and stated
GPU/game evidence, not just a source commit or large CPU assertion count.

# Shared local and cloud handoff

The owner named the ABC compatibility line `0.4.7beta` on 2026-09-09 and kept
the D picture-control line at `0.5`. This isolated D-line candidate integrates
the reviewed 0.4.7 compatibility source at the owner's explicit request.
Preserve both histories and historical version labels. Integration into this
candidate does not authorize changing the separate 0.4.7 branch or merging
into `main`/stable releases. Continue from this isolated integration branch.

## Shared-mechanism compatibility fixes

Treat game names as reproduction and regression-test entry points, not as the
architecture boundary. Fix shared rendering, command-list, queue, resource,
format or lifetime contracts once so all titles with that contract benefit.
Do not accumulate per-game branches, allowlists or guessed engine exceptions
when the evidence identifies a common mechanism. Use mechanism names for new
modules, tests and integration PRs; preserve the exact historical identity of
an already-running owner test artifact. Keep independent installer packaging
moving while a separate compatibility mechanism is under investigation.

For this 0.5 research successor (#168), the sole research target is `next/0.5`. Temporary research PRs target it and must not feed algorithms into `main` or stable 0.4. Maintenance-only instructions below describe that separate line. Read the current 0.5 section first.

Read `docs/interop/CURRENT_STATUS.md` first for the current candidate, supported routes, validation boundaries, and owner handoff state. The sole continuing development branch is `main`. The former compatibility integration branch (PR #133) and maintenance branches are historical integration inputs; start temporary work branches from current `origin/main` and merge them back. Preserve separate unfinished 0.5, Feeder and Manager experiments until explicitly integrated.

Verify the remote HEAD and the candidate's `source-manifest.json` / `validation.json` before citing a commit or test result. Chat history and old roadmap checkboxes are historical context, not proof of current source or a successful build. Do not repeat migration work already present in the ancestry.

Preserve existing worktrees and all uncommitted work. Use an isolated worktree for changes. Record actual commits, commands, logs, artifact hashes, unresolved defects, and the next action in the shared handoff and relevant Issues. A source commit, local validation, hosted CI, and game/NVIDIA validation are separate facts.

## Owner performance feedback loop (2026-09-18)

For this performance-repair series, the owner explicitly requests an execution
loop, not report-only iterations. When a new test result supplies a supported
repair direction, continue in the same task through a coherent repair of the
supported related hot paths, relevant regressions, Windows build, and test-candidate delivery, then report
what to test next. Do not stop at the investigation report to request another
routine authorization. A specific diagnosis-only request overrides this default.
Do not invent a patch without evidence, hide failed/skipped checks, or require
another full capture matrix when the current evidence is enough. Preserve exact
build identity, image workload, safety/restore contracts and independent branches.
The owner further requests whole-path batches rather than repeatedly delivering
one isolated micro-optimization. Inspect and repair the related identity, lookup,
state and batch costs together when supported, but do not invent unrelated work
or equate synthetic work reductions with an observed game-frame improvement.

After handing the candidate to the owner for testing, follow the latest owner instruction before starting another development batch. No automatic stable release, tag replacement, or merge into main/release is implied by a compatibility test handoff.

## Protect private evidence

- Keep `bug-inbox/`, feedback archives, raw logs, captures, dumps, local paths,
  vendor payloads, binaries, credentials, and signing private keys out of any
  public-source export.
- Before publishing or pushing a new file category, run the checks in
  `docs/REPOSITORY_AUDIT.md` and inspect the staged diff.
- Do not copy the working directory to create a public repository. Export an
  explicit tracked-file allowlist from a reviewed commit.

## Use CI deliberately

- Run the smallest relevant local gate before pushing. Batch related commits
  into one push when practical; do not push progress-only commits to refresh a
  pull request.
- Pull requests use `.github/workflows/pull-request-validation.yml` as their
  single automatic entry point. Specialist workflows are reusable or manual;
  do not add another direct `pull_request` trigger to them.
- Avoid validating the same development commit through both `push` and
  `pull_request`. Reserve automatic `push` validation for protected integration
  or release branches where a post-merge check is intentional.
- Every new automatic workflow needs narrow branch/path scope, read-only default
  permissions, and branch-normalized concurrency with stale runs cancelled.
- If a GitHub-hosted job has no steps and reports a billing or spending-limit
  error, record it as "CI not started", rely on logged local gates temporarily,
  and do not rerun it until billing is fixed.

## Report evidence accurately

- Distinguish local, GitHub-hosted, WARP, hardware, and in-game results.
- A skipped, cancelled, billing-blocked, or unstarted job is not a passing test.
- Do not publish releases, change repository visibility, rewrite history, or
  remove evidence without an explicit maintainer request.
