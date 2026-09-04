# 0.2 experimental status

Implemented: native ReShade Quickbar; background NVML polling; callback FPS/frametime history; paired core C ABI; schema-aware desktop controls; product-owned file journal; local payload selection; conservative D3D12 installation policy.

Validated: 20 local Node tests, UI ID contract, two portable C++ contract tests and sample-data DOM interactions. GitHub Actions also passed the Windows/Linux manager tests and Windows MSVC Quickbar build/link/CTest. An x64 Add-on artifact was produced. See docs/CI_RESULTS.md for exact code commit, run and digest.

NOT validated: actual core wrapper build, Electron packaging, game loading/input/HDR and real GPU telemetry. This is not a ready-to-distribute gameplay bundle. See docs/VALIDATION.md.

The manager and core adapter remain separate isolated branches in the same private repository. Do not merge the product-root tree into the core. No production shader/hook source is changed by the adapter.
