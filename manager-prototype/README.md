# DLSS 5 AI Rendering Manager — China UX Prototype

> Product prototype for the Chinese beginner-friendly manager. This directory is isolated from the NR→SR research core and does not change the rendering algorithm.

## Prototype goals

1. Default to Simplified Chinese and hide engineering terminology.
2. Beginner navigation: 我的游戏 / 一键安装 / 问题修复 / 设置.
3. Game detail exposes intent-level controls first:
   - 性能优先（NR → SR）
   - 画质优先（SR → NR）
   - AI 增强强度 presets
   - 风格
   - 人脸保护
   - 使用推荐设置
4. Advanced mode can reveal backend / add-ons / overlay / logs later.
5. Authentication is desktop-side only. The in-game add-on must never need network access to render.
6. Reserve a stable NR control contract rather than depending on RenoDX private ImGui internals.
7. Keep the existing ReShade native settings page as the fallback UI.

## Relationship to DLSS5-Swapper

This prototype is informed by the MIT-licensed DLSS5-Swapper project by Rakan Alkhaldi. If the product later directly incorporates substantial DLSS5-Swapper source, its original copyright and MIT license notice must remain in the distributed product, together with all relevant third-party notices.

Upstream reference: `rakanki911/DLSS5-Swapper`.

## Run

```powershell
cd manager-prototype
npm install
npm start
```

This first branch is intentionally a UI/product skeleton. Install/restore, authentication service, game scanning, and live NR control are represented by explicit adapter boundaries rather than pretending they are already production-ready.
