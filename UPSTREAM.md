# Upstream relationship

This project is a focused downstream edition of
[`rakanki911/DLSS5-Swapper`](https://github.com/rakanki911/DLSS5-Swapper),
which is maintained by Rakan Alkhaldi and released under the MIT License.

Baseline used for this foundation:

- upstream repository: `rakanki911/DLSS5-Swapper`
- upstream commit: `ccb67f4bc92cb5da1d25416dd6b338e12fa432c5`
- baseline date: 2026-09-05

The downstream intentionally keeps only the reusable local-library discovery,
PE inspection, game scanning, process guard and file-journal foundations. Its
product UI, payload contract, NR settings layer, install/repair workflow and
Chinese beginner-facing copy are maintained separately.

See `docs/UPSTREAM_SYNC.md` before importing a later upstream revision.
