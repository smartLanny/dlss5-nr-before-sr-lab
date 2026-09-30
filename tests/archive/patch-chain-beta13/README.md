# Beta 13 patch-chain tests

These files preserve tests for the former staged transformations and fixed
pre-fold source anchors. They are historical evidence, not gates for the
maintained `src/` tree. To rerun one, check out the pre-fold commit
`12a9dd50` in a separate worktree and execute the same test at its original
`tests/` path there. These archived copies retain the original relative-path
assumptions for that checkout. The portable test runner excludes this directory.

Current behavior checks are in `tests/test_folded_source_contracts.py`,
`tests/test_reconstruction_runtime.py`, `tests/test_product_identity.py`,
and the existing CPU/GPU source harnesses. The final-product checks read the
maintained `src/` directly and derive the version from
`scripts/core-source-layout.json`.
