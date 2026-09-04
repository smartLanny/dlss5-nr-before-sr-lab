# Roadmap

## Foundation (this branch)

- one-page game list;
- launcher discovery plus manual folders;
- x64 DX11/DX12 + native DLSS compatibility gate;
- transactional install, repair and uninstall;
- payload SHA-256 verification;
- live INI controls using the add-on's existing 500 ms reload;
- honest component diagnostics;
- Chinese-first product copy and Bilibili attribution.

## Next safe additions

1. Persist a small read-only status file from the add-on so the desktop app can
   distinguish `组件完整`, `等待 DLSS 帧`, `NR→SR 正在运行` and safety bypasses.
2. Add a signed game compatibility catalogue with tested presets.
3. Add release update checking with signed manifests and rollback.
4. Add optional crash/log collection that strips paths and addresses before
   sharing.
5. Consider Feeder or other routes only after the native path is stable.

## Explicitly deferred

- independent in-game Chromium or Win32 overlay;
- account system and online entitlement;
- automatic anti-cheat override;
- direct DLL injection without ReShade.
