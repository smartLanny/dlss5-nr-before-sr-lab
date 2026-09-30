# Contributing

This repository is currently a private research staging area. Keep changes small,
reviewable, and fail-closed.

## Never commit

- NVIDIA, ShortFuse, game, ReShade runtime, or other third-party binary files.
- Model weights, kernels, compiled shaders extracted from a runtime, or dumps.
- Raw logs, screenshots, paths, usernames, pointer values, crash dumps, or game
  settings copied from a tester's machine.
- Build output, packaged add-ons, credentials, tokens, or private keys.

## Required behavior

- Feature 13/Ray Reconstruction remains strictly unmodified.
- A live RR feature handle protects companion feature-1 calls from a second NR
  pass; time-window heuristics are not accepted as release behavior.
- Disabling the master switch removes per-frame Evaluate hooks.
- If another add-on owns NR, this add-on must not submit Feature 18.
- Failed or ambiguous contracts retain the game's original Color/Output.
- Original NGX parameters are restored on every path.
- `neural == proxy` remains a pixel-identity path in the composition shader.

## Pull request checklist

- Explain the exact engine/resource contract being changed.
- Add a sanitized log excerpt or synthetic test description; do not attach a raw
  log or any runtime binary.
- State source and output physical dimensions, active extents, bases, formats,
  create flags, MV scale semantics, and result codes when relevant.
- Test master-off, Mode 1, Mode 2, feature-creation failure, and RR pass-through.
- Document any unresolved resource-state or GPU-lifetime assumption.
- Run the repository scan described in `docs/REPOSITORY_AUDIT.md`.

## Quick tests (any OS)

`python3 scripts/run-portable-tests.py` runs every `tests/**/test_*.py` with a
host C++ compiler (`CXX`, else `clang++`/`g++`) and prints PASS / FAIL / SKIP.
A SKIP names what is missing (Windows, an optional module such as `numpy`, or a
prerequisite produced by another script) and is not a pass. Use `-k <text>` to
filter and `--verbose` to print failing output. In a cloud or shallow clone run
`git fetch --unshallow --tags origin` first; archive tests need full history.

This does not replace the canonical Windows build
(`scripts/build-unified-candidate.ps1`), which compiles the committed Core source and
runs CPU, WARP and packaging gates.
