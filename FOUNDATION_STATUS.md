# Foundation status

## Ready in this revision

- Chinese-first, dark, single-page desktop UI;
- Steam / Epic / GOG and manual-folder discovery;
- x64 DirectX 11/12 + native DLSS compatibility gate;
- payload SHA-256 validation;
- transactional install, repair and uninstall;
- unknown `dxgi.dll` conflict refusal;
- preservation and restoration of pre-existing ReShade/runtime files;
- beginner settings mapped to `nr_before_sr.ini`;
- file-level diagnostics and copyable support text;
- product branding/community links centralized in `product.json`;
- pinned DLSS5-Swapper submodule with explicit update boundary;
- Windows CI, eight passing unit tests and a passing UI contract check.

## Needs release maintainer input

The repository intentionally does not contain redistributable runtime binaries.
Before a usable installer can be built, prepare the five payload files described
in `payload/nr-before-sr/README.md`, verify their redistribution status, and run
`scripts/prepare-payload.ps1`.

## Runtime claims

The desktop app can verify that files are present and match the pinned payload.
It cannot yet prove that Feature 18 executed in a running game. The UI therefore
says `组件完整`, not `正常运行`. A future read-only status channel is documented
in `docs/ROADMAP.md`.
