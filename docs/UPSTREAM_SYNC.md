# Updating from DLSS5-Swapper

The foundation pins upstream as the Git submodule `vendor/DLSS5-Swapper` and
keeps product policy behind thin adapters under `src/core/` and `src/library.js`.
The pinned baseline is recorded in `UPSTREAM.md`.

Before updating the submodule:

1. inspect the upstream diff for these consumed files:
   - `src/library.js`
   - `src/core/pe.js`
   - `src/core/emulators.js`
   - `src/core/feeder-release.js`
   - `src/core/file-journal.js`
   - `src/core/scan.js`
   - `src/core/install-guards.js`
2. do not silently import Overlay, Feeder or OptiScaler product policy;
3. update the submodule commit and the commit named in `UPSTREAM.md`,
   `THIRD_PARTY_NOTICES.md` and `src/product/constants.js` together;
4. run `npm run verify:vendor` and `npm test` on Windows;
5. run install/repair/uninstall against a disposable test directory.

Product-specific changes should normally be implemented in `src/product/`
instead of editing the submodule. If an upstream file must be patched, first
create a dedicated upstream fork so the divergence remains visible.
