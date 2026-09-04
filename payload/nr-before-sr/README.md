# Release payload (not committed)

Put the following **lawfully obtained, matching x64 files** in this directory:

- `ReShade64.dll` — official add-on-enabled x64 ReShade runtime
- `nr-before-sr.zh-CN.addon64` — Chinese NR-before-SR add-on build
- `nrchain_nvngx.dll` — matching bridge build
- `nvngx_dlssnr.dll` — matching NVIDIA neural-rendering runtime
- `nr_before_sr.ini` — default configuration

Then run:

```powershell
node scripts/verify-payload.js --write
```

This writes `bundle.json` with pinned SHA-256 digests. The application refuses
to install a missing or mismatched payload. None of these binaries should be
committed unless their redistribution rights have been reviewed separately.
