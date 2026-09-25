# unified3 menu and short checks

The menu now has one navigation level: **Picture | Lighting | Experiments | Developer | Feedback**. Active layers show every model control directly. Page bodies scroll while status and navigation stay pinned; narrow windows stack labels and sliders. Native model skin masking is separate from optional face detection.

New or missing settings, new-layer initialization and the relevant reset use: one layer, 100% working scale, model strength 1.5, structure/tone 1, skin structure 0.4, native mask/UI correction on, final enhancement and AI colour 1, lighting/edge protection 0. Saved values are retained. Reducing then increasing the layer count restores each layer's settings. Dragging applies on release.

Lighting methods are exclusive and keep separate values. Switching exits the previous preview. Lighting presets, undo and resets never write AI colour. Reset current method preserves the shared safeguards, reference white and other methods. Reset this layer changes only that layer. Restore default picture preserves the master switch and compatibility preferences.

Colour policy controls permission; AI colour controls the adopted amount. Input mapping/N2 and recovery operate at other stages. They still interact in the rendered result. D21 composition and input remain the baseline; RGB and N2 remain explicit experiments. Unavailable controls explain why. Reference white is editable only with a current matching HDR input-mapping observation.

Independent face detection and both power-saving modes default off. Bundled face resources occupy disk space; off performs only small state checks and does not start new detection/face work. Previously submitted work drains safely. A checked energy option is not proof of an active pause.

Feedback: F8 creates text; F7 captures current-path images and available model stages. Stage captures reset/warm history and affect timing. Comparisons temporarily change settings and restore them on completion/cancel. Pre-SR E is not the game's final SR output.

## Three short checks

1. Preserve the INI. Check saved values, page switching, 1–5 layers and release-to-apply scale. Re-enable a layer and confirm its settings remain.
2. Temporarily set AI colour to 0.3. Select a lighting preset, undo, switch methods and reset the current method. AI colour must remain 0.3; switching exits old previews. Restore your original value.
3. Reset one layer: strength 1.5, skin 0.4 and native mask/UI on; other layers and chain settings stay intact. Independent detection off must prevent preview activation. Restore your settings afterwards.

Keep unified2 and your previous configuration for rollback. This is a candidate, not a stable release; it is not installed automatically. CPU, Windows, WARP, actual-menu host, controlled NVIDIA inference, hosted CI and game acceptance are separate records. Menu changes do not establish a fix for the 99%/100% colour discontinuity, RGB halos or steady-state RR+NR+FG performance.

The next work remains same-source P/N/E/native-SR-O colour and detail localization, equivalent-workload wait/submission profiling, then independently validated residual SR, cross-RR and AMDOF paths.
