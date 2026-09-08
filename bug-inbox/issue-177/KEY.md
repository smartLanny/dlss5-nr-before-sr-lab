# 黎明行者之血 / The Blood of Dawnwalker — beta0.4.6 观感无效

- Owner: 0.4.6 似乎无效；**0.3.3.5 是最后一个有用版本**
- Log: beta0.4.6 / nr-before-sr.zh-CN.addon64 / path The Blood of Dawnwalker
- mode=2 (NR→SR); Feature18-success 高；启动瞬间 native-hooks-unavailable/installing → 后转 none/active（瞬时非终态）
- ColorDiag: original/proxy/composited meanRGB≈0 且 finalChanged=0；nr-output 有极小值但合成无可见变化 → 与「无效」观感一致

## Greps
1:07:55:42.848  DLSS 5 AI Rendering Perfect Chain beta0.4.6 attached | author=@野生的装机宅 Bilibili | official_release=https://space.bilibili.com/941799 | language=zh-CN | file=nr-before-sr.zh-CN.addon64 | build_id=beta0.4.6 | sourceCommit=9087a9efbc7bb53a3c79e7766a174534f49c412c sourceDirty=1
6:07:55:42.855  Config: enabled=1 mode=2 identity=0 preTransfer=1.000 postTransfer=1.000 colorFollowRequested=0.000 preserveLighting=1 proxyScale=1.000 paperWhiteScale=1.000 peakShoulderRequested=0 warmup=2 intensity=1.100 localTone=1.000 localStructure=1.000 skinStructure=0.300 autoMask=1 style=2 uiCorrection=0 r8OutputEncoding=0 perfEpoch=23 jitterMode=1 feature18Evals=0
21:07:55:59.530  Found SR NGX module: H:\games\The Blood of Dawnwalker\Engine\Plugins\Runtime\Nvidia\DLSS\Binaries\ThirdParty\Win64\nvngx_dlss.dll
48:07:56:02.820  [artifact-identity] sourceCommit=9087a9efbc7bb53a3c79e7766a174534f49c412c sourceDirty=1 status=verified addonPath=H:\games\The Blood of Dawnwalker\Dawnwalker\Binaries\Win64\nr-before-sr.zh-CN.addon64 addonSha256=b68f2709a131c9ce0513b6366dbcc2e7d551bef5bcd41934075407378a48c090 measurement=loaded-path-file-once-worker.
87:07:56:11.846  Pipeline diagnostic scope reset: epoch=1 Feature1 generation=1 mode=2.
88:07:56:11.847  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
112:07:56:11.980  [command-state] NR bypass: reason=native-hooks-unavailable token=14 registered=1 installing=1 dirty=0x0 count=1 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
113:07:56:11.980  [state] bypass transition #1: waiting-feature1 -> command-state-unverified.
139:07:56:12.015  [stats] current contract: generation=1 mode=2 carrier-requests=2 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
161:07:56:12.089  [command-state] NR bypass: reason=native-hooks-unavailable token=33 registered=1 installing=1 dirty=0x0 count=2 baseline=Reset resetSerial=2 pipelineWrites=0 nullBaseline=1 guard=1/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
185:07:56:12.110  [stats] current contract: generation=1 mode=2 carrier-requests=3 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
192:07:56:16.421  NR guide binding: mode=2 depth=1920x1080@0,0 mv=1920x1080@0,0 sourceMV=3840x2160 converted=1 scale=1.000000,1.000000 flags=73 depthInverted=1 historyReset=1.
193:07:56:16.421  NR create contract: mode=2 extent=1920x1080 depth=1920x1080 mv=1920x1080 gameFlags=73 normalizedFlags=73 perfQuality=5.
195:07:56:16.422  Feature 18 create-time tuning: mode=2 extent=1920x1080 intensity=1.100 style=2 autoMask=1 skin=0.300 localTone=1.000 localStructure=1.000 uiCorrection=0 proxyMapping=existing.
196:07:56:16.733  [nr-instance] created=1 mode=2 featureGeneration=1 config=2 intensity=1.100 style=2 proxyMapping=existing.
255:07:56:16.818  [state] bypass transition #2: command-state-unverified -> none/active.
416:07:56:18.437  ColorDiag #1 mode=2 featureGeneration=1 extent=1920x1080 samples=2040 invalid=0 maxNrDelta=0.000152 changed=0 maxFinalRelativeDelta=0.000000 meanFinalRelativeDelta=0.000000 finalChanged=0 sampledExposure=1.000000 sampledSourceToCanonical=0.638621 transfer=1.000 colorFollowEffective=0.000 preserveLighting=1.
424:07:56:22.735  ColorDiag #2 mode=2 featureGeneration=1 extent=1920x1080 samples=2040 invalid=0 maxNrDelta=0.000176 changed=0 maxFinalRelativeDelta=0.000000 meanFinalRelativeDelta=0.000000 finalChanged=0 sampledExposure=1.000000 sampledSourceToCanonical=0.058279 transfer=1.000 colorFollowEffective=0.000 preserveLighting=1.
430:07:56:23.378  ColorDiag #3 mode=2 featureGeneration=1 extent=1920x1080 samples=2040 invalid=0 maxNrDelta=0.000180 changed=0 maxFinalRelativeDelta=0.000000 meanFinalRelativeDelta=0.000000 finalChanged=0 sampledExposure=1.000000 sampledSourceToCanonical=0.015383 transfer=1.000 colorFollowEffective=0.000 preserveLighting=1.
459:07:56:25.779  [stats] session totals: Feature18-success=597 transfer-prepared=595 transfer-recorded=595 transfer-bound=595 transfer-submitted=595 lifecycle-bypass=0; queue-affinity admitted=447 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
460:07:56:25.779  [stats] current contract: generation=1 mode=2 carrier-requests=600 Feature18-success=597 transfer-prepared=595 transfer-recorded=595 transfer-bound=595 transfer-submitted=595.
488:07:56:38.977  [stats] session totals: Feature18-success=1197 transfer-prepared=1195 transfer-recorded=1195 transfer-bound=1195 transfer-submitted=1195 lifecycle-bypass=0; queue-affinity admitted=621 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
489:07:56:38.977  [stats] current contract: generation=1 mode=2 carrier-requests=1200 Feature18-success=1197 transfer-prepared=1195 transfer-recorded=1195 transfer-bound=1195 transfer-submitted=1195.
518:07:56:54.501  [stats] session totals: Feature18-success=1797 transfer-prepared=1795 transfer-recorded=1795 transfer-bound=1795 transfer-submitted=1795 lifecycle-bypass=0; queue-affinity admitted=622 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
519:07:56:54.501  [stats] current contract: generation=1 mode=2 carrier-requests=1800 Feature18-success=1797 transfer-prepared=1795 transfer-recorded=1795 transfer-bound=1795 transfer-submitted=1795.
548:07:57:10.016  [stats] session totals: Feature18-success=2397 transfer-prepared=2395 transfer-recorded=2395 transfer-bound=2395 transfer-submitted=2395 lifecycle-bypass=0; queue-affinity admitted=623 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
549:07:57:10.017  [stats] current contract: generation=1 mode=2 carrier-requests=2400 Feature18-success=2397 transfer-prepared=2395 transfer-recorded=2395 transfer-bound=2395 transfer-submitted=2395.
566:07:57:26.378  Recycled rotating chain slot: count=1 age=263 mode=2 extent=1920x1080.
567:07:57:28.273  Recycled rotating chain slot: count=2 age=198 mode=2 extent=1920x1080.
568:07:57:28.417  Recycled rotating chain slot: count=3 age=176 mode=2 extent=1920x1080.
593:07:57:33.001  [stats] session totals: Feature18-success=2997 transfer-prepared=2991 transfer-recorded=2991 transfer-bound=2991 transfer-submitted=2991 lifecycle-bypass=0; queue-affinity admitted=709 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
594:07:57:33.002  [stats] current contract: generation=1 mode=2 carrier-requests=3000 Feature18-success=2997 transfer-prepared=2991 transfer-recorded=2991 transfer-bound=2991 transfer-submitted=2991.
598:07:57:35.273  [state] bypass transition #3: none/active -> master-disabled.
601:07:57:36.203  [state] bypass transition #4: master-disabled -> waiting-feature1.
604:07:57:36.278  [state] bypass transition #5: waiting-feature1 -> none/active.
607:07:57:48.594  [state] bypass transition #6: none/active -> master-disabled.
610:07:57:50.770  [state] bypass transition #7: master-disabled -> waiting-feature1.
613:07:57:50.844  [state] bypass transition #8: waiting-feature1 -> none/active.
637:07:57:52.214  [stats] session totals: Feature18-success=3597 transfer-prepared=3587 transfer-recorded=3587 transfer-bound=3587 transfer-submitted=3587 lifecycle-bypass=0; queue-affinity admitted=767 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
638:07:57:52.215  [stats] current contract: generation=1 mode=2 carrier-requests=3600 Feature18-success=3597 transfer-prepared=3587 transfer-recorded=3587 transfer-bound=3587 transfer-submitted=3587.
