# Branching and release policy

This repository uses `main` as the canonical integrated source branch.

## Canonical branches

- `main` — current integrated source of truth. New ordinary fixes and features branch from the latest `main` unless an issue/PR explicitly documents another base.
- `release/0.4` — maintenance line for the released 0.4.x family after mainline normalization. Only low-risk compatibility fixes, release identity changes, documentation, and release automation belong here. Accepted 0.4 fixes must also be carried forward to `main` when `main` has moved beyond the 0.4 line.
- `next/0.5` — integration branch for the 0.5 architecture/research program. Feature/research branches may target this branch when they are not ready for `main`.

## Development branches

Use short-lived branches with intent in the name, for example:

- `fix/<issue-or-topic>`
- `feature/<topic>`
- `research/<topic>`
- `release/<version>-finalize`

A branch is a workspace, not a release. A Pull Request is the review/CI/merge boundary. Tags such as `beta0.4.3r3` or `beta0.5.0` identify released snapshots and must not be moved after publication.

## Pull request rules

1. Read the latest target branch before development.
2. Keep unrelated fixes in separate PRs unless they share one proven root cause and regression surface.
3. Do not merge research/Draft PRs merely to reduce branch count.
4. Before merge, distinguish source-level, compile, CI/WARP, and real-game runtime validation.
5. Unknown resource, queue, command-state, format, subrect, HDR, RR/SR ownership, or lifetime contracts remain fail-safe/bypass paths.

## 0.4 maintenance

`beta0.4.3r3` is the last release created from the historical `codex/beta0.4.2` line before normalization. After normalization, do not create new ordinary development directly on `codex/beta0.4.2`.

For a future 0.4 maintenance release:

1. branch from `release/0.4`;
2. implement and validate the fix in a PR back to `release/0.4`;
3. carry the same accepted fix forward to `main` when needed;
4. freeze version identity in a dedicated release-finalization PR;
5. publish an immutable release tag.

## 0.5 development

Large 0.5 work should use an umbrella/RFC plus independently testable issues. Each implementation lives on its own branch and normally opens a Draft PR to `next/0.5`. Do not use one permanent mega-branch or one mega-PR for unrelated provider, DX11/DX12 transport, render-scale, resampling, RR, and FG/MFG work.

## Historical branches

Old `codex/beta0.3*`, `codex/beta0.4.1`, `codex/beta0.4.2*`, and one-shot release/finalize branches may remain temporarily for traceability while open PRs are audited. They are not canonical bases for new work unless a PR explicitly documents why.
