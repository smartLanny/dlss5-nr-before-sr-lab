# Verified CI results · 2026-09-05

Code commit: `8fade9e4bf55ba6ec65e43fd5f3e3a9e1ca8e077`.

- Quickbar Windows build run: https://github.com/smartLanny/dlss5-nr-before-sr-lab/actions/runs/33914577117
- Job 101158654056: completed/success. MSVC compile/link, frame-history CTest and artifact upload all succeeded.
- Manager tests run: https://github.com/smartLanny/dlss5-nr-before-sr-lab/actions/runs/33914577250
- Jobs 101158654545 (Ubuntu) and 101158654850 (Windows): completed/success.

The first native build exposed an unresolved obsolete ImGui SetWindowFontScale symbol. The code now uses the ReShade-exported PushFont/PopFont pair. The succeeding run above includes that correction.

Artifact: `quickbar-experimental-x64`, id `9952671903`.
Original artifact archive SHA256: `5129db561fab0520b37e970d0b5ecbb9effbf48f782f9ce0825f13e600436cc1`.
Extracted `zjz-quickbar.addon64`: 210432 bytes; PE machine 0x8664 (x64).
Binary SHA256: `6392909cedd9b84b437494eff1c3576407bf216927e374a2c2f7800d06958c61`.

The downloaded artifact digest and binary PE architecture were checked locally. The binary was not executed locally. This proves compilation and portable test behavior, NOT real ReShade loading, NR control, GPU sensor correctness, frame-generation semantics or game compatibility. The paired core wrapper and full Electron installer have not been built/tested in this task.
