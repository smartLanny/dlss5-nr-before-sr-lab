# Architecture

## First-principles boundary

The desktop manager owns only four jobs:

1. find local games;
2. install, repair and safely remove a verified payload;
3. edit `nr_before_sr.ini` in beginner-friendly terms;
4. show honest file-level diagnostics.

It does not replace ReShade, inject a second overlay, log users in or claim
runtime success without telemetry. The NR add-on remains responsible for the
rendering chain and its existing ReShade in-game controls.

## Layers

- `vendor/DLSS5-Swapper/`: pinned MIT-licensed upstream dependency.
- `src/core/` and `src/library.js`: thin adapters over the pinned dependency.
- `src/product/`: product-specific policy and stable extension points.
- `src/renderer/`: one-page Chinese desktop UI.
- `payload/`: untracked release inputs plus a hash manifest.
- `product.json`: branding and community links without changing application code.

## Extension points

Future features should be added behind small modules rather than directly in
`main.js`:

- compatibility database: add a provider beside `game-support.js`;
- runtime telemetry: add a read-only status provider, then clearly distinguish
  `组件完整` from `正在运行`;
- update channel: add a signed manifest provider outside the game process;
- optional licensing: keep it in the desktop process and use signed offline
  entitlements; never put a server secret in Electron or the game DLL;
- additional install routes: implement separate route adapters without changing
  the native NR-before-SR installer.
