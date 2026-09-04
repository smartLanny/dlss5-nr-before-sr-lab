# 0.2 experimental status

Implemented source: native ReShade Quickbar; background NVML polling; callback FPS/frametime history; paired core C ABI; schema-aware desktop controls; real product-owned write-ahead journal; local payload selection; conservative D3D12 installation policy.

Validated locally: 20 Node tests, UI ID contract, two portable C++ contract tests and sample-data DOM interactions. Native Windows builds, Electron packaging, game loading/input/HDR and real GPU telemetry are NOT validated. See docs/VALIDATION.md. This is not a ready-to-distribute gameplay binary.

The project remains in an isolated product branch; it is not a separately created GitHub fork. The core adapter is intended for a second isolated branch based on reviewed beta0.4.1. No production shader/hook source is changed by the adapter.
