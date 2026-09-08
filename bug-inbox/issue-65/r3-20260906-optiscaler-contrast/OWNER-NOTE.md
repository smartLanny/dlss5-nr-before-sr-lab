# SF6 beta0.4.3r3 + OptiScaler contrast (2026-09-06)

Owner/guest: Capcom RE Engine; game UI may show no DLSS, but DX12. OptiScaler v0.10.0-dev can run DLSS + DLSS Ray Reconstruction (NR→SR); our ReShade addon still fails.

## Our pack
- StreetFighter6.exe, beta0.4.3r3, RTX 5060 Ti
- RenoDX loaded=0 (clean vs earlier PC-A RenoDX case)
- Gate: unregistered-list / native-bypass / command-state-unverified; nrSuccess=0
- srCarrierEvaluations≈8918 (Feature1 carrier traffic present)

## OptiScaler screenshot (guest)
- OptiScaler v0.10.0-dev on StreetFighter6.exe
- D3D12, Input/Output DLSS, Override DLSS on
- DLSS Ray Reconstruction / 神经渲染 enabled, NR→SR path shown
- Note: screenshot GPU is 3060 Ti (different machine); used as existence proof that an injected DLSS/NR path works on SF6 DX12 without native DLSS menu

## Implication
RE Engine SF6 can host NGX DLSS/NR via injection (OptiScaler). Our failure is not "no DLSS API in process" alone — we already see Feature1 evals — but command-list registration (`unregistered-list`). Cross-ref #36 RE Engine family.
