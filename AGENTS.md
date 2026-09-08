# Shared local and cloud handoff

The owner named the ABC compatibility line `0.4.7beta` on 2026-09-09 and kept
the D picture-control line at `0.5`. This isolated D-line candidate integrates
the reviewed 0.4.7 compatibility source at the owner's explicit request.
Preserve both histories and historical version labels. Integration into this
candidate does not authorize changing the separate 0.4.7 branch or merging
into `main`/stable releases. Continue from this isolated integration branch.

For this 0.5 research successor (#168), the sole research target is `next/0.5`. Temporary research PRs target it and must not feed algorithms into `main` or stable 0.4. Maintenance-only instructions below describe that separate line. Read the current 0.5 section first.

Read `docs/interop/CURRENT_STATUS.md` first for the current candidate, supported routes, validation boundaries, and owner handoff state. The sole continuing development branch is `main`. The former compatibility integration branch (PR #133) and maintenance branches are historical integration inputs; start temporary work branches from current `origin/main` and merge them back. Preserve separate unfinished 0.5, Feeder and Manager experiments until explicitly integrated.

Verify the remote HEAD and the candidate's `source-manifest.json` / `validation.json` before citing a commit or test result. Chat history and old roadmap checkboxes are historical context, not proof of current source or a successful build. Do not repeat migration work already present in the ancestry.

Preserve existing worktrees and all uncommitted work. Use an isolated worktree for changes. Record actual commits, commands, logs, artifact hashes, unresolved defects, and the next action in the shared handoff and relevant Issues. A source commit, local validation, hosted CI, and game/NVIDIA validation are separate facts.

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
