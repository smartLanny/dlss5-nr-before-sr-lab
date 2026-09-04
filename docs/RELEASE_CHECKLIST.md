# Release checklist

1. Confirm the pinned upstream commit and run `npm run verify:vendor`.
2. Build the matching Chinese add-on and bridge from reviewed NR source.
3. Confirm redistribution rights for every payload file.
4. Run `scripts/prepare-payload.ps1`; never hand-edit `bundle.json`.
5. Run `npm test` on Windows.
6. Test a fresh install, repeat install, repair and uninstall in disposable game
   folders containing both an existing Add-on ReShade and an unknown `dxgi.dll`.
7. Test at least Cyberpunk 2077, Control and Hogwarts Legacy with the game closed
   during file operations.
8. Confirm the UI says `组件完整`, not `正常运行`, unless verified telemetry has
   actually been implemented and tested.
9. Build both installer and portable artifacts and publish SHA-256 checksums.
10. Preserve `LICENSE`, `UPSTREAM.md`, `THIRD_PARTY_NOTICES.md` and the upstream
    ReShade/DLSS5-Swapper license files in the distribution.
