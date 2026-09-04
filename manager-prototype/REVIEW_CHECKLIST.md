# Prototype review checklist

Use this before moving the Manager to a standalone repository.

## Beginner UX

- [ ] A first-time user can identify “我的游戏” without knowing ReShade/DLSS terminology.
- [ ] The normal path needs no Backend/Add-on/API decisions.
- [ ] Performance / quality modes explain consequences in plain Chinese.
- [ ] Recommended intensity is visually obvious.
- [ ] Every technical error has a Chinese action the user can take.
- [ ] Advanced mode is opt-in.

## Safety

- [x] Prototype install button does not write game files.
- [x] Renderer is sandboxed and context-isolated.
- [x] Auth/network boundary stays outside the game process.
- [x] RR / double-NR protections remain non-overridable by UI contract.
- [x] Existing ReShade native UI remains the fallback plan.

## Productization

- [ ] Replace mock discovery with transactional scanner/install infrastructure.
- [ ] Move to standalone private Manager repository before substantial upstream source import.
- [ ] Preserve DLSS5-Swapper MIT notice and relevant third-party notices.
- [ ] Add actual maintainer/community identifiers only after final branding is chosen.
- [ ] Add signed update/release pipeline.
- [ ] Add real diagnostic data with privacy sanitization.
