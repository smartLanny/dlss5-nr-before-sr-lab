# Repository cleanup continuation — 2026-09-26

## Ownership and scope

The owner asked this executing agent to finish the remaining work, not transfer
execution to the out-of-quota Claude session. The current product remains the
0.5.x mainline established by #300. #308 is a separate unfinished runtime task;
#309 retains residual reconstruction/provider research. Cleanup is not a claim
that either feature, broad game compatibility, black-point reduction or FPS
improvement is finished.

UI1 only has an initial owner observation of apparent effect, not full game
acceptance. Keep its two new controls off by default and keep the Inbox1 rollback.
The reviewed public Core prerelease remains optional; no Manager pin, application
release, game installation, model, INI, signing key or verification policy changes.

## Source retention, not indiscriminate merging

[The machine-readable archive index](ARCHIVE_INDEX_20260926.json) maps all 177
retired remote branch names to exact commits and durable retention points. The
first phase removed 84 already-contained names; the second phase preserves the
remaining 93 as `archive/retired-20260926/...` tags before removing their branch
names. `main` and the #308 head remain development branches. Cleanup's own PR is
short-lived and must be removed after merging its maintenance-only changes.

The index also maps 36 original local worktree HEADs to exact archival tags and
private recovery keys. Tags and bundles preserve committed code; worktree ZIPs
preserve local staged/unstaged changes, untracked files and ignored evidence.
Keeping an old experiment in an archive does not assert that it was integrated
or that a similarly named modern capability is equivalent.

Old Manager histories stay in this private repository's archival tags. Their
presence does not prove feature parity with the public Manager and does not
license exporting private feedback or Core history there. Original PR numbers,
commit subjects and sampled file paths are navigation aids only; some histories
have multiple merge bases. All complete source trees remain available by SHA.

The Claude cleanup proposal is preserved in its original content at
[historical proposal](../history/pre-mainline-20260926/REPO_CLEANUP_PROPOSAL.md).
Its obsolete conflict, branch-count and game-acceptance statements remain history,
not current instructions. #294 and this closeout record supersede them.

## File preservation and removal controls

`scripts/archive-retired-worktree.py` requires an explicit registered linked
worktree, exact HEAD and a matching archival tag. It never selects workspaces by
an unrestricted directory glob. The main checkout, controlling worktree, locked
worktrees and in-progress Git merges/rebases are rejected.

The archive includes regular file bytes, empty directories, the staged and
unstaged binary patches, index entries and the index snapshot. Each archive is
actually extracted into a fresh private directory and every restored file is
checked against its recorded SHA256. Removal is a separate explicit command; it
checks archive/manifest integrity, current source bytes, restored bytes and Git
state again before asking Git to remove that exact worktree. A failure retains
the archive and stops rather than falling back to broad recursive deletion.

Tests also reconstruct a fresh Git checkout and reproduce different staged and
working versions from the archived patches and files. Do not copy an archived
`.git` pointer over a newly created checkout's own binding. Staged patches must
be applied to the frozen baseline before overlaying the final working files;
recorded deletions and untracked/ignored files must be restored from the manifest.

This is a regular-file/Git-state backup, not a filesystem image: it does not claim
ACL or NTFS alternate-stream backup. Reparse points and symlinks are deliberately
rejected, not traversed. Two historical test-output junctions were detected in
Perf7 and Perf9 worktrees. They and their separate C-drive targets remain intact.
A proposed explicit junction-retirement helper was blocked by the tool and was
not created or replaced with another deletion mechanism.

The preferred Z-drive location was explicitly rejected as outside allowed roots.
No UNC/remount/alternate-tool workaround was attempted. F-drive preservation is
valid locally but is not a verified independent-device or NAS backup. Original
raw capture folders, shared SDK/runtime inputs and published OTA originals are
not included in the deletion scope.

## How to recover a retired task

Find the original branch in `ARCHIVE_INDEX_20260926.json`, fetch its preserved tag,
and create a fresh task branch/worktree from the exact commit. Do not recreate an
old integration line as another current product baseline. For committed-only
history that is sufficient. For a modified local checkout, also locate its exact
private recovery key: `worktree.zip`, `manifest.json` and `receipt.json`.

Validate the recorded archive hash before extraction. The ZIP has `files/`,
`git-state/` and its own manifest. Restore only into a fresh directory, excluding
the old `.git` file. Apply `git-state/staged.patch` with the Git index, then restore
the final working file set, including deletions and local evidence. The separate
bundle can rebuild the object database; a bundle alone does not contain dirty or
ignored work. The archive index is not a script authorizing further deletion.

## Verification and development boundary

This continuation changes maintenance tools, their tests and repository records,
not rendering code, shaders, model settings or build-version defaults. The archive
fixture suite validates destructive branches only in fresh temporary repositories:
modified/new files, changed staging, archive/manifest/restored-copy corruption,
locked worktrees, wrong source/ref and nested/existing destinations must prevent
removal. Provenance tests resolve all retained commits/tags and preserve the old
proposal; mainline maintenance checks retain historical content and Windows-first
CI policy.

These are filesystem/CPU/Git checks, not a new Core build, hardware shader test,
NGX inference or game run. The existing public UI1 binary keeps its original
`deba69db` build / `4351ca3b` packaging identities. Cleanup does not bump it to
0.5.2. #308 needs its own source reconciliation, full Windows candidate and game
acceptance; #309 needs actual runtime integration, not only archival recovery.

## Final measured results

Final per-directory counts, logical bytes archived, ZIP sizes and sampled free
space are recorded in the closeout receipt and #294. Logical file sizes, allocated
disk space and free-space changes are different measures; do not label them as
the same saving. Private full manifests and original failure logs remain in the
F-drive final-pass archive rather than being published in the Manager release.
