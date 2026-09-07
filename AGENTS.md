# Shared local and cloud handoff

Read `docs/interop/CURRENT_STATUS.md` first for the current candidate, supported routes, validation boundaries, and owner handoff state. The sole continuing development branch is `main`. The former compatibility integration branch (PR #133) and maintenance branches are historical integration inputs; start temporary work branches from current `origin/main` and merge them back. Preserve separate unfinished 0.5, Feeder and Manager experiments until explicitly integrated.

Verify the remote HEAD and the candidate's `source-manifest.json` / `validation.json` before citing a commit or test result. Chat history and old roadmap checkboxes are historical context, not proof of current source or a successful build. Do not repeat migration work already present in the ancestry.

Preserve existing worktrees and all uncommitted work. Use an isolated worktree for changes. Record actual commits, commands, logs, artifact hashes, unresolved defects, and the next action in the shared handoff and relevant Issues. A source commit, local validation, hosted CI, and game/NVIDIA validation are separate facts.

After handing the candidate to the owner for testing, follow the latest owner instruction before starting another development batch. No automatic stable release, tag replacement, or merge into main/release is implied by a compatibility test handoff.
