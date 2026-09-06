# Cloud-readable fullish extract — issue #97

## 20260906-unpacked_360_feedback_a__capture-info.txt
```
format=DLSS5-Core-v1
capture.kind=core
session=
build=beta0.4.4-test1
language=zh-CN
exe.leaf=HTGame.exe
gpu=NVIDIA GeForce RTX 4080 SUPER
driver.raw=61664
driver.known=1
vendor=4318
api=49152
display.extent=3840x2160
display.storageFormat=24
display.colorSpace=1
snapshot.valid=1
requested.config=22
requested.enabled=1
requested.mode=2
requested.blend=1.20000005
requested.color=0.75
requested.workMode=1
requested.workScale=0.75
requested.lightingLock=0.200000003
requested.detailStability=0
requested.edgeGuard=0.25
requested.intensity=1
requested.localTone=1
requested.localStructure=1
requested.skin=0
requested.faceMask=1
requested.uiCorrection=1
requested.jitterMode=1
requested.style=2
applied.instance=0
applied.frame=0
applied.config=0
applied.intensity=1
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
start.commandState.sequence=7653
start.commandState.token=123
start.commandState.reason=none
start.commandState.route=pristine-null
start.commandState.registered=1
start.commandState.hooksReady=1
start.commandState.guardRequested=1
start.commandState.guardCovered=1
start.commandState.freshReset=1
start.commandState.tainted=0
start.commandState.baseline=Reset
start.commandState.resetSerial=12556
start.commandState.pipelineWrites=0
start.commandState.nullBaseline=1
start.commandState.hasPso=0
start.commandState.hasDxrPso=0
start.commandState.dirty=0x0
start.commandState.dynamic=0x0
start.commandState.vertexMask=0x0
start.commandState.streamMask=0x0
start.commandState.metaKnown=1
start.commandState.metaInit=0x1020
start.commandState.metaExecute=0x1020
start.commandState.rootHeapPresence=0,0,1,1
start.commandState.rootArgumentCounts=0,0
start.ngxHookFailure.available=0
start.nrSuccess=0
start.transferSubmitted=0
start.bypass=command-state-unverified
start.srCarrierEvaluations=7653
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
start.commandSignatureObserved=6
start.commandSignatureUnknownUses=0
end.commandState.available=1
end.commandState.schema=1
end.commandState.ageMs=0
end.commandState.sequence=7689
end.commandState.token=119
end.commandState.reason=none
end.commandState.route=pristine-null
end.commandState.registered=1
end.commandState.hooksReady=1
end.commandState.guardRequested=1
end.commandState.guardCovered=1
end.commandState.freshReset=1
end.commandState.tainted=0
end.commandState.baseline=Reset
end.commandState.resetSerial=12233
end.commandState.pipelineWrites=0
end.commandState.nullBaseline=1
end.commandState.hasPso=0
end.commandState.hasDxrPso=0
end.commandState.dirty=0x0
end.commandState.dynamic=0x0
end.commandState.vertexMask=0x0
end.commandState.streamMask=0x0
end.commandState.metaKnown=1
end.commandState.metaInit=0x1020
end.commandState.metaExecute=0x1020
end.commandState.rootHeapPresence=0,0,1,1
end.commandState.rootArgumentCounts=0,0
end.ngxHookFailure.available=0
end.nrSuccess=0
end.transferSubmitted=0
end.bypass=command-state-unverified
end.srCarrierEvaluations=7689
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
end.commandSignatureObserved=6
end.commandSignatureUnknownUses=0
collection.elapsedMs=750
collection.parameterSamples=0
collection.cancelled=0
artifact.sourceCommit=1a8490a2c76d93b0c5e4cc50b8d4f178df55eb83
artifact.sourceDirty=0
artifact.addonSha256=38386426f02f879b9f50219b1af9b868f97efd810bd93acb6fb69f75fb64c600
artifact.identityStatus=verified
module.0=dxgi.dll
module.1=d3d12.dll
module.2=d3d12.dll
module.3=DLSS5-AI???????????????-beta0.4.4-test1-@??????????????????-Bilibili.addon64
module.4=D3DCOMPILER_47.dll
module.5=nvwgf2umx.dll
module.6=_nvngx.dll
module.7=nvngx_deepdvc.dll
module.8=nvngx_dlss.dll
module.9=nvngx_dlssnr.dll
modules.contract=names only; no module bytes or process addresses collected
color.status=bypassed
color.requested=0
color.effective=0
color.reason=unknown-live-contract
color.contract=last recorded blend; not a measured picture strength
collection.metadataTruncated=0

```

## 20260906-unpacked_360_feedback_a__logs__nr-current-summary.txt — KEY LINES (95) + TAIL
```
10:15:35.715  [state] bypass transition #1: waiting-feature1 -> command-state-unverified.
10:15:35.731  [stats] current contract: generation=2 mode=2 carrier-requests=2 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
10:15:35.731  [command-state] NR bypass: reason=native-hooks-unavailable token=18 registered=1 installing=1 dirty=0x0 count=2 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
10:15:35.774  [stats] current contract: generation=2 mode=2 carrier-requests=3 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
10:15:35.775  [command-state] NR bypass: reason=native-hooks-unavailable token=39 registered=1 installing=1 dirty=0x0 count=3 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
10:15:35.790  [command-state] NR bypass: reason=native-hooks-unavailable token=53 registered=1 installing=1 dirty=0x0 count=4 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
10:15:35.803  [command-state] NR bypass: reason=native-hooks-unavailable token=44 registered=1 installing=1 dirty=0x0 count=5 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
10:15:35.831  [command-state] NR bypass: reason=native-hooks-unavailable token=175 registered=1 installing=1 dirty=0x0 count=6 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
10:15:36.366  [command-state] NR bypass: reason=native-hooks-unavailable token=145 registered=1 installing=1 dirty=0x0 count=34 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0 guard=0/0/0 tainted=0 dynamic=0x0 vb=0x0 so=0x0; unknown state remains native.
10:15:47.161  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
10:15:47.162  [stats] current contract: generation=2 mode=2 carrier-requests=600 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
10:15:58.649  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
10:15:58.649  [stats] current contract: generation=2 mode=2 carrier-requests=1200 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
10:16:03.864  [state] bypass transition #2: command-state-unverified -> waiting-feature1.
10:16:03.871  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=0 transfer=1.200 colorFollowRequested=0.750 intensity=1.000 style=0 autoMask=0 skin=-1.000 localTone=1.000 localStructure=1.000 uiCorrection=1 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=2 feature18Evals=0
10:16:03.882  [stats] current contract: generation=2 mode=1 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
10:16:03.900  [stats] current contract: generation=2 mode=1 carrier-requests=2 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
10:16:03.919  [stats] current contract: generation=2 mode=1 carrier-requests=3 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
10:16:10.207  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
10:16:13.131  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=0 transfer=1.200 colorFollowRequested=0.750 intensity=1.000 style=2 autoMask=0 skin=-1.000 localTone=1.000 localStructure=1.000 uiCorrection=1 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=3 feature18Evals=0
10:16:15.396  [stats] current contract: generation=2 mode=1 carrier-requests=600 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
10:16:17.867  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=0 transfer=1.200 colorFollowRequested=0.750 intensity=1.000 style=2 autoMask=0 skin=-1.000 localTone=1.000 localStructure=1.000 uiCorrection=1 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=1 epoch=4 feature18Evals=0
10:16:21.635  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
10:16:22.831  [state] bypass transition #3: waiting-feature1 -> command-state-unverified.
10:16:26.801  [stats] current contract: generation=2 mode=1 carrier-requests=1200 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
10:16:27.960  [state] bypass transition #4: command-state-unverified -> master-disabled.
10:16:27.969  Config updated from ReShade simplified settings: enabled=0 nrBeforeSr=0 transfer=1.200 colorFollowRequested=0.750 intensity=1.000 style=2 autoMask=0 skin=-1.000 localTone=1.000 localStructure=1.000 uiCorrection=1 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=1 epoch=5 feature18Evals=0
10:16:29.522  [state] bypass transition #5: master-disabled -> waiting-feature1.
10:16:29.530  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=0 transfer=1.200 colorFollowRequested=0.750 intensity=1.000 style=2 autoMask=0 skin=-1.000 localTone=1.000 localStructure=1.000 uiCorrection=1 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=1 epoch=6 feature18Evals=0
10:16:29.947  [state] bypass transition #6: waiting-feature1 -> command-state-unverified.
10:16:34.474  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
10:16:39.531  [stats] current contract: generation=2 mode=1 carrier-requests=1800 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
10:16:45.632  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
10:16:50.717  [stats] current contract: generation=2 mode=1 carrier-requests=2400 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
10:16:52.147  [state] bypass transition #7: command-state-unverified -> master-disabled.
10:16:52.154  Config updated from ReShade simplified settings: enabled=0 nrBeforeSr=0 transfer=1.200 colorFollowRequested=0.750 intensity=1.000 style=2 autoMask=0 skin=-1.000 localTone=1.000 localStructure=1.000 uiCorrection=1 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=1 epoch=7 feature18Evals=0
10:16:53.213  [state] bypass transition #8: master-disabled -> waiting-feature1.
10:16:53.220  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=0 transfer=1.200 colorFollowRequested=0.750 intensity=1.000 style=2 autoMask=0 skin=-1.000 localTone=1.000 localStructure=1.000 uiCorrection=1 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=1 epoch=8 feature18Evals=0
10:16:56.425  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.200 colorFollowRequested=0.750 intensity=1.000 style=2 autoMask=0 skin=-1.000 localTone=1.000 localStructure=1.000 uiCorrection=1 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=1 epoch=9 feature18Evals=0
10:16:56.433  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
10:16:56.451  [stats] current contract: generation=2 mode=2 carrier-requests=2 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
10:16:56.470  [stats] current contract: generation=2 mode=2 carrier-requests=3 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
10:16:57.912  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
10:16:58.130  [state] bypass transition #9: waiting-feature1 -> master-disabled.
10:16:58.138  Config updated from ReShade simplified settings: enabled=0 nrBeforeSr=1 transfer=1.200 colorFollowRequested=0.750 intensity=1.000 style=2 autoMask=0 skin=-1.000 localTone=1.000 localStructure=1.000 uiCorrection=1 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=1 epoch=10 feature18Evals=0
10:16:58.393  [state] bypass transition #10: master-disabled -> waiting-feature1.
10:16:58.400  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.200 colorFollowRequested=0.750 intensity=1.000 style=2 autoMask=0 skin=-1.000 localTone=1.000 localStructure=1.000 uiCorrection=1 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=1 epoch=11 feature18Evals=0
10:17:01.791  [state] bypass transition #11: waiting-feature1 -> master-disabled.
10:17:01.797  Config updated from ReShade simplified settings: enabled=0 nrBeforeSr=1 transfer=1.200 colorFollowRequested=0.750 intensity=1.000 style=2 autoMask=0 skin=-1.000 localTone=1.000 localStructure=1.000 uiCorrection=1 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=1 epoch=12 feature18Evals=0
10:17:02.486  [state] bypass transition #12: master-disabled -> waiting-feature1.
10:17:02.494  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.200 colorFollowRequested=0.750 intensity=1.000 style=2 autoMask=0 skin=-1.000 localTone=1.000 localStructure=1.000 uiCorrection=1 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=1 epoch=13 feature18Evals=0
10:17:03.099  Config updated from ReShade simplified settings: enabled=0 nrBeforeSr=1 transfer=1.200 colorFollowRequested=0.750 intensity=1.000 style=2 autoMask=0 skin=-1.000 localTone=1.000 localStructure=1.000 uiCorrection=1 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=1 epoch=14 feature18Evals=0
10:17:03.533  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.200 colorFollowRequested=0.750 intensity=1.000 style=2 autoMask=0 skin=-1.000 localTone=1.000 localStructure=1.000 uiCorrection=1 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=1 epoch=15 feature18Evals=0
10:17:04.412  Config updated from ReShade simplified settings: enabled=0 nrBeforeSr=1 transfer=1.200 colorFollowRequested=0.750 intensity=1.000 style=2 autoMask=0 skin=-1.000 localTone=1.000 localStructure=1.000 uiCorrection=1 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=1 epoch=16 feature18Evals=0
10:17:04.904  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.200 colorFollowRequested=0.750 intensity=1.000 style=2 autoMask=0 skin=-1.000 localTone=1.000 localStructure=1.000 uiCorrection=1 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=1 epoch=17 feature18Evals=0
10:17:09.513  [stats] current contract: generation=2 mode=2 carrier-requests=600 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
10:17:11.135  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
10:17:22.118  [stats] current contract: generation=2 mode=2 carrier-requests=1200 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
10:17:23.253  Config updated from ReShade simplified settings: enabled=0 nrBeforeSr=1 transfer=1.200 colorFollowRequested=0.750 intensity=1.000 style=2 autoMask=0 skin=-1.000 localTone=1.000 localStructure=1.000 uiCorrection=1 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=1 epoch=18 feature18Evals=0
10:17:25.634  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.200 colorFollowRequested=0.750 intensity=1.000 style=2 autoMask=0 skin=-1.000 localTone=1.000 localStructure=1.000 uiCorrection=1 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=1 epoch=19 feature18Evals=0
10:17:26.073  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
10:17:36.273  [stats] current contract: generation=2 mode=2 carrier-requests=1800 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
10:17:37.901  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
10:17:48.525  [stats] current contract: generation=2 mode=2 carrier-requests=2400 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
10:17:50.166  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
10:17:51.791  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.200 colorFollowRequested=0.750 intensity=1.000 style=2 autoMask=1 skin=0.000 localTone=1.000 localStructure=1.000 uiCorrection=1 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=1 epoch=20 feature18Evals=0
10:18:00.864  [stats] current contract: generation=2 mode=2 carrier-requests=3000 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
10:18:02.534  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
10:15:36.341  [command-state] known-null snapshot: token=133 baseline=Reset resetSerial=35 route=pristine-null dynamic=0x0; native ClearState(NULL) restores the dummy PSO.
10:15:36.353  [command-state] restored native state: token=133 pso=[address] raytracing=[address] computeRoot=[address] graphicsRoot=[address] resourceHeap=[address] samplerHeap=[address].
10:15:36.353  [command-state] restored known-null baseline: token=133 origin=Reset resetSerial=35 dynamic=0x0.
10:15:36.829  [command-state] known-null snapshot: token=60 baseline=Reset resetSerial=51 route=pristine-null dynamic=0x0; native ClearState(NULL) restores the dummy PSO.
10:15:36.830  [command-state] restored native state: token=60 pso=[address] raytracing=[address] computeRoot=[address] graphicsRoot=[address] resourceHeap=[address] samplerHeap=[address].
10:15:36.830  [command-state] restored known-null baseline: token=60 origin=Reset resetSerial=51 dynamic=0x0.
10:15:37.059  [command-state] known-null snapshot: token=120 baseline=Reset resetSerial=87 route=pristine-null dynamic=0x0; native ClearState(NULL) restores the dummy PSO.
10:15:37.059  [command-state] restored native state: token=120 pso=[address] raytracing=[address] computeRoot=[address] graphicsRoot=[address] resourceHeap=[address] samplerHeap=[address].
10:15:37.060  [command-state] restored known-null baseline: token=120 origin=Reset resetSerial=87 dynamic=0x0.
10:15:37.201  [command-state] known-null snapshot: token=60 baseline=Reset resetSerial=84 route=pristine-null dynamic=0x0; native ClearState(NULL) restores the dummy PSO.
10:15:37.343  [command-state] known-null snapshot: token=198 baseline=Reset resetSerial=67 route=pristine-null dynamic=0x0; native ClearState(NULL) restores the dummy PSO.
10:15:37.418  [command-state] known-null snapshot: token=165 baseline=Reset resetSerial=106 route=pristine-null dynamic=0x0; native ClearState(NULL) restores the dummy PSO.
--- TAIL ---
10:17:50.165      MV.Scale.X                                 = 1
10:17:50.165      MV.Scale.Y                                 = 1
10:17:50.165      Jitter.Offset.X                            = -0.1875
10:17:50.165      Jitter.Offset.Y                            = -0.12962961
10:17:50.166      DLSS.Pre.Exposure                          = 1.8062292
10:17:50.166      DLSS.Exposure.Scale                        = 1
10:17:50.166      Reset                                      = 0
10:17:50.166  [stats] all NGX evaluations=13199, upscaling-carrier evaluations=6600 feature=1(SuperSampling)
10:17:50.166  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
10:17:51.579  [perf] stage=C-NR-transfer epoch=19 transfer=1.200 samples=600 avgFPS=97.605 avgMs=10.2454 p95Ms=22.8115 p99Ms=24.9445 onePercentLowFPS=38.400
10:17:51.791  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=1.200 colorFollowRequested=0.750 intensity=1.000 style=2 autoMask=1 skin=0.000 localTone=1.000 localStructure=1.000 uiCorrection=1 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=1 epoch=20 feature18Evals=0
10:17:51.792  [perf] reset stage=C-NR-transfer epoch=20 transfer=1.200; warming 180 frames
10:17:59.817  [perf] stage=C-NR-transfer epoch=20 transfer=1.200 samples=600 avgFPS=96.928 avgMs=10.3170 p95Ms=22.4667 p99Ms=25.3062 onePercentLowFPS=38.490
10:18:00.864  [stats] current contract: generation=2 mode=2 carrier-requests=3000 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
10:18:02.530  --- SuperSampling Evaluate #7200 handle=[address] generation=2 create=1920x1080 -> 3840x2160 ---
10:18:02.530      Color                                      = [address] physical=1920x1080 format=26(R11G11B10_FLOAT) mips=1 flags=0x1 sample=1
10:18:02.530      MotionVectors                              = [address] physical=1920x1080 format=34(R16G16_FLOAT) mips=1 flags=0x4 sample=1
10:18:02.531      Depth                                      = [address] physical=1920x1080 format=19(R32G8X24_TYPELESS) mips=1 flags=0x2 sample=1
10:18:02.531      Output                                     = [address] physical=3840x2160 format=10(R16G16B16A16_FLOAT) mips=1 flags=0x5 sample=1
10:18:02.531      DLSS.Input.Color.Subrect.Base.X            = 0
10:18:02.531      DLSS.Input.Color.Subrect.Base.Y            = 0
10:18:02.531      DLSS.Input.Depth.Subrect.Base.X            = 0
10:18:02.532      DLSS.Input.Depth.Subrect.Base.Y            = 0
10:18:02.532      DLSS.Input.MV.Subrect.Base.X               = 0
10:18:02.532      DLSS.Input.MV.Subrect.Base.Y               = 0
10:18:02.532      DLSS.Output.Subrect.Base.X                 = 0
10:18:02.532      DLSS.Output.Subrect.Base.Y                 = 0
10:18:02.532      DLSS.Render.Subrect.Dimensions.Width       = 1920
10:18:02.532      DLSS.Render.Subrect.Dimensions.Height      = 1080
10:18:02.532      MV.Scale.X                                 = 1
10:18:02.533      MV.Scale.Y                                 = 1
10:18:02.533      Jitter.Offset.X                            = -0.25
10:18:02.533      Jitter.Offset.Y                            = 0.16666669
10:18:02.533      DLSS.Pre.Exposure                          = 1.8062257
10:18:02.533      DLSS.Exposure.Scale                        = 1
10:18:02.533      Reset                                      = 0
10:18:02.533  [stats] all NGX evaluations=14399, upscaling-carrier evaluations=7200 feature=1(SuperSampling)
10:18:02.534  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
10:18:06.120  [perf] stage=C-NR-transfer epoch=20 transfer=1.200 samples=600 avgFPS=95.431 avgMs=10.4787 p95Ms=23.5679 p99Ms=25.6953 onePercentLowFPS=37.187
10:18:12.457  [perf] stage=C-NR-transfer epoch=20 transfer=1.200 samples=600 avgFPS=94.693 avgMs=10.5605 p95Ms=23.2245 p99Ms=25.1706 onePercentLowFPS=38.343
```

## 20260906-unpacked_360_feedback_a__logs__reshade-addon-summary.txt — TAIL
```
10:13:26:805 [ 9144] | INFO  |   | Flags                                   | 0x842                                   |
10:13:26:805 [ 9144] | INFO  |   +-----------------------------------------+-----------------------------------------+
10:13:26:823 [ 9144] | INFO  | Running on NVIDIA GeForce RTX 4080 SUPER Driver 616.64.
10:13:27:104 [ 9144] | INFO  | Recreated runtime environment on runtime [address] ('[local-path]
10:13:27:107 [ 9144] | INFO  | Redirecting ID3D12Device::CreateCommandQueue(this = [address], pDesc = [address], riid = {0EC870A6-5D7E-4C22-8CFC-[address]}, ppCommandQueue = [address]) ...
10:13:27:107 [ 9144] | INFO  | > Dumping command queue description:
10:13:27:107 [ 9144] | INFO  |   +-----------------------------------------+-----------------------------------------+
10:13:27:107 [ 9144] | INFO  |   | Parameter                               | Value                                   |
10:13:27:107 [ 9144] | INFO  |   +-----------------------------------------+-----------------------------------------+
10:13:27:107 [ 9144] | INFO  |   | Type                                    | 2                                       |
10:13:27:107 [ 9144] | INFO  |   | Priority                                | 100                                     |
10:13:27:107 [ 9144] | INFO  |   | Flags                                   | 0                                       |
10:13:27:107 [ 9144] | INFO  |   | NodeMask                                | 0                                       |
10:13:27:107 [ 9144] | INFO  |   +-----------------------------------------+-----------------------------------------+
10:13:27:133 [ 9144] | INFO  | Redirecting IDXGISwapChain::ResizeBuffers(this = [address], BufferCount = 2, Width = 3840, Height = 2160, NewFormat = 24, SwapChainFlags = 0x842) ...
10:13:27:133 [ 9144] | INFO  | Destroyed runtime environment on runtime [address] ('[local-path]
10:13:27:140 [ 9144] | INFO  | Recreated runtime environment on runtime [address] ('[local-path]
10:13:28:852 [ 9144] | INFO  | Redirecting D3D12CreateDevice(pAdapter = [address], MinimumFeatureLevel = 1000, riid = {77ACCE80-638E-4E65-8895-[address]}, ppDevice = [address]) ...
10:13:28:852 [ 9144] | INFO  | Redirecting D3D12CreateDevice(pAdapter = [address], MinimumFeatureLevel = 1000, riid = {77ACCE80-638E-4E65-8895-[address]}, ppDevice = [address]) ...
10:13:28:852 [ 9144] | INFO  | Redirecting D3D12CreateDevice(pAdapter = [address], MinimumFeatureLevel = 1000, riid = {77ACCE80-638E-4E65-8895-[address]}, ppDevice = [address]) ...
10:13:28:853 [ 9144] | INFO  | Redirecting D3D12CreateDevice(pAdapter = [address], MinimumFeatureLevel = 1000, riid = {77ACCE80-638E-4E65-8895-[address]}, ppDevice = [address]) ...
10:13:29:615 [ 9144] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
10:13:29:617 [ 9144] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
10:13:29:645 [ 9144] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
10:13:30:251 [20196] | INFO  | Redirecting D3D11CreateDevice(pAdapter = [address], DriverType = 1, Software = [address], Flags = 0, pFeatureLevels = [address], FeatureLevels = 3, SDKVersion = 7, ppDevice = [address], pFeatureLevel = [address], ppImmediateContext = [address]) ...
10:13:30:251 [20196] | INFO  | > Passing on to D3D11CreateDeviceAndSwapChain:
10:13:30:252 [20196] | INFO  | Redirecting D3D11CreateDeviceAndSwapChain(pAdapter = [address], DriverType = 1, Software = [address], Flags = 0, pFeatureLevels = [address], FeatureLevels = 3, SDKVersion = 7, pSwapChainDesc = [address], ppSwapChain = [address], ppDevice = [address], pFeatureLevel = [address], ppImmediateContext = [address]) ...
10:13:30:252 [20196] | INFO  | Redirecting CreateDXGIFactory2(Flags = 0x80000000, riid = {7B7166EC-21C7-44AE-B21A-[address]}, ppFactory = [address]) ...
10:13:30:308 [20196] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
10:13:30:341 [20196] | INFO  | Using feature level b000.
10:13:30:358 [20196] | INFO  | Redirecting CreateDXGIFactory1(riid = {7B7166EC-21C7-44AE-B21A-[address]}, ppFactory = [address]) ...
10:13:35:091 [ 9144] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
10:13:35:092 [ 9144] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
10:13:42:124 [23352] | INFO  | Redirecting RegisterClassW(lpWndClass = [address] { "CPGPMObilePcGameBridgeWnd", style = 0x3 }) ...
10:13:42:124 [23352] | INFO  | > Adding 'CS_OWNDC' window class style flag to "CPGPMObilePcGameBridgeWnd".
10:13:58:763 [ 9144] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
10:13:58:767 [ 9144] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
10:13:58:767 [ 9144] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
10:14:07:100 [ 9144] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
10:14:07:195 [ 9144] | INFO  | Redirecting RegisterClassExA(lpWndClassEx = [address] { "CustomHDHandle_13026733040", style = 0 }) ...
10:14:07:232 [ 9144] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
10:14:39:821 [ 9144] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
10:14:39:822 [ 9144] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
10:15:07:784 [ 9144] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
10:15:11:800 [ 9144] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
10:15:20:412 [ 9144] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
10:15:25:663 [ 9144] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
10:15:25:664 [ 9144] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
10:17:11:211 [ 5084] | INFO  | Redirecting RegisterClassExA(lpWndClassEx = [address] { "{F54CC444-0903-46F8-AF1E-[address]}", style = 0x40 }) ...
10:17:11:213 [ 5084] | INFO  | Redirecting RegisterClassExA(lpWndClassEx = [address] { "BE4025FB-44A7-44a8-9A6E-[address]", style = 0x40 }) ...
```
