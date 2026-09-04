# 0.2 design decisions / research

## ReShade stays, its main window is not the primary UX

Use the public `reshade_overlay` event (inside the host ImGui frame) to draw our window. Do not confuse named `register_overlay()` with suppressing the main window. Interactive frames call `block_input_next_frame()`; pinned display uses NoInputs. `reshade_open_overlay` is observed, not vetoed; Home remains the recovery entrance. No raw-memory bridge, graphics proxy rewrite, browser pixel streaming, or full-screen capture for blur.

## Core contract

C ABI uses fixed-width scalars, size/version negotiation, expected configuration generation, finite-value validation and the core's ApplyConfig under its critical section. F8 capture owns temporary configuration; control writes return BUSY while capture owns it. UI publishes no credentials, does no network access, and never loads arbitrary core binaries itself. The loaded core is reference-held while using its exports.

## Appearance and metrics

Two compact widgets: interactive AI controls/performance panel; optional click-through pinned metrics. Rounded translucent fill, restrained accent, no ad banner. This is Game-Bar-inspired UX, not a registered Xbox Game Bar widget. No actual blur pass in this iteration.

Sensor selection is independent from the render adapter when identity is not proven. Sensor errors are not zeroes. Callback FPS is not FG/MFG display FPS. Keep native backend replaceable with a later verified PresentMon provider rather than silently relabeling the current number.

## Primary references

- https://crosire.github.io/reshade-docs/namespacereshade.html
- https://crosire.github.io/reshade-docs/structreshade_1_1api_1_1effect__runtime.html
- https://github.com/crosire/reshade/tree/v6.8.0/include
- https://learn.microsoft.com/en-us/xbox/game-bar/guide/click-through
- https://docs.nvidia.com/deploy/nvml-api/group__nvmlDeviceQueries.html
- https://github.com/GameTechDev/PresentMon
- https://releases.electronjs.org/release

Local core audit base: e0b94ff1670de82ccd4264c19d4d459fc3c2a160 (beta0.4.1).
