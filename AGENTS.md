# DLSS5 Core — shared development rules

## Current branch and ownership

- `main` is the sole continuing Core integration branch (0.5.x). #300 is merged;
  0.4 is frozen. Do not recreate a D16/D21 integration line or reset another tree.
  Historical branch names are now archival tags; resolve them through the index.
- Read `docs/interop/CURRENT_STATUS.md` first. #294 is the task/experiment index.
- Before ancestry, merge-base or archive-retirement decisions, check
  `git rev-parse --is-shallow-repository`. A shallow clone is not evidence of
  missing history. Obtain complete history and required tags (for example,
  `git fetch --unshallow --tags origin`), then check again. If history/objects
  remain unavailable, report that limitation; do not classify or retire refs.
- Ownership (owner decision 2026-09-29, #311): the Claude maintenance session
  integrates main (reviewed PR merges), release tags, PR/issue cleanup and shared
  docs. The executing agent (cloud-through-Windows MCP) develops Core on task
  branches, runs Windows builds/packaging/hardware checks, and opens PRs; it does
  not merge main, close others' PRs or create release tags. Coordinate in #311.
- Fetch and inspect current main, relevant Issues/PRs and existing branches before
  coding. Use one short-lived task branch and one PR, normally targeting main.
  At most one explicit dependency layer; never maintain competing "latest" stacks.
- Manager is maintained in `smartLanny/DLSS5-Manager-ZJZ`; this repository owns
  Core rendering, runtime/guide contracts and the Core artifact/ABI handoff.
- Shared rendering/resource/lifetime mechanisms are the architecture boundary;
  game names are reproduction entry points, not speculative hard-coded fixes.

## Implementation and validation

- Fix a coherent supported batch and validate it; do not stop at advice when the
  owner requests implementation. Do not invent an optimization without evidence.
- Preserve native SR/RR and safe bypass. Report persistent bypass with a clear
  request for F8/log feedback. A bypass-induced FPS increase is not a performance fix.
- Preserve per-layer model/history identity, exact command-list/queue/fence and
  resource lifetimes, extent/subrect, formats, jitter, MV, depth, exposure/HDR,
  feature identity and FG ownership. Never replace completion proof with frame age.
- Preserve saved settings. New experiments remain opt-in until separately accepted.
- Use `scripts/build-unified-candidate.ps1` with exact dependency/source pins,
  full face backend and explicit FFX SDK/cache inputs. Do not bypass failed gates.
- Windows verification is primary. Hosted checks remain opt-in through the one
  PR entry and `ENABLE_HOSTED_CI=true`; the owner has not enabled paid CI.
  Do not add automatic push checks or retry a billing-blocked job.

## Evidence and delivery

- Distinguish source review, compilation, CPU, WARP, hardware shader, NGX inference,
  hosted CI and game acceptance. A skipped/failed/unstarted check is not a pass.
- Beta 13 default routes passed the owner's game check (2026-09-29); experiments
  and open issues in CURRENT_STATUS did not. UI1 had only an initial observation.
- Record build SHA, packaging SHA, binary/ZIP hashes, actual commands, receipts,
  failures and remaining tests. A merge tree match does not rename a binary's source.
- Released names are fixed: UI1/Inbox1, `0.5.2-beta.1` … `0.5.2-beta.13`. The
  next behavior change is `0.5.2-beta.14`; never relabel an existing binary.
- Public Core attachments require reviewed payloads, licenses and explicit release
  authorization. An experimental prerelease must not silently replace stable/latest
  or change Manager default component pins. Keep the previous package available.
- Public packages stay Core-only (no NVIDIA NR runtime DLLs) until redistribution
  terms are confirmed; private draft packages may bundle them for owner testing.
- Do not change signing keys, invent signatures or bypass existing verification.

## Preservation and privacy

- The owner accepted the verified F-drive archive as the backup arrangement
  (see #311); a second NAS/device copy is not a current execution gate. Keep
  its limitations explicit and do not retry denied Z/junction operations.
- Preserve other worktrees and all uncommitted/untracked work. Before authorized
  cleanup, save refs, index/diffs and unique files; independently verify archives.
  Git bundles do not back up ignored builds, captures, local edits or release assets.
- Branch retirement is not feature completion. Record original ref/SHA, retained
  source/tests/evidence and successor Issue before closing an old implementation PR.
- Raw feedback, ETL, paths, dumps, vendor payloads and private keys stay private.
  Never transfer a private feedback Issue wholesale into the public Manager repo.
- Public-source export uses a reviewed tracked-file allowlist, not a working-tree
  copy. Follow `docs/REPOSITORY_AUDIT.md`; historical private inbox files are not
  permission to add new binaries or publish them.
- No force push to main, history rewrite, tag replacement, game installation or
  unrelated deletion. Execute only the owner's scoped cleanup/release authorization.

Historical instructions: `docs/history/pre-mainline-20260926/AGENTS.pre-mainline.md`.
Archived source and local recovery keys: `docs/maintenance/ARCHIVE_INDEX_20260926.json`.
Development tasks: #294 index. Integration/maintenance coordination: #311.
