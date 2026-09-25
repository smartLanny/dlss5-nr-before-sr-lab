## build
20:17:03.266  DLSS 5 AI Rendering Perfect Chain beta0.4.2-native-bridge-exp1 attached | author=@野生的装机宅 Bilibili | official_release=https://space.bilibili.com/941799 | language=zh-CN | file=DLSS5-AI渲染超分版-beta0.4.2-native-bridge-exp1-@野生的装机宅-Bilibili.addon64 | build_id=beta0.4.2-native-bridge-exp1 | sourceCommit=9282e6ba771e369dac93166c02ff10a2173f35d0 sourceDirty=1
20:17:03.269  ShortFuse sidecar: loaded=0 enabledSetting=0 hookPoint=0 ownsNR=0; AI upscaling NR available.
20:17:03.269  Generic RenoDX DLSS5 addon: loaded=0; AI upscaling NR available.
20:17:03.269  Config: enabled=1 mode=2 identity=0 preTransfer=1.200 postTransfer=1.200 colorFollowRequested=0.750 preserveLighting=0 proxyScale=1.000 paperWhiteScale=1.000 peakShoulderRequested=0 warmup=2 intensity=1.000 localTone=1.000 localStructure=1.000 skinStructure=-1.000 autoMask=0 style=0 uiCorrection=1 r8OutputEncoding=0 perfEpoch=2 jitterMode=1 feature18Evals=0
20:17:03.273  Registered ReShade overlay, F6 toggle and '-' diagnostic screenshot hotkey (API 18).

## key
1:20:17:03.266  DLSS 5 AI Rendering Perfect Chain beta0.4.2-native-bridge-exp1 attached | author=@野生的装机宅 Bilibili | official_release=https://space.bilibili.com/941799 | language=zh-CN | file=DLSS5-AI渲染超分版-beta0.4.2-native-bridge-exp1-@野生的装机宅-Bilibili.addon64 | build_id=beta0.4.2-native-bridge-exp1 | sourceCommit=9282e6ba771e369dac93166c02ff10a2173f35d0 sourceDirty=1
9:20:17:04.115  [module] pinned hook owner name=D3D12_CreateFeature address=00007FFCA500F960 module=00007FFCA4FE0000 until process exit.
10:20:17:04.116  MH_CreateHook D3D12_CreateFeature                    target=00007FFCA500F960 status=0 (MH_OK)
15:20:17:04.116  [module] pinned hook owner name=core CreateFeature address=00007FFCA88A7FE0 module=00007FFCA8840000 until process exit.
16:20:17:04.117  MH_CreateHook core CreateFeature                     target=00007FFCA88A7FE0 status=0 (MH_OK)
32:20:17:13.801  _nvngx core CreateFeature entered: call=1 id=1 (SuperSampling)
33:20:17:13.834  core CreateFeature id=1 (SuperSampling) result=0x00000001(Success) handle=000002013B53E3F0
68:20:17:13.844  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
92:20:17:13.911  [command-state] NR bypass: reason=native-hooks-unavailable token=1 registered=1 installing=1 dirty=0x0 count=1 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0; successful native Reset and game bindings can recover.
93:20:17:13.911  [state] bypass transition #1: waiting-feature1 -> command-state-unverified.
118:20:17:13.933  [stats] current contract: generation=1 mode=2 carrier-requests=2 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
119:20:17:13.933  [command-state] NR bypass: reason=null-pso-policy-disabled token=1 registered=1 installing=0 dirty=0x0 count=2 baseline=Reset resetSerial=1 pipelineWrites=0 nullBaseline=1; successful native Reset and game bindings can recover.
143:20:17:13.961  [stats] current contract: generation=1 mode=2 carrier-requests=3 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
144:20:17:13.961  [command-state] NR bypass: reason=null-pso-policy-disabled token=1 registered=1 installing=0 dirty=0x0 count=3 baseline=Reset resetSerial=2 pipelineWrites=0 nullBaseline=1; successful native Reset and game bindings can recover.
167:20:17:13.981  [command-state] NR bypass: reason=null-pso-policy-disabled token=1 registered=1 installing=0 dirty=0x0 count=4 baseline=Reset resetSerial=3 pipelineWrites=0 nullBaseline=1; successful native Reset and game bindings can recover.
190:20:17:15.047  [command-state] NR bypass: reason=null-pso-policy-disabled token=1 registered=1 installing=0 dirty=0x0 count=5 baseline=Reset resetSerial=4 pipelineWrites=0 nullBaseline=1; successful native Reset and game bindings can recover.
213:20:17:15.059  [command-state] NR bypass: reason=null-pso-policy-disabled token=1 registered=1 installing=0 dirty=0x0 count=6 baseline=Reset resetSerial=5 pipelineWrites=0 nullBaseline=1; successful native Reset and game bindings can recover.
370:20:17:21.489  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
371:20:17:21.489  [stats] current contract: generation=1 mode=2 carrier-requests=600 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
372:20:17:21.489  [command-state] NR bypass: reason=null-pso-policy-disabled token=1 registered=1 installing=0 dirty=0x0 count=600 baseline=Reset resetSerial=599 pipelineWrites=0 nullBaseline=1; successful native Reset and game bindings can recover.
397:20:17:25.830  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
398:20:17:25.830  [stats] current contract: generation=1 mode=2 carrier-requests=1200 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
399:20:17:25.830  [command-state] NR bypass: reason=null-pso-policy-disabled token=1 registered=1 installing=0 dirty=0x0 count=1200 baseline=Reset resetSerial=1199 pipelineWrites=0 nullBaseline=1; successful native Reset and game bindings can recover.
423:20:17:30.172  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
424:20:17:30.172  [stats] current contract: generation=1 mode=2 carrier-requests=1800 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
425:20:17:30.173  [command-state] NR bypass: reason=null-pso-policy-disabled token=1 registered=1 installing=0 dirty=0x0 count=1800 baseline=Reset resetSerial=1799 pipelineWrites=0 nullBaseline=1; successful native Reset and game bindings can recover.
450:20:17:34.509  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
451:20:17:34.509  [stats] current contract: generation=1 mode=2 carrier-requests=2400 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
452:20:17:34.509  [command-state] NR bypass: reason=null-pso-policy-disabled token=1 registered=1 installing=0 dirty=0x0 count=2400 baseline=Reset resetSerial=2399 pipelineWrites=0 nullBaseline=1; successful native Reset and game bindings can recover.
476:20:17:38.846  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
477:20:17:38.846  [stats] current contract: generation=1 mode=2 carrier-requests=3000 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
478:20:17:38.846  [command-state] NR bypass: reason=null-pso-policy-disabled token=1 registered=1 installing=0 dirty=0x0 count=3000 baseline=Reset resetSerial=2999 pipelineWrites=0 nullBaseline=1; successful native Reset and game bindings can recover.
503:20:17:49.763  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
504:20:17:49.763  [stats] current contract: generation=1 mode=2 carrier-requests=3600 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
505:20:17:49.763  [command-state] NR bypass: reason=null-pso-policy-disabled token=1 registered=1 installing=0 dirty=0x0 count=3600 baseline=Reset resetSerial=3599 pipelineWrites=0 nullBaseline=1; successful native Reset and game bindings can recover.
530:20:18:03.310  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
531:20:18:03.310  [stats] current contract: generation=1 mode=2 carrier-requests=4200 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
532:20:18:03.310  [command-state] NR bypass: reason=null-pso-policy-disabled token=1 registered=1 installing=0 dirty=0x0 count=4200 baseline=Reset resetSerial=4199 pipelineWrites=0 nullBaseline=1; successful native Reset and game bindings can recover.
558:20:18:23.309  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
559:20:18:23.309  [stats] current contract: generation=1 mode=2 carrier-requests=4800 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.

## ReShade head
20:17:00:945 [25008] | INFO  | Initializing crosire's ReShade version '6.8.0.2155' (64-bit) loaded from 'E:\Program Files (x86)\Steam\steamapps\common\Baldurs Gate 3\bin\dxgi.dll' into 'E:\Program Files (x86)\Steam\steamapps\common\Baldurs Gate 3\bin\bg3_dx11.exe' (0x64317982) ...
20:17:00:976 [25008] | INFO  | Registering hooks for 'user32.dll' ...
20:17:00:977 [25008] | INFO  | > Libraries loaded.
20:17:00:977 [25008] | INFO  | > Found 21 match(es). Installing ...
20:17:01:006 [25008] | INFO  | Registering hooks for 'C:\Windows\system32\dinput.dll' ...
