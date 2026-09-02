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
