# 应用图标

用户选定第一张 DLSS 5 字标，并要求结合醒目的“装机宅”三个字。
成品沿用白绿大 5 和深色底，文字为 DLSS、5、装机宅，应用内与程序共用此图标。

- 源图：`src/renderer/app-icon.png`
- 构建入口：`scripts/build-icon.js`
- 产物：1024 px PNG 与 16/24/32/48/64/128/256 px ICO。
- 生成方式：内置 ImageGen，以第一张已展示字标为编辑参考。
- 画布使用深色不透明底，不包含预览棋盘格。

生成提示词：

Edit the attached FIRST selected DLSS 5 icon into the user's final combined direction. One 1024 x 1024 square desktop app icon, production-quality typography and purposeful spacing. Preserve the original upper italic cream-white 'DLSS' wordmark, the huge distinctive cream-and-lime stylized numeral 5 (especially its sharp separated lower-left segment and lime lower curve), and the minimalist charcoal/white/lime visual system. Add the exact Chinese brand text '装机宅' as a strong, bold, highly legible bottom line spanning most of the icon width. The Chinese words should be clearly prominent, not a tiny signature. Carefully rebalance size and spacing so DLSS, the large 5, and 装机宅 all fit without overlap; retain the large 5 as the primary recognizable symbol. Keep the Chinese lettering heavy and clean, white/cream with restrained lime accent if needed, not a framed badge. No avatar, person, house symbol, extra slogan, gold, glow, flare, texture or watermark. Only exact text 'DLSS' and '装机宅', plus the large numeral 5. Use a full-bleed solid charcoal background covering all four corners of the square canvas: NO checkerboard, transparency grid, white margins, presentation board, or surrounding mockup. The attached grid is an unwanted preview artifact; remove it completely and replace every outside pixel with the same solid charcoal family. Preserve the understated border and reference proportions as much as the new third line permits. Output only one clean finished icon.
