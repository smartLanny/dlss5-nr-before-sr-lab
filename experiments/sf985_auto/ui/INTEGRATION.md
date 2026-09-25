# Three processing points, not five duplicate tiles

Owner clarification, 2026-09-12. Refs #190, #226 and smartLanny/dlss5-manager#22.

Keep the current NR -> SR and SR -> NR selectors. They already represent before-reconstruction (Render) and after-reconstruction (Upscaled). Add only a third output-before-display (Present) tile on the right. Auto is a selection policy, not another execution point; Off continues to use Enabled/F6. Internal request protocol values may still distinguish Auto/Before/After/Present/Off without five peer UI controls. Do not duplicate Render/Upscaled next to the existing controls.

## This commit

`DrawProcessingModeBlocks.inl` is a complete replacement for the identically named four-argument function in D14 `src/nr_settings_ui.inl`. The observed base is `49b0deb0d09b0a442784e0ebf1073f9498eaf157`; the original UI blob is `24199c452e72c59d162679bab8f63ff7dd144db8`.

It preserves Mode=2 (before), Mode=1 (after), the original hidden IDs, F6/strength values, and effective RR/compatibility locks. It measures text through the existing ImVec2 ABI adapter and uses short multiline labels or a vertical layout at narrow widths.

**The replacement is not applied to production Core by this commit. Present is visibly disabled because D14 has no connected Present-to-Core consumer. There is no Mode=3 write, fake working third route, new NGX call or new callback registration. Do not enable the tile by merely removing its disabled condition.**

The accompanying complete source handoff includes the exact UI delta generator, a UI-only entry point, and an updated cumulative generator. It preserves previous full-reference and Auto code. The generator changes the reviewed selector and source pins only in a new output directory, never overwriting the checkout. Before production compilation, replace the single function, update its explicit `INTEGRATION_CORE_PINS` blob, and preserve every other setting/control. Do not apply an old full UI snapshot over newer local changes.

## Remaining production work

Present needs current color and valid guides, a same-device/source/view contract, a real NR-only Core consumer, the actual swapchain queue and legal output writeback, resource retirement, and shared source-frame ownership with before/after and FG. Switching modes must reset affected history and retain user preference; F8 must distinguish requested, recorded and confirmed routes. Do not reuse old input or infer temporal-guide validity from a backbuffer pointer.

Present means processing this injected game's output, not the desktop. It does not enable or disable the game's existing SR. Avoid the absolute labels 'best compatibility' and 'worst performance': NR work size, pass count, guide generation, copies and scheduling determine cost and supported paths. HUD/HDR/FG require separate checks. #252's F6-off slowdown remains a separate unresolved hot-path issue; no fix is claimed here.

## Verification

From repository root:

```text
g++ -std=c++17 -O2 -Wall -Wextra -Werror experiments/sf985_auto/tests/test_processing_ui.cpp -o processing_ui_test
./processing_ui_test
```

The actual replacement function passed 1267 UI-shim checks with GCC 14.2 and Clang 17 (ASan/UBSan). The shim is not real ReShade, Windows ABI or font rendering. The separate source-handoff suite passed 31 Python tests, including exact replacement, pin updates and a real `git apply --check` on a synthetic fixture. The complete D14 checkout generator, Windows/Core/HLSL/WARP/NGX/game/performance and hosted CI were not run. An initial unused fixture variable and a bilingual-ID-count fixture expectation were corrected before final checks; no production gate was relaxed.

No DLL, game deployment, main merge, release or PR is created as part of this source handoff.
