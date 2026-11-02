# Contributing

This is the public source repository for the DLSS5 enhancement Core. The project's
original source is MIT-licensed; preserve LICENSE and THIRD_PARTY_NOTICES.txt.
Keep changes reviewable and preserve safe native fallback.

Before copying or adapting code, record its upstream repository, exact revision,
license and affected files. Compatible open-source code is welcome; public
visibility alone does not grant permission to relicense a dependency. Submit
original contributions under the project license and disclose third-party code.

## Never commit

- NVIDIA, ShortFuse, game, ReShade runtime, or other third-party binary files.
- Model weights, kernels, compiled shaders extracted from a runtime, or dumps.
- Raw logs, screenshots, paths, usernames, pointer values, crash dumps, or game
  settings copied from a tester's machine.
- Build output, packaged add-ons, credentials, tokens, or private keys.

## Required behavior

- Preserve native Feature 13/Ray Reconstruction. Private RR experiments remain
  explicitly opt-in research, separate from the default product path.
- A live RR feature handle protects companion feature-1 calls from a second NR
  pass; time-window heuristics are not accepted as release behavior.
- Preserve the existing master-off fast path; do not add model evaluation or
  per-frame allocation when disabled. Shared observers retain their lifetime rules.
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
