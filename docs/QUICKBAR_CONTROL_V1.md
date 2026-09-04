# Quickbar control V1 · experimental

Based on beta0.4.1 commit e0b94ff1670de82ccd4264c19d4d459fc3c2a160. Original nr_before_sr.cpp and rendering files are unchanged. This adds a build wrapper and two x64 plain-C exports for the manager branch product/glass-quickbar-v0.2.

Build with scripts/build-quickbar.ps1, using the same compiler/SDK variables as build.ps1. The script refuses a changed nr_before_sr.cpp blob. Quickbar itself is built separately with MSVC against ReShade6.8.0/API20/ImGui1.92.5.

GetState copies configuration/status without INI reads. SetValue accepts only known finite scalar settings, checks expected generation, observes the F8 capture lock, then calls existing ApplyConfig. Strength updates TransferStrength and PostTransferStrength together. It does not write private ImGui tables or hook another add-on.

F6 and F8 keep their existing functions. Quickbar defaults to F10; Home remains the ReShade fallback. Submitted command count does not prove displayed neural rendering. No GPU resources, keys, credentials or game pixels cross this ABI.

A portable mock-core contract test was run in the paired manager project. The actual Windows core wrapper build and in-game behavior have NOT yet been validated. Do not publish a gameplay binary until Windows compile, capture-lock, input, HDR and gameplay tests pass.
