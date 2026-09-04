# Product decisions for the foundation

- No account or login in the base product.
- The author identity is shown as `by 野生的装机宅`, not as a fake user account.
- No independent HUD. The core already depends on ReShade, so the existing
  ReShade overlay remains the lowest-risk in-game UI.
- No second game-detail page. Each game expands in place.
- No fake `正常运行` claim. The manager reports `组件完整` until a future,
  verified runtime telemetry channel exists.
- No Feeder, OptiScaler, emulator, x86, Vulkan or OpenGL route in the first
  product base. Their code can be added later as separate adapters.
- No drive-wide scan by default.
- No binaries or proprietary runtime in source control.
