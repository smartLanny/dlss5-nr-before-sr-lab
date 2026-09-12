# 100% / 50% OF continuation and repository boundary

This continues PR #250 at `919d9ff4720003b171353cbe195e05009f32406a` on the owner's
D14 base `49b0deb0d09b0a442784e0ebf1073f9498eaf157`. It replaces the initial no-resize
experiment requirement with exactly two explicit OF scales: 100% default and 50%.
Private main was also checked at `9577f1cad328eed5bdff98aef82af89dcda119a6`; it does
not replace the owner's D14 integration target. No active worktree was changed.

## Implemented increment

Production probe: two-backend preparation, exact rounded dimensions, area-overlap
filter for the OF copy, source-pixel motion conversion, UI selector and safe session
retirement/recreation. AMD remains the default. Captured source and raw depth are
not resized. Native Core and all Feeder/host packages are unchanged. A missing Feeder
cannot become a new loader dependency from this increment: none is imported.
This is still a guide-only probe, NOT the D14 external NR consumer implementation.

Manager authority is now `smartLanny/DLSS5-Manager-ZJZ`. Its public baseline was read
at `8895d2b5358c38cc0d795e5e432052e7f8f494a7`, an MIT TypeScript/DLC planning shell.
Do not copy older private Electron Manager trees or payloads into that public tree.
Use the public bridge-DLC issue for installation contracts; keep #239 for actual
GPU/Core integration. Existing Feeder transport must be reused rather than rebuilt.

## Local evidence in this increment

- CMake Release + CTest: 2/2 executables passed.
- Existing actual policy: 169 assertions; its four deliberate defects detected.
- Actual geometry/root constants plus independent CPU area oracle: 29,141 checks;
  Clang17 C++17 Wall/Wextra/Werror/pedantic with ASan+UBSan also passed.
- Three geometry mutations were compiled and hit their exact expected assertion:
  missing vector ratio, wrong odd-size rounding, wrong NVIDIA fixed-point units.
- Shader text included in a CPU test is explicitly NOT shader compilation.

No Windows/MSVC addon link, HLSL/FX compile, WARP, hardware OF, NR, real-game output,
performance or hosted-CI pass is claimed. ROG plugin discovery returned no available
execution action in this session; no installed game, driver or Manager was changed.

Next: compile this exact source on Windows, validate fixed GPU motion pairs and
scale transitions, then connect the proven source/depth/motion pair to the current
D14 consumer seam. Preserve current native processing when no provider is ready.
Do not present the last completed preview as a current-frame neural result.
