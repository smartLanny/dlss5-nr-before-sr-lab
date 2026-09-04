# Agent handoff

Manager branch: product/glass-quickbar-v0.2 (base 38db68aedad03806816f52c541d036e8bf6d847a).
Core branch: ui/quickbar-control-v1; commit dfb8978fde3d845ccfe79bbe595701fbf0622337 (base e0b94ff1670de82ccd4264c19d4d459fc3c2a160).

Do not merge the product-root branch into the core branch: they are separate project trees in the same private repository. The shared C ABI header is copied in core-integration/ and native/shared/ and must stay identical.

Current state: source implementation + 20 passing Node tests + portable C++ tests + sample-data DOM checks. Quickbar Windows MSVC build/link/CTest passed remotely; see CI_RESULTS.md. Actual core wrapper and Electron binaries remain unbuilt. npm install failed with EAI_AGAIN for registry.npmjs.org. No game or real sensor data was used in the preview.

First job: build the paired core with scripts/build-quickbar.ps1; the separate Quickbar Add-on already passed Windows CI. Check ReShade API20/ImGui19250, input blocking and cursor restoration, Resize/AltTab/DPI/HDR, F8 capture lock and control generation conflicts. The experimental NVML provider loads System32/nvml.dll only, degrades to --, and needs real driver/unload tests.

Second job: produce a lawful matching seven-file payload with prepare-payload.ps1. The initial manager receipt schema is now v2; v0.1 owners must uninstall using their old tool first. Do not improvise silent receipt migration.

Do not call a GitHub workflow 'passed' unless its run result was read. Do not advertise perfect game support or FG display-FPS from the current callback counter.
