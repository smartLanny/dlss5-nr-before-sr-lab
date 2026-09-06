# Cloud-readable fullish extract — issue #98

## 20260906-unpacked_shuhai_feedback_a__capture-info.txt
```
format=DLSS5-Core-v1
capture.kind=core
session=
build=beta0.4.4-test1
language=zh-CN
exe.leaf=yysls.exe
gpu=NVIDIA GeForce RTX 5080 Laptop GPU
driver.raw=61664
driver.known=1
vendor=4318
api=49152
display.extent=2560x1440
display.storageFormat=28
display.colorSpace=1
snapshot.valid=1
requested.config=19
requested.enabled=1
requested.mode=2
requested.blend=1.38999999
requested.color=0
requested.workMode=0
requested.workScale=1
requested.lightingLock=0.310000002
requested.detailStability=0.310000002
requested.edgeGuard=0.319999993
requested.intensity=1.01999998
requested.localTone=2
requested.localStructure=1.30999994
requested.skin=0
requested.faceMask=0
requested.uiCorrection=0
requested.jitterMode=1
requested.style=2
applied.instance=1
applied.frame=4253
applied.config=19
applied.intensity=1.01999998
applied.skin=-1
capture.automaticSettingChanges=none
config.writesToIni=0
images.requested=0
gpu.readbackRequested=0
fg.status=unavailable; this collector does not intercept FG
applied.contract=historical successful NR; compare config generation and start/end counters
core.zipLimitBytes=8388608
timeline.columns=tag,elapsedMs,SRcarrier,featureGeneration,reset,jitterX,jitterY,mvScaleX,mvScaleY,preExposure,exposureScale,previousBypass
start.commandState.available=1
start.commandState.schema=1
start.commandState.ageMs=0
start.commandState.sequence=4319
start.commandState.token=32
start.commandState.reason=none
start.commandState.route=typed-pso
start.commandState.registered=1
start.commandState.hooksReady=1
start.commandState.guardRequested=1
start.commandState.guardCovered=1
start.commandState.freshReset=1
start.commandState.tainted=0
start.commandState.baseline=Reset
start.commandState.resetSerial=517
start.commandState.pipelineWrites=6
start.commandState.nullBaseline=0
start.commandState.hasPso=1
start.commandState.hasDxrPso=0
start.commandState.dirty=0x0
start.commandState.dynamic=0x857
start.commandState.vertexMask=0x1
start.commandState.streamMask=0x0
start.commandState.metaKnown=1
start.commandState.metaInit=0x1020
start.commandState.metaExecute=0x1020
start.commandState.rootHeapPresence=1,1,1,1
start.commandState.rootArgumentCounts=0,5
start.ngxHookFailure.available=0
start.nrSuccess=4253
start.transferSubmitted=4245
start.bypass=none/active
start.srCarrierEvaluations=4319
start.lastCarrierFeature=1
start.rrLiveHandles=0
start.rrCreatesInProgress=0
start.rrEvaluations=0
start.otherNrOwner=0
start.genericRenoDxConflict=0
start.moduleObserverFault=none
start.feature1Preflight=0/0
start.preflightCoreLoaded=0
start.preflightDlssdLoaded=0
start.commandSignatureHook=1
start.commandSignatureDeferredStart=0
start.commandSignatureObserved=44
start.commandSignatureUnknownUses=0
end.commandState.available=0
end.ngxHookFailure.available=0
end.nrSuccess=4289
end.transferSubmitted=4281
end.bypass=none/active
end.srCarrierEvaluations=4355
end.lastCarrierFeature=1
end.rrLiveHandles=0
end.rrCreatesInProgress=0
end.rrEvaluations=0
end.otherNrOwner=0
end.genericRenoDxConflict=0
end.moduleObserverFault=none
end.feature1Preflight=0/0
end.preflightCoreLoaded=0
end.preflightDlssdLoaded=0
end.commandSignatureHook=1
end.commandSignatureDeferredStart=0
end.commandSignatureObserved=44
end.commandSignatureUnknownUses=0
collection.elapsedMs=766
collection.parameterSamples=0
collection.cancelled=0
artifact.sourceCommit=1a8490a2c76d93b0c5e4cc50b8d4f178df55eb83
artifact.sourceDirty=0
artifact.addonSha256=38386426f02f879b9f50219b1af9b868f97efd810bd93acb6fb69f75fb64c600
artifact.identityStatus=verified
module.0=dxgi.dll
module.1=d3d12.dll
module.2=dxgi.dll
module.3=sl.dlss.dll
module.4=_nvngx.dll
module.5=nvngx_deepdvc.dll
module.6=sl.dlss_d.dll
module.7=sl.dlss_g.dll
module.8=DLSS5-AI???????????????-beta0.4.4-test1-@??????????????????-Bilibili.addon64
module.9=D3DCOMPILER_47.dll
module.10=nvwgf2umx.dll
module.11=nvngx_dlss.dll
module.12=nrchain_nvngx.dll
module.13=nvngx_dlssnr.dll
modules.contract=names only; no module bytes or process addresses collected
color.status=recent-recorded-combine
color.requested=0
color.effective=0
color.reason=bounded-model-value-domain
color.contract=last recorded blend; not a measured picture strength
collection.metadataTruncated=0

```

## 20260906-unpacked_shuhai_feedback_a__logs__nr-current-summary.txt — KEY LINES (115) + TAIL
```
11:16:34.303  [state] bypass transition #5: queue-affinity-unverified -> command-state-unverified.
11:16:34.307  [state] bypass transition #6: command-state-unverified -> none/active.
11:16:34.314  [command-state] NR bypass: reason=native-hooks-unavailable token=17 registered=1 installing=1 dirty=0x0 count=9 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:16:34.314  [state] bypass transition #7: none/active -> command-state-unverified.
11:16:34.336  [state] bypass transition #8: command-state-unverified -> none/active.
11:16:34.524  [command-state] NR bypass: reason=native-hooks-unavailable token=26 registered=1 installing=1 dirty=0x0 count=10 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:16:34.524  [state] bypass transition #9: none/active -> command-state-unverified.
11:16:34.538  [state] bypass transition #10: command-state-unverified -> none/active.
11:16:34.567  [command-state] NR bypass: reason=native-hooks-unavailable token=20 registered=1 installing=1 dirty=0x0 count=11 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:16:34.568  [state] bypass transition #11: none/active -> command-state-unverified.
11:16:34.581  [state] bypass transition #12: command-state-unverified -> none/active.
11:17:28.543  [command-state] NR bypass: reason=native-hooks-unavailable token=22 registered=1 installing=1 dirty=0x0 count=12 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:17:29.782  [command-state] NR bypass: reason=native-hooks-unavailable token=25 registered=1 installing=1 dirty=0x0 count=13 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:17:30.064  [command-state] NR bypass: reason=native-hooks-unavailable token=12 registered=1 installing=1 dirty=0x0 count=14 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:17:30.233  [command-state] NR bypass: reason=native-hooks-unavailable token=5 registered=1 installing=1 dirty=0x0 count=15 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:17:30.389  [command-state] NR bypass: reason=native-hooks-unavailable token=16 registered=1 installing=1 dirty=0x0 count=16 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:17:31.019  [command-state] NR bypass: reason=native-hooks-unavailable token=21 registered=1 installing=1 dirty=0x0 count=18 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:17:31.114  [command-state] NR bypass: reason=native-hooks-unavailable token=11 registered=1 installing=1 dirty=0x0 count=19 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:17:31.510  [command-state] NR bypass: reason=native-hooks-unavailable token=9 registered=1 installing=1 dirty=0x0 count=20 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:17:31.664  [command-state] NR bypass: reason=native-hooks-unavailable token=8 registered=1 installing=1 dirty=0x0 count=21 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:17:32.669  [command-state] NR bypass: reason=native-hooks-unavailable token=27 registered=1 installing=1 dirty=0x0 count=22 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:17:32.969  [command-state] NR bypass: reason=native-hooks-unavailable token=13 registered=1 installing=1 dirty=0x0 count=23 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:17:34.246  [command-state] NR bypass: reason=native-hooks-unavailable token=28 registered=1 installing=1 dirty=0x0 count=24 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:17:34.802  [command-state] NR bypass: reason=native-hooks-unavailable token=29 registered=1 installing=1 dirty=0x0 count=25 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:17:34.933  [command-state] NR bypass: reason=native-hooks-unavailable token=33 registered=1 installing=1 dirty=0x0 count=29 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:17:35.119  [command-state] NR bypass: reason=native-hooks-unavailable token=36 registered=1 installing=1 dirty=0x0 count=32 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:17:35.193  [command-state] NR bypass: reason=native-hooks-unavailable token=37 registered=1 installing=1 dirty=0x0 count=33 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:17:35.256  [command-state] NR bypass: reason=native-hooks-unavailable token=38 registered=1 installing=1 dirty=0x0 count=34 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:17:35.409  [command-state] NR bypass: reason=native-hooks-unavailable token=40 registered=1 installing=1 dirty=0x0 count=36 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:17:35.545  [command-state] NR bypass: reason=native-hooks-unavailable token=41 registered=1 installing=1 dirty=0x0 count=37 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:17:35.679  [command-state] NR bypass: reason=native-hooks-unavailable token=42 registered=1 installing=1 dirty=0x0 count=38 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:17:35.852  [command-state] NR bypass: reason=native-hooks-unavailable token=43 registered=1 installing=1 dirty=0x0 count=39 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:17:36.122  [command-state] NR bypass: reason=native-hooks-unavailable token=44 registered=1 installing=1 dirty=0x0 count=40 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:17:36.203  [command-state] NR bypass: reason=native-hooks-unavailable token=45 registered=1 installing=1 dirty=0x0 count=41 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:17:36.295  [command-state] NR bypass: reason=native-hooks-unavailable token=46 registered=1 installing=1 dirty=0x0 count=42 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:17:36.447  [command-state] NR bypass: reason=native-hooks-unavailable token=47 registered=1 installing=1 dirty=0x0 count=43 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:17:41.000  [command-state] NR bypass: reason=native-hooks-unavailable token=48 registered=1 installing=1 dirty=0x0 count=44 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:17:43.033  [stats] session totals: Feature18-success=549 transfer-prepared=543 transfer-recorded=543 transfer-bound=543 transfer-submitted=543 lifecycle-bypass=0; queue-affinity admitted=140 unknown-bypass=6 predicted-change-bypass=0 actual-mismatch=0.
11:17:43.033  [stats] current contract: generation=1 mode=2 carrier-requests=600 Feature18-success=549 transfer-prepared=543 transfer-recorded=543 transfer-bound=543 transfer-submitted=543.
11:17:48.062  Config updated from F6: enabled=0 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.310 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=590 feature18Evals=786
11:17:48.531  Config updated from F6: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.310 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=591 feature18Evals=786
11:17:56.131  [stats] session totals: Feature18-success=1149 transfer-prepared=1141 transfer-recorded=1141 transfer-bound=1141 transfer-submitted=1141 lifecycle-bypass=0; queue-affinity admitted=236 unknown-bypass=6 predicted-change-bypass=0 actual-mismatch=0.
11:17:56.131  [stats] current contract: generation=1 mode=2 carrier-requests=1200 Feature18-success=1149 transfer-prepared=1141 transfer-recorded=1141 transfer-bound=1141 transfer-submitted=1141.
11:18:08.781  [stats] session totals: Feature18-success=1749 transfer-prepared=1741 transfer-recorded=1741 transfer-bound=1741 transfer-submitted=1741 lifecycle-bypass=0; queue-affinity admitted=244 unknown-bypass=6 predicted-change-bypass=0 actual-mismatch=0.
11:18:08.781  [stats] current contract: generation=1 mode=2 carrier-requests=1800 Feature18-success=1749 transfer-prepared=1741 transfer-recorded=1741 transfer-bound=1741 transfer-submitted=1741.
11:18:21.379  [stats] session totals: Feature18-success=2349 transfer-prepared=2341 transfer-recorded=2341 transfer-bound=2341 transfer-submitted=2341 lifecycle-bypass=0; queue-affinity admitted=246 unknown-bypass=6 predicted-change-bypass=0 actual-mismatch=0.
11:18:21.379  [stats] current contract: generation=1 mode=2 carrier-requests=2400 Feature18-success=2349 transfer-prepared=2341 transfer-recorded=2341 transfer-bound=2341 transfer-submitted=2341.
11:18:25.566  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.310 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=592 feature18Evals=2549
11:18:27.505  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.310 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=593 feature18Evals=2640
11:18:30.245  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.310 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=594 feature18Evals=2769
11:18:31.726  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.310 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=595 feature18Evals=2838
11:18:32.515  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.310 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=596 feature18Evals=2875
11:18:33.286  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.310 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=597 feature18Evals=2911
11:18:34.102  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.310 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=598 feature18Evals=2949
11:18:34.105  [stats] session totals: Feature18-success=2949 transfer-prepared=2941 transfer-recorded=2941 transfer-bound=2941 transfer-submitted=2941 lifecycle-bypass=0; queue-affinity admitted=291 unknown-bypass=6 predicted-change-bypass=0 actual-mismatch=0.
11:18:34.105  [stats] current contract: generation=1 mode=2 carrier-requests=3000 Feature18-success=2949 transfer-prepared=2941 transfer-recorded=2941 transfer-bound=2941 transfer-submitted=2941.
11:18:34.635  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.310 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=599 feature18Evals=2974
11:18:35.140  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.310 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=600 feature18Evals=2997
11:18:36.765  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.310 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=601 feature18Evals=3073
11:18:37.797  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.310 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=602 feature18Evals=3121
11:18:39.291  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.310 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=603 feature18Evals=3191
11:18:40.153  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.310 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=604 feature18Evals=3231
11:18:40.767  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.310 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=605 feature18Evals=3259
11:18:42.893  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.310 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=606 feature18Evals=3359
11:18:46.978  [stats] session totals: Feature18-success=3549 transfer-prepared=3541 transfer-recorded=3541 transfer-bound=3541 transfer-submitted=3541 lifecycle-bypass=0; queue-affinity admitted=373 unknown-bypass=6 predicted-change-bypass=0 actual-mismatch=0.
11:18:46.978  [stats] current contract: generation=1 mode=2 carrier-requests=3600 Feature18-success=3549 transfer-prepared=3541 transfer-recorded=3541 transfer-bound=3541 transfer-submitted=3541.
11:18:54.594  [command-state] NR bypass: reason=native-hooks-unavailable token=29 registered=1 installing=1 dirty=0x0 count=45 baseline=Reset resetSerial=247 pipelineWrites=0 nullBaseline=1 guard=1/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:18:54.933  [command-state] NR bypass: reason=native-hooks-unavailable token=43 registered=1 installing=1 dirty=0x0 count=47 baseline=Reset resetSerial=78 pipelineWrites=0 nullBaseline=1 guard=1/0/0 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:18:56.839  [command-state] NR bypass: reason=native-hooks-unavailable token=32 registered=1 installing=1 dirty=0x0 count=50 baseline=Reset resetSerial=491 pipelineWrites=0 nullBaseline=1 guard=1/0/0 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:18:56.883  [command-state] NR bypass: reason=null-guard-graphics-state-used token=32 registered=1 installing=0 dirty=0x0 count=52 baseline=Reset resetSerial=492 pipelineWrites=0 nullBaseline=1 guard=1/1/1 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:18:56.904  [command-state] NR bypass: reason=native-hooks-unavailable token=36 registered=1 installing=1 dirty=0x0 count=53 baseline=Reset resetSerial=65 pipelineWrites=0 nullBaseline=1 guard=1/0/0 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:18:57.978  [command-state] NR bypass: reason=null-guard-graphics-state-used token=29 registered=1 installing=0 dirty=0x0 count=54 baseline=Reset resetSerial=263 pipelineWrites=0 nullBaseline=1 guard=1/1/1 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:18:58.005  [command-state] NR bypass: reason=native-hooks-unavailable token=30 registered=1 installing=1 dirty=0x0 count=55 baseline=Reset resetSerial=167 pipelineWrites=0 nullBaseline=1 guard=1/0/0 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:18:58.616  [command-state] NR bypass: reason=null-guard-graphics-state-used token=36 registered=1 installing=0 dirty=0x0 count=56 baseline=Reset resetSerial=66 pipelineWrites=0 nullBaseline=1 guard=1/1/1 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:18:59.637  [stats] session totals: Feature18-success=4136 transfer-prepared=4128 transfer-recorded=4128 transfer-bound=4128 transfer-submitted=4128 lifecycle-bypass=0; queue-affinity admitted=409 unknown-bypass=6 predicted-change-bypass=0 actual-mismatch=0.
11:18:59.637  [stats] current contract: generation=1 mode=2 carrier-requests=4200 Feature18-success=4136 transfer-prepared=4128 transfer-recorded=4128 transfer-bound=4128 transfer-submitted=4128.
11:19:00.111  [command-state] NR bypass: reason=null-guard-graphics-state-used token=31 registered=1 installing=0 dirty=0x0 count=58 baseline=Reset resetSerial=368 pipelineWrites=0 nullBaseline=1 guard=1/1/1 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:19:00.317  [command-state] NR bypass: reason=null-guard-graphics-state-used token=31 registered=1 installing=0 dirty=0x0 count=59 baseline=Reset resetSerial=369 pipelineWrites=0 nullBaseline=1 guard=1/1/1 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:19:00.339  [command-state] NR bypass: reason=native-hooks-unavailable token=28 registered=1 installing=1 dirty=0x0 count=60 baseline=Reset resetSerial=622 pipelineWrites=0 nullBaseline=1 guard=1/0/0 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:16:34.215  [command-state] restored native state: token=15 pso=[address] raytracing=[address] computeRoot=[address] graphicsRoot=[address] resourceHeap=[address] samplerHeap=[address].
--- TAIL ---
11:18:54.594  [command-state] NR bypass: reason=native-hooks-unavailable token=29 registered=1 installing=1 dirty=0x0 count=45 baseline=Reset resetSerial=247 pipelineWrites=0 nullBaseline=1 guard=1/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
11:18:54.933  [command-state] NR bypass: reason=native-hooks-unavailable token=43 registered=1 installing=1 dirty=0x0 count=47 baseline=Reset resetSerial=78 pipelineWrites=0 nullBaseline=1 guard=1/0/0 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:18:56.815  [command-state] NR bypass: reason=null-guard-graphics-state-used token=29 registered=1 installing=0 dirty=0x0 count=49 baseline=Reset resetSerial=257 pipelineWrites=0 nullBaseline=1 guard=1/1/1 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:18:56.839  [command-state] NR bypass: reason=native-hooks-unavailable token=32 registered=1 installing=1 dirty=0x0 count=50 baseline=Reset resetSerial=491 pipelineWrites=0 nullBaseline=1 guard=1/0/0 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:18:56.883  [command-state] NR bypass: reason=null-guard-graphics-state-used token=32 registered=1 installing=0 dirty=0x0 count=52 baseline=Reset resetSerial=492 pipelineWrites=0 nullBaseline=1 guard=1/1/1 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:18:56.904  [command-state] NR bypass: reason=native-hooks-unavailable token=36 registered=1 installing=1 dirty=0x0 count=53 baseline=Reset resetSerial=65 pipelineWrites=0 nullBaseline=1 guard=1/0/0 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:18:57.978  [command-state] NR bypass: reason=null-guard-graphics-state-used token=29 registered=1 installing=0 dirty=0x0 count=54 baseline=Reset resetSerial=263 pipelineWrites=0 nullBaseline=1 guard=1/1/1 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:18:58.005  [command-state] NR bypass: reason=native-hooks-unavailable token=30 registered=1 installing=1 dirty=0x0 count=55 baseline=Reset resetSerial=167 pipelineWrites=0 nullBaseline=1 guard=1/0/0 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:18:58.616  [command-state] NR bypass: reason=null-guard-graphics-state-used token=36 registered=1 installing=0 dirty=0x0 count=56 baseline=Reset resetSerial=66 pipelineWrites=0 nullBaseline=1 guard=1/1/1 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:18:59.074  [perf] stage=C-NR-transfer epoch=606 transfer=1.390 samples=600 avgFPS=227.265 avgMs=4.4001 p95Ms=20.3629 p99Ms=22.5782 onePercentLowFPS=40.463
11:18:59.635  --- SuperSampling Evaluate #4200 handle=[address] generation=1 create=1707x960 -> 2560x1440 ---
11:18:59.635      Color                                      = [address] physical=1707x960 format=28(R8G8B8A8_UNORM) mips=1 flags=0x1 sample=1
11:18:59.635      MotionVectors                              = [address] physical=1707x960 format=34(R16G16_FLOAT) mips=1 flags=0x5 sample=1
11:18:59.635      Depth                                      = [address] physical=1707x960 format=41(R32_FLOAT) mips=1 flags=0x5 sample=1
11:18:59.635      Output                                     = [address] physical=2560x1440 format=28(R8G8B8A8_UNORM) mips=1 flags=0x5 sample=1
11:18:59.635      DLSS.Input.Color.Subrect.Base.X            = 0
11:18:59.636      DLSS.Input.Color.Subrect.Base.Y            = 0
11:18:59.636      DLSS.Input.Depth.Subrect.Base.X            = 0
11:18:59.636      DLSS.Input.Depth.Subrect.Base.Y            = 0
11:18:59.636      DLSS.Input.MV.Subrect.Base.X               = 0
11:18:59.636      DLSS.Input.MV.Subrect.Base.Y               = 0
11:18:59.636      DLSS.Output.Subrect.Base.X                 = 0
11:18:59.636      DLSS.Output.Subrect.Base.Y                 = 0
11:18:59.636      DLSS.Render.Subrect.Dimensions.Width       = 1707
11:18:59.636      DLSS.Render.Subrect.Dimensions.Height      = 960
11:18:59.636      MV.Scale.X                                 = 1707
11:18:59.637      MV.Scale.Y                                 = 960
11:18:59.637      Jitter.Offset.X                            = 0.25
11:18:59.637      Jitter.Offset.Y                            = -0.3888889
11:18:59.637      DLSS.Pre.Exposure                          = 1
11:18:59.637      DLSS.Exposure.Scale                        = 1
11:18:59.637      Reset                                      = 0
11:18:59.637  [stats] all NGX evaluations=20641, upscaling-carrier evaluations=4200 feature=1(SuperSampling)
11:18:59.637  [stats] session totals: Feature18-success=4136 transfer-prepared=4128 transfer-recorded=4128 transfer-bound=4128 transfer-submitted=4128 lifecycle-bypass=0; queue-affinity admitted=409 unknown-bypass=6 predicted-change-bypass=0 actual-mismatch=0.
11:18:59.637  [stats] current contract: generation=1 mode=2 carrier-requests=4200 Feature18-success=4136 transfer-prepared=4128 transfer-recorded=4128 transfer-bound=4128 transfer-submitted=4128.
11:19:00.111  [command-state] NR bypass: reason=null-guard-graphics-state-used token=31 registered=1 installing=0 dirty=0x0 count=58 baseline=Reset resetSerial=368 pipelineWrites=0 nullBaseline=1 guard=1/1/1 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:19:00.317  [command-state] NR bypass: reason=null-guard-graphics-state-used token=31 registered=1 installing=0 dirty=0x0 count=59 baseline=Reset resetSerial=369 pipelineWrites=0 nullBaseline=1 guard=1/1/1 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:19:00.339  [command-state] NR bypass: reason=native-hooks-unavailable token=28 registered=1 installing=1 dirty=0x0 count=60 baseline=Reset resetSerial=622 pipelineWrites=0 nullBaseline=1 guard=1/0/0 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:19:01.037  NR jitter actual: mode=1 state=forwarded input=valid,valid source=1707x960 work=1707x960 scale=1.000000,1.000000 value=0.000000,-0.166667.
11:19:01.038  NR-before-SR V2 evaluate succeeded: count=4200 work=1707x960 render=1707x960 mode=Reference scale=1.000 identity=0 strength=1.390
```

## 20260906-unpacked_shuhai_feedback_a__logs__nr-previous-summary.txt — KEY LINES (171) + TAIL
```
11:10:19.737  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.350 colorFollowRequested=1.000 intensity=1.020 style=1 autoMask=0 skin=-1.000 localTone=2.000 localStructure=1.300 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=557 feature18Evals=1374
11:10:22.492  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.320 colorFollowRequested=1.000 intensity=1.020 style=1 autoMask=0 skin=-1.000 localTone=2.000 localStructure=1.300 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=558 feature18Evals=1507
11:10:27.333  [stats] session totals: Feature18-success=1741 transfer-prepared=1733 transfer-recorded=1733 transfer-bound=1733 transfer-submitted=1733 lifecycle-bypass=0; queue-affinity admitted=348 unknown-bypass=8 predicted-change-bypass=0 actual-mismatch=0.
11:10:27.333  [stats] current contract: generation=1 mode=2 carrier-requests=1800 Feature18-success=1741 transfer-prepared=1733 transfer-recorded=1733 transfer-bound=1733 transfer-submitted=1733.
11:10:29.625  [command-state] NR bypass: reason=native-hooks-unavailable token=33 registered=1 installing=1 dirty=0x0 count=51 baseline=Reset resetSerial=188 pipelineWrites=0 nullBaseline=1 guard=1/0/0 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:10:29.760  [command-state] NR bypass: reason=native-hooks-unavailable token=36 registered=1 installing=1 dirty=0x0 count=52 baseline=Reset resetSerial=111 pipelineWrites=0 nullBaseline=1 guard=1/0/0 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:10:29.915  [command-state] NR bypass: reason=native-hooks-unavailable token=35 registered=1 installing=1 dirty=0x0 count=53 baseline=Reset resetSerial=69 pipelineWrites=0 nullBaseline=1 guard=1/0/0 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:10:31.589  [command-state] NR bypass: reason=native-hooks-unavailable token=45 registered=1 installing=1 dirty=0x0 count=54 baseline=Reset resetSerial=40 pipelineWrites=0 nullBaseline=1 guard=1/0/0 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:10:31.668  [command-state] NR bypass: reason=native-hooks-unavailable token=40 registered=1 installing=1 dirty=0x0 count=56 baseline=Reset resetSerial=58 pipelineWrites=0 nullBaseline=1 guard=1/0/0 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:10:32.258  [command-state] NR bypass: reason=null-guard-graphics-state-used token=33 registered=1 installing=0 dirty=0x0 count=57 baseline=Reset resetSerial=204 pipelineWrites=0 nullBaseline=1 guard=1/1/1 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:10:32.749  [command-state] NR bypass: reason=null-guard-graphics-state-used token=32 registered=1 installing=0 dirty=0x0 count=58 baseline=Reset resetSerial=145 pipelineWrites=0 nullBaseline=1 guard=1/1/1 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:10:33.413  [command-state] NR bypass: reason=null-guard-graphics-state-used token=29 registered=1 installing=0 dirty=0x0 count=59 baseline=Reset resetSerial=189 pipelineWrites=0 nullBaseline=1 guard=1/1/1 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:10:33.491  [command-state] NR bypass: reason=null-guard-graphics-state-used token=36 registered=1 installing=0 dirty=0x0 count=60 baseline=Reset resetSerial=123 pipelineWrites=0 nullBaseline=1 guard=1/1/1 tainted=0 dynamic=0x846 vb=0x0 so=0x0; unknown state remains native.
11:10:39.300  [stats] session totals: Feature18-success=2326 transfer-prepared=2318 transfer-recorded=2318 transfer-bound=2318 transfer-submitted=2318 lifecycle-bypass=0; queue-affinity admitted=386 unknown-bypass=8 predicted-change-bypass=0 actual-mismatch=0.
11:10:39.300  [stats] current contract: generation=1 mode=2 carrier-requests=2400 Feature18-success=2326 transfer-prepared=2318 transfer-recorded=2318 transfer-bound=2318 transfer-submitted=2318.
11:10:51.287  [stats] session totals: Feature18-success=2925 transfer-prepared=2917 transfer-recorded=2917 transfer-bound=2917 transfer-submitted=2917 lifecycle-bypass=0; queue-affinity admitted=395 unknown-bypass=8 predicted-change-bypass=0 actual-mismatch=0.
11:10:51.287  [stats] current contract: generation=1 mode=2 carrier-requests=3000 Feature18-success=2925 transfer-prepared=2917 transfer-recorded=2917 transfer-bound=2917 transfer-submitted=2917.
11:11:06.017  [stats] session totals: Feature18-success=3520 transfer-prepared=3508 transfer-recorded=3508 transfer-bound=3508 transfer-submitted=3508 lifecycle-bypass=0; queue-affinity admitted=543 unknown-bypass=8 predicted-change-bypass=0 actual-mismatch=0.
11:11:06.017  [stats] current contract: generation=1 mode=2 carrier-requests=3600 Feature18-success=3520 transfer-prepared=3508 transfer-recorded=3508 transfer-bound=3508 transfer-submitted=3508.
11:11:18.936  [stats] session totals: Feature18-success=4115 transfer-prepared=4101 transfer-recorded=4101 transfer-bound=4101 transfer-submitted=4101 lifecycle-bypass=0; queue-affinity admitted=627 unknown-bypass=8 predicted-change-bypass=0 actual-mismatch=0.
11:11:18.936  [stats] current contract: generation=1 mode=2 carrier-requests=4200 Feature18-success=4115 transfer-prepared=4101 transfer-recorded=4101 transfer-bound=4101 transfer-submitted=4101.
11:11:28.903  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.320 colorFollowRequested=1.000 intensity=1.020 style=1 autoMask=1 skin=0.000 localTone=2.000 localStructure=1.300 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=559 feature18Evals=4606
11:11:29.862  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.320 colorFollowRequested=1.000 intensity=1.020 style=1 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.300 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=560 feature18Evals=4634
11:11:31.813  [stats] session totals: Feature18-success=4711 transfer-prepared=4693 transfer-recorded=4693 transfer-bound=4693 transfer-submitted=4693 lifecycle-bypass=0; queue-affinity admitted=721 unknown-bypass=8 predicted-change-bypass=0 actual-mismatch=0.
11:11:31.813  [stats] current contract: generation=1 mode=2 carrier-requests=4800 Feature18-success=4711 transfer-prepared=4693 transfer-recorded=4693 transfer-bound=4693 transfer-submitted=4693.
11:11:36.842  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.320 colorFollowRequested=1.000 intensity=1.020 style=1 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.300 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=561 feature18Evals=4959
11:11:39.041  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.320 colorFollowRequested=1.000 intensity=1.020 style=1 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.300 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=562 feature18Evals=5068
11:11:42.182  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.000 colorFollowRequested=1.000 intensity=1.020 style=1 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.300 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=563 feature18Evals=5223
11:11:43.960  [stats] session totals: Feature18-success=5311 transfer-prepared=5293 transfer-recorded=5293 transfer-bound=5293 transfer-submitted=5293 lifecycle-bypass=0; queue-affinity admitted=763 unknown-bypass=8 predicted-change-bypass=0 actual-mismatch=0.
11:11:43.960  [stats] current contract: generation=1 mode=2 carrier-requests=5400 Feature18-success=5311 transfer-prepared=5293 transfer-recorded=5293 transfer-bound=5293 transfer-submitted=5293.
11:11:44.975  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.100 colorFollowRequested=1.000 intensity=1.020 style=1 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.300 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=564 feature18Evals=5361
11:11:46.641  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.320 colorFollowRequested=1.000 intensity=1.020 style=1 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.300 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=565 feature18Evals=5443
11:11:48.100  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=3.730 colorFollowRequested=1.000 intensity=1.020 style=1 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.300 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=566 feature18Evals=5515
11:11:49.091  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=2.150 colorFollowRequested=1.000 intensity=1.020 style=1 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.300 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=567 feature18Evals=5564
11:11:49.770  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=8.000 colorFollowRequested=1.000 intensity=1.020 style=1 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.300 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=568 feature18Evals=5597
11:11:50.928  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=2.350 colorFollowRequested=1.000 intensity=1.020 style=1 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.300 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=569 feature18Evals=5653
11:11:53.021  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.550 colorFollowRequested=1.000 intensity=1.020 style=1 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.300 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=570 feature18Evals=5756
11:11:55.608  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=1.000 intensity=1.020 style=1 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.300 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=571 feature18Evals=5884
11:11:56.151  [stats] session totals: Feature18-success=5911 transfer-prepared=5893 transfer-recorded=5893 transfer-bound=5893 transfer-submitted=5893 lifecycle-bypass=0; queue-affinity admitted=866 unknown-bypass=8 predicted-change-bypass=0 actual-mismatch=0.
11:11:56.151  [stats] current contract: generation=1 mode=2 carrier-requests=6000 Feature18-success=5911 transfer-prepared=5893 transfer-recorded=5893 transfer-bound=5893 transfer-submitted=5893.
11:12:08.311  [stats] session totals: Feature18-success=6500 transfer-prepared=6482 transfer-recorded=6482 transfer-bound=6482 transfer-submitted=6482 lifecycle-bypass=0; queue-affinity admitted=878 unknown-bypass=8 predicted-change-bypass=0 actual-mismatch=0.
11:12:08.311  [stats] current contract: generation=1 mode=2 carrier-requests=6600 Feature18-success=6500 transfer-prepared=6482 transfer-recorded=6482 transfer-bound=6482 transfer-submitted=6482.
11:12:25.646  [stats] session totals: Feature18-success=7097 transfer-prepared=7077 transfer-recorded=7077 transfer-bound=7077 transfer-submitted=7077 lifecycle-bypass=0; queue-affinity admitted=989 unknown-bypass=8 predicted-change-bypass=0 actual-mismatch=0.
11:12:25.646  [stats] current contract: generation=1 mode=2 carrier-requests=7200 Feature18-success=7097 transfer-prepared=7077 transfer-recorded=7077 transfer-bound=7077 transfer-submitted=7077.
11:12:29.744  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=1 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.300 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=572 feature18Evals=7298
11:12:31.100  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=1.000 intensity=1.020 style=1 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.300 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=573 feature18Evals=7363
11:12:31.893  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=1 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.300 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=574 feature18Evals=7402
11:12:36.271  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=1 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.300 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=1 epoch=575 feature18Evals=7614
11:12:37.248  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=1 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.300 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=576 feature18Evals=7662
11:12:37.880  [stats] session totals: Feature18-success=7693 transfer-prepared=7673 transfer-recorded=7673 transfer-bound=7673 transfer-submitted=7673 lifecycle-bypass=0; queue-affinity admitted=1027 unknown-bypass=8 predicted-change-bypass=0 actual-mismatch=0.
11:12:37.880  [stats] current contract: generation=1 mode=2 carrier-requests=7800 Feature18-success=7693 transfer-prepared=7673 transfer-recorded=7673 transfer-bound=7673 transfer-submitted=7673.
11:12:39.874  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=1 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.300 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=577 feature18Evals=7789
11:12:43.551  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=1 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.300 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=578 feature18Evals=7970
11:12:46.379  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.300 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=579 feature18Evals=8109
11:12:50.466  [stats] session totals: Feature18-success=8291 transfer-prepared=8265 transfer-recorded=8265 transfer-bound=8265 transfer-submitted=8265 lifecycle-bypass=0; queue-affinity admitted=1094 unknown-bypass=8 predicted-change-bypass=0 actual-mismatch=0.
11:12:50.466  [stats] current contract: generation=1 mode=2 carrier-requests=8400 Feature18-success=8291 transfer-prepared=8265 transfer-recorded=8265 transfer-bound=8265 transfer-submitted=8265.
11:13:22.234  [stats] session totals: Feature18-success=8887 transfer-prepared=8857 transfer-recorded=8857 transfer-bound=8857 transfer-submitted=8857 lifecycle-bypass=0; queue-affinity admitted=1228 unknown-bypass=8 predicted-change-bypass=0 actual-mismatch=0.
11:13:22.234  [stats] current contract: generation=1 mode=2 carrier-requests=9000 Feature18-success=8887 transfer-prepared=8857 transfer-recorded=8857 transfer-bound=8857 transfer-submitted=8857.
11:13:34.578  [stats] session totals: Feature18-success=9486 transfer-prepared=9456 transfer-recorded=9456 transfer-bound=9456 transfer-submitted=9456 lifecycle-bypass=0; queue-affinity admitted=1234 unknown-bypass=8 predicted-change-bypass=0 actual-mismatch=0.
11:13:34.578  [stats] current contract: generation=1 mode=2 carrier-requests=9600 Feature18-success=9486 transfer-prepared=9456 transfer-recorded=9456 transfer-bound=9456 transfer-submitted=9456.
11:13:36.422  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.520 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=580 feature18Evals=9575
11:13:38.239  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=581 feature18Evals=9648
11:13:39.633  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.190 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=582 feature18Evals=9702
11:13:42.890  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.310 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=583 feature18Evals=9846
11:13:48.131  [stats] session totals: Feature18-success=10086 transfer-prepared=10048 transfer-recorded=10048 transfer-bound=10048 transfer-submitted=10048 lifecycle-bypass=0; queue-affinity admitted=1405 unknown-bypass=8 predicted-change-bypass=0 actual-mismatch=0.
11:13:48.131  [stats] current contract: generation=1 mode=2 carrier-requests=10200 Feature18-success=10086 transfer-prepared=10048 transfer-recorded=10048 transfer-bound=10048 transfer-submitted=10048.
11:13:58.568  Config updated from F6: enabled=0 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.310 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=584 feature18Evals=10576
11:13:59.244  Config updated from F6: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.310 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=585 feature18Evals=10576
11:13:59.913  Config updated from F6: enabled=0 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.310 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=586 feature18Evals=10612
11:14:00.290  Config updated from F6: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.310 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=587 feature18Evals=10612
11:14:01.608  [stats] session totals: Feature18-success=10680 transfer-prepared=10638 transfer-recorded=10638 transfer-bound=10638 transfer-submitted=10638 lifecycle-bypass=0; queue-affinity admitted=1519 unknown-bypass=8 predicted-change-bypass=0 actual-mismatch=0.
11:14:01.608  [stats] current contract: generation=1 mode=2 carrier-requests=10800 Feature18-success=10680 transfer-prepared=10638 transfer-recorded=10638 transfer-bound=10638 transfer-submitted=10638.
11:14:42.165  [stats] session totals: Feature18-success=11277 transfer-prepared=11227 transfer-recorded=11227 transfer-bound=11227 transfer-submitted=11227 lifecycle-bypass=0; queue-affinity admitted=1646 unknown-bypass=8 predicted-change-bypass=0 actual-mismatch=0.
11:14:42.165  [stats] current contract: generation=1 mode=2 carrier-requests=11400 Feature18-success=11277 transfer-prepared=11227 transfer-recorded=11227 transfer-bound=11227 transfer-submitted=11227.
11:14:48.941  Config updated from F6: enabled=0 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.310 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=588 feature18Evals=11593
11:14:49.526  Config updated from F6: enabled=1 nrBeforeSr=1 transfer=1.390 colorFollowRequested=0.000 intensity=1.020 style=2 autoMask=0 skin=0.000 localTone=2.000 localStructure=1.310 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=589 feature18Evals=11593
11:14:56.095  [stats] session totals: Feature18-success=11874 transfer-prepared=11822 transfer-recorded=11822 transfer-bound=11822 transfer-submitted=11822 lifecycle-bypass=0; queue-affinity admitted=1715 unknown-bypass=8 predicted-change-bypass=0 actual-mismatch=0.
11:14:56.096  [stats] current contract: generation=1 mode=2 carrier-requests=12000 Feature18-success=11874 transfer-prepared=11822 transfer-recorded=11822 transfer-bound=11822 transfer-submitted=11822.
11:15:07.888  [stats] session totals: Feature18-success=12431 transfer-prepared=12379 transfer-recorded=12379 transfer-bound=12379 transfer-submitted=12379 lifecycle-bypass=0; queue-affinity admitted=1780 unknown-bypass=8 predicted-change-bypass=0 actual-mismatch=0.
11:15:07.888  [stats] current contract: generation=1 mode=2 carrier-requests=12600 Feature18-success=12431 transfer-prepared=12379 transfer-recorded=12379 transfer-bound=12379 transfer-submitted=12379.
--- TAIL ---
11:14:56.095      MV.Scale.Y                                 = 960
11:14:56.095      Jitter.Offset.X                            = -0.0625
11:14:56.095      Jitter.Offset.Y                            = 0.31481481
11:14:56.095      DLSS.Pre.Exposure                          = 1
11:14:56.095      DLSS.Exposure.Scale                        = 1
11:14:56.095      Reset                                      = 0
11:14:56.095  [stats] all NGX evaluations=59435, upscaling-carrier evaluations=12000 feature=1(SuperSampling)
11:14:56.095  [stats] session totals: Feature18-success=11874 transfer-prepared=11822 transfer-recorded=11822 transfer-bound=11822 transfer-submitted=11822 lifecycle-bypass=0; queue-affinity admitted=1715 unknown-bypass=8 predicted-change-bypass=0 actual-mismatch=0.
11:14:56.096  [stats] current contract: generation=1 mode=2 carrier-requests=12000 Feature18-success=11874 transfer-prepared=11822 transfer-recorded=11822 transfer-bound=11822 transfer-submitted=11822.
11:14:58.764  NR jitter actual: mode=1 state=forwarded input=valid,valid source=1707x960 work=1707x960 scale=1.000000,1.000000 value=-0.375000,-0.055556.
11:15:00.941  [perf] stage=C-NR-transfer epoch=589 transfer=1.390 samples=600 avgFPS=241.710 avgMs=4.1372 p95Ms=19.9609 p99Ms=21.5162 onePercentLowFPS=43.194
11:15:03.884  NR-before-SR V2 evaluate succeeded: count=2400 work=1707x960 render=1707x960 mode=Reference scale=1.000 identity=0 strength=1.390
11:15:06.241  [perf] stage=C-NR-transfer epoch=589 transfer=1.390 samples=600 avgFPS=216.694 avgMs=4.6148 p95Ms=18.1144 p99Ms=19.2978 onePercentLowFPS=49.085
11:15:07.885  --- SuperSampling Evaluate #12600 handle=[address] generation=1 create=1707x960 -> 2560x1440 ---
11:15:07.885      Color                                      = [address] physical=1707x960 format=28(R8G8B8A8_UNORM) mips=1 flags=0x1 sample=1
11:15:07.886      MotionVectors                              = [address] physical=1707x960 format=34(R16G16_FLOAT) mips=1 flags=0x5 sample=1
11:15:07.886      Depth                                      = [address] physical=1707x960 format=41(R32_FLOAT) mips=1 flags=0x5 sample=1
11:15:07.886      Output                                     = [address] physical=2560x1440 format=28(R8G8B8A8_UNORM) mips=1 flags=0x5 sample=1
11:15:07.886      DLSS.Input.Color.Subrect.Base.X            = 0
11:15:07.886      DLSS.Input.Color.Subrect.Base.Y            = 0
11:15:07.886      DLSS.Input.Depth.Subrect.Base.X            = 0
11:15:07.886      DLSS.Input.Depth.Subrect.Base.Y            = 0
11:15:07.886      DLSS.Input.MV.Subrect.Base.X               = 0
11:15:07.886      DLSS.Input.MV.Subrect.Base.Y               = 0
11:15:07.886      DLSS.Output.Subrect.Base.X                 = 0
11:15:07.886      DLSS.Output.Subrect.Base.Y                 = 0
11:15:07.886      DLSS.Render.Subrect.Dimensions.Width       = 1707
11:15:07.886      DLSS.Render.Subrect.Dimensions.Height      = 960
11:15:07.886      MV.Scale.X                                 = 1707
11:15:07.888      MV.Scale.Y                                 = 960
11:15:07.888      Jitter.Offset.X                            = -0.25
11:15:07.888      Jitter.Offset.Y                            = 0.16666669
11:15:07.888      DLSS.Pre.Exposure                          = 1
11:15:07.888      DLSS.Exposure.Scale                        = 1
11:15:07.888      Reset                                      = 0
11:15:07.888  [stats] all NGX evaluations=62165, upscaling-carrier evaluations=12600 feature=1(SuperSampling)
11:15:07.888  [stats] session totals: Feature18-success=12431 transfer-prepared=12379 transfer-recorded=12379 transfer-bound=12379 transfer-submitted=12379 lifecycle-bypass=0; queue-affinity admitted=1780 unknown-bypass=8 predicted-change-bypass=0 actual-mismatch=0.
11:15:07.888  [stats] current contract: generation=1 mode=2 carrier-requests=12600 Feature18-success=12431 transfer-prepared=12379 transfer-recorded=12379 transfer-bound=12379 transfer-submitted=12379.
11:15:11.317  NR jitter actual: mode=1 state=forwarded input=valid,valid source=1707x960 work=1707x960 scale=1.000000,1.000000 value=0.031250,0.425926.
11:15:11.320  [perf] stage=C-NR-transfer epoch=589 transfer=1.390 samples=600 avgFPS=243.592 avgMs=4.1052 p95Ms=19.2063 p99Ms=20.9234 onePercentLowFPS=38.196
```

## 20260906-unpacked_shuhai_feedback_a__logs__reshade-addon-summary.txt — TAIL
```
11:16:34:915 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {6F1D2B61-D5A0-11CF-BFC7-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:915 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {6F1D2B82-D5A0-11CF-BFC7-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:915 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {6F1D2B83-D5A0-11CF-BFC7-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:915 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {CFCF2B10-1172-11F0-8003-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:917 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {9B398800-38AA-11F0-8004-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:918 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {CFCF2B10-1172-11F0-8004-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:918 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {9B398800-38AA-11F0-8006-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:919 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {CFCF2B10-1172-11F0-800A-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:920 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {CFCF2B10-1172-11F0-800C-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:921 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {CFD19C10-1172-11F0-800E-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:935 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {CFD19C10-1172-11F0-8012-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:936 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {CFD19C10-1172-11F0-8015-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:936 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {D97B99F0-5820-11F1-8002-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:937 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {CFDDAA00-1172-11F0-8018-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:938 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {2E71A110-A21F-11F0-8001-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:939 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {2E76AA20-A21F-11F0-8002-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:941 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {5A3D17C0-721C-11F1-8002-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:942 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {D31D3F10-6977-11F1-8002-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:943 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {CFDDAA00-1172-11F0-801B-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:945 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {2E774660-A21F-11F0-8003-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:946 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {5A3CF0B0-721C-11F1-8001-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:946 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {D31D1800-6977-11F1-8001-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:947 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {CFDDAA00-1172-11F0-801D-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:948 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {5A3D8CF0-721C-11F1-8003-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:950 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {CFDDAA00-1172-11F0-801F-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:951 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {D31D6620-6977-11F1-8003-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:952 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {CFDDAA00-1172-11F0-8021-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:954 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {2E774660-A21F-11F0-8004-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:954 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {5A3DB400-721C-11F1-8004-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:955 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {D31D8D30-6977-11F1-8004-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:955 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {CFDDAA00-1172-11F0-8024-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:956 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {2E776D70-A21F-11F0-8005-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:957 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {5A3DDB10-721C-11F1-8005-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:958 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {D31D8D30-6977-11F1-8005-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:959 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {CFDDAA00-1172-11F0-8026-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:959 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {5A3E2930-721C-11F1-8006-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:960 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {D31DB440-6977-11F1-8006-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:966 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {5A3F3AA0-721C-11F1-8007-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:966 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {CFD19C10-1172-11F0-8016-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:967 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {A4A5EEB0-4D26-11F1-8001-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:970 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {2E776D70-A21F-11F0-8006-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:34:971 [ 5184] | INFO  | Redirecting IDirectInput8W::CreateDevice(this = [address], rguid = {CFE01B00-1172-11F0-8029-[address]}, lplpDirectInputDevice = [address], pUnkOuter = [address]) ...
11:16:35:368 [22432] | WARN  | RegisterRawInputDevices failed with error code 87.
11:16:41:813 [22432] | WARN  | RegisterRawInputDevices failed with error code 87.
11:17:23:509 [13172] | INFO  | Redirecting RegisterClassExA(lpWndClassEx = [address] { "{F54CC444-0903-46F8-AF1E-[address]}", style = 0x40 }) ...
11:17:23:511 [13172] | INFO  | Redirecting RegisterClassExA(lpWndClassEx = [address] { "BE4025FB-44A7-44a8-9A6E-[address]", style = 0x40 }) ...
11:17:40:335 [22456] | WARN  | Reference count for ID3D11Device0 object [address] ([address]) is inconsistent (22).
11:19:02:165 [13024] | INFO  | Redirecting CreateDXGIFactory2(Flags = 0, riid = {1BC6EA02-EF36-464F-BF0C-[address]}, ppFactory = [address]) ...
11:19:02:166 [13024] | INFO  | Redirecting CreateDXGIFactory2(Flags = 0, riid = {1BC6EA02-EF36-464F-BF0C-[address]}, ppFactory = [address]) ...
11:19:02:166 [13024] | INFO  | Redirecting CreateDXGIFactory2(Flags = 0, riid = {1BC6EA02-EF36-464F-BF0C-[address]}, ppFactory = [address]) ...
```
