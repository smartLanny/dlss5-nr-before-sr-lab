# Cloud-readable fullish extract — issue #101

## 20260906-unpacked_wang_feedback_small__capture-info.txt
```
format=DLSS5-Core-v1
capture.kind=core
session=
build=beta0.4.4-test1
language=zh-CN
exe.leaf=HTGame.exe
gpu=NVIDIA GeForce RTX 5080
driver.raw=61664
driver.known=1
vendor=4318
api=49152
display.extent=3440x1440
display.storageFormat=24
display.colorSpace=1
snapshot.valid=1
requested.config=16
requested.enabled=1
requested.mode=1
requested.blend=2
requested.color=1
requested.workMode=0
requested.workScale=0.75
requested.lightingLock=0
requested.detailStability=0
requested.edgeGuard=0
requested.intensity=1.5
requested.localTone=1.60000002
requested.localStructure=2
requested.skin=0
requested.faceMask=1
requested.uiCorrection=0
requested.jitterMode=1
requested.style=2
applied.instance=4
applied.frame=5600
applied.config=15
applied.intensity=1.5
applied.skin=0
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
start.commandState.sequence=7630
start.commandState.token=80
start.commandState.reason=null-guard-graphics-state-used
start.commandState.route=native-bypass
start.commandState.registered=1
start.commandState.hooksReady=1
start.commandState.guardRequested=1
start.commandState.guardCovered=1
start.commandState.freshReset=1
start.commandState.tainted=0
start.commandState.baseline=Reset
start.commandState.resetSerial=8424
start.commandState.pipelineWrites=0
start.commandState.nullBaseline=1
start.commandState.hasPso=0
start.commandState.hasDxrPso=0
start.commandState.dirty=0x0
start.commandState.dynamic=0x8
start.commandState.vertexMask=0x0
start.commandState.streamMask=0x0
start.commandState.metaKnown=1
start.commandState.metaInit=0x1020
start.commandState.metaExecute=0x1020
start.commandState.rootHeapPresence=0,0,1,1
start.commandState.rootArgumentCounts=0,0
start.ngxHookFailure.available=0
start.nrSuccess=5600
start.transferSubmitted=5580
start.bypass=command-state-unverified
start.srCarrierEvaluations=6842
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
end.commandState.ageMs=16
end.commandState.sequence=7690
end.commandState.token=177
end.commandState.reason=null-guard-graphics-state-used
end.commandState.route=native-bypass
end.commandState.registered=1
end.commandState.hooksReady=1
end.commandState.guardRequested=1
end.commandState.guardCovered=1
end.commandState.freshReset=1
end.commandState.tainted=0
end.commandState.baseline=Reset
end.commandState.resetSerial=5403
end.commandState.pipelineWrites=0
end.commandState.nullBaseline=1
end.commandState.hasPso=0
end.commandState.hasDxrPso=0
end.commandState.dirty=0x0
end.commandState.dynamic=0x8
end.commandState.vertexMask=0x0
end.commandState.streamMask=0x0
end.commandState.metaKnown=1
end.commandState.metaInit=0x1020
end.commandState.metaExecute=0x1020
end.commandState.rootHeapPresence=0,0,1,1
end.commandState.rootArgumentCounts=0,0
end.ngxHookFailure.available=0
end.nrSuccess=5600
end.transferSubmitted=5580
end.bypass=command-state-unverified
end.srCarrierEvaluations=6872
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
module.9=nrchain_nvngx.dll
module.10=nvngx_dlssnr.dll
modules.contract=names only; no module bytes or process addresses collected
color.status=bypassed
color.requested=1
color.effective=0
color.reason=unverified-hdr-luminance-only
color.contract=last recorded blend; not a measured picture strength
collection.metadataTruncated=0

```

## 20260906-unpacked_wang_feedback_small__logs__nr-current-summary.txt — KEY LINES (153) + TAIL
```
14:32:37.392  [stats] session totals: Feature18-success=2235 transfer-prepared=2616 transfer-recorded=2227 transfer-bound=2227 transfer-submitted=2227 lifecycle-bypass=0; queue-affinity admitted=9 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
14:32:51.927  [stats] current contract: generation=1 mode=2 carrier-requests=600 Feature18-success=579 transfer-prepared=577 transfer-recorded=577 transfer-bound=577 transfer-submitted=577.
14:32:54.251  [stats] session totals: Feature18-success=2817 transfer-prepared=3198 transfer-recorded=2809 transfer-bound=2809 transfer-submitted=2809 lifecycle-bypass=0; queue-affinity admitted=9 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
14:33:01.698  Config updated from ReShade simplified settings: enabled=0 nrBeforeSr=1 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1229 feature18Evals=3071
14:33:02.222  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1230 feature18Evals=3071
14:33:09.854  [stats] current contract: generation=1 mode=2 carrier-requests=1200 Feature18-success=1175 transfer-prepared=1171 transfer-recorded=1171 transfer-bound=1171 transfer-submitted=1171.
14:33:12.250  [stats] session totals: Feature18-success=3414 transfer-prepared=3793 transfer-recorded=3404 transfer-bound=3404 transfer-submitted=3404 lifecycle-bypass=0; queue-affinity admitted=10 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
14:33:14.765  Config updated from F6: enabled=0 nrBeforeSr=1 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1231 feature18Evals=3497
14:33:15.321  Config updated from F6: enabled=1 nrBeforeSr=1 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1232 feature18Evals=3497
14:33:23.956  [state] bypass transition #1200: none/active -> command-state-unverified.
14:33:28.129  [stats] current contract: generation=1 mode=2 carrier-requests=1800 Feature18-success=1727 transfer-prepared=1721 transfer-recorded=1721 transfer-bound=1721 transfer-submitted=1721.
14:33:30.506  [stats] session totals: Feature18-success=3964 transfer-prepared=4341 transfer-recorded=3952 transfer-bound=3952 transfer-submitted=3952 lifecycle-bypass=0; queue-affinity admitted=10 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
14:33:45.933  [stats] current contract: generation=1 mode=2 carrier-requests=2400 Feature18-success=2318 transfer-prepared=2312 transfer-recorded=2312 transfer-bound=2312 transfer-submitted=2312.
14:33:48.276  [stats] session totals: Feature18-success=4557 transfer-prepared=4934 transfer-recorded=4545 transfer-bound=4545 transfer-submitted=4545 lifecycle-bypass=0; queue-affinity admitted=10 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
14:33:52.127  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.375  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.381  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.400  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.405  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.435  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.437  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.457  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.461  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.493  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.500  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.526  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.531  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.555  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.561  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.581  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.585  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.610  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.613  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.646  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.650  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.706  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.710  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.724  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.728  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.756  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.758  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.791  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.794  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.824  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.827  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.859  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.862  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.892  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.895  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.930  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.933  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.960  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.963  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:52.987  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:53.114  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:53.122  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:53.131  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:53.146  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:53.153  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:53.175  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:53.399  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:53.416  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:53.420  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:53.447  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:53.452  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:53.474  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:53.479  [stats] current contract: generation=2 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:53.502  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:33:53.531  [stats] current contract: generation=1 mode=2 carrier-requests=2 Feature18-success=1 transfer-prepared=1 transfer-recorded=1 transfer-bound=1 transfer-submitted=1.
14:33:53.549  [stats] current contract: generation=1 mode=2 carrier-requests=3 Feature18-success=2 transfer-prepared=2 transfer-recorded=2 transfer-bound=2 transfer-submitted=2.
14:34:05.559  [stats] session totals: Feature18-success=5157 transfer-prepared=5526 transfer-recorded=5137 transfer-bound=5137 transfer-submitted=5137 lifecycle-bypass=0; queue-affinity admitted=25 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
14:34:10.915  [stats] current contract: generation=1 mode=2 carrier-requests=600 Feature18-success=599 transfer-prepared=599 transfer-recorded=599 transfer-bound=599 transfer-submitted=599.
14:34:18.673  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1233 feature18Evals=5600
14:34:18.688  [stats] current contract: generation=1 mode=1 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:34:18.710  [stats] current contract: generation=1 mode=1 carrier-requests=2 Feature18-success=0 transfer-prepared=1 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:34:18.733  [stats] current contract: generation=1 mode=1 carrier-requests=3 Feature18-success=0 transfer-prepared=2 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:34:22.567  [stats] session totals: Feature18-success=5600 transfer-prepared=6125 transfer-recorded=5580 transfer-bound=5580 transfer-submitted=5580 lifecycle-bypass=0; queue-affinity admitted=25 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
14:34:23.899  [post-SR] stopped after successful native SR: stage=command-state reason=null-guard-graphics-state-used prepared-only=1 count=600; original Output retained, no Feature18 call was made.
14:34:25.704  [state] bypass transition #1800: none/active -> command-state-unverified.
14:34:27.507  [command-state] NR bypass: reason=null-guard-graphics-state-used token=16 registered=1 installing=0 dirty=0x0 count=1200 baseline=Reset resetSerial=8579 pipelineWrites=0 nullBaseline=1 guard=1/1/1 tainted=0 dynamic=0x8 vb=0x0 so=0x0; unknown state remains native.
--- TAIL ---
14:34:11.006  [perf] stage=C-NR-transfer epoch=1232 transfer=2.000 samples=600 avgFPS=138.873 avgMs=7.2008 p95Ms=29.1482 p99Ms=31.1799 onePercentLowFPS=31.013
14:34:12.600  NR jitter actual: mode=1 state=forwarded input=valid,valid source=2295x961 work=2295x961 scale=1.000000,1.000000 value=-0.437500,0.388889.
14:34:16.561  [module] lazy rescan #240 core=[address] coreLifecycle=1 dlssd=[address] dlssdLifecycle=0 observerGap=0 startupCovered=0 feature1CreateAttempts=2 feature1CreateSuccess=2 feature1CreateInFlight=0 observerEpoch=0 nullEvals=0 carrierEvals=6372 observerFault=none.
14:34:18.673  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1233 feature18Evals=5600
14:34:18.674  [perf] reset stage=B-post-SR-NR epoch=1233 transfer=2.000; warming 180 frames
14:34:18.688  Pipeline diagnostic scope reset: epoch=62 Feature1 generation=1 mode=1.
14:34:18.688  [stats] current contract: generation=1 mode=1 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:34:18.691  Post-SR native FP16 Output retained: active=3440x1440 copied byte-exactly to owned snapshot; no SR target redirection.
14:34:18.710  [stats] current contract: generation=1 mode=1 carrier-requests=2 Feature18-success=0 transfer-prepared=1 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:34:18.715  Post-SR native FP16 Output retained: active=3440x1440 copied byte-exactly to owned snapshot; no SR target redirection.
14:34:18.733  [stats] current contract: generation=1 mode=1 carrier-requests=3 Feature18-success=0 transfer-prepared=2 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
14:34:20.496  Post-SR native FP16 Output retained: active=3440x1440 copied byte-exactly to owned snapshot; no SR target redirection.
14:34:22.564  --- SuperSampling Evaluate #6600 handle=[address] generation=1 create=2295x961 -> 3440x1440 ---
14:34:22.565      Color                                      = [address] physical=2296x964 format=26(R11G11B10_FLOAT) mips=1 flags=0x1 sample=1
14:34:22.565      MotionVectors                              = [address] physical=2295x961 format=34(R16G16_FLOAT) mips=1 flags=0x4 sample=1
14:34:22.565      Depth                                      = [address] physical=2296x964 format=19(R32G8X24_TYPELESS) mips=1 flags=0x2 sample=1
14:34:22.565      Output                                     = [address] physical=3440x1440 format=10(R16G16B16A16_FLOAT) mips=1 flags=0x5 sample=1
14:34:22.565      DLSS.Input.Color.Subrect.Base.X            = 0
14:34:22.566      DLSS.Input.Color.Subrect.Base.Y            = 0
14:34:22.566      DLSS.Input.Depth.Subrect.Base.X            = 0
14:34:22.566      DLSS.Input.Depth.Subrect.Base.Y            = 0
14:34:22.566      DLSS.Input.MV.Subrect.Base.X               = 0
14:34:22.566      DLSS.Input.MV.Subrect.Base.Y               = 0
14:34:22.566      DLSS.Output.Subrect.Base.X                 = 0
14:34:22.566      DLSS.Output.Subrect.Base.Y                 = 0
14:34:22.566      DLSS.Render.Subrect.Dimensions.Width       = 2295
14:34:22.566      DLSS.Render.Subrect.Dimensions.Height      = 961
14:34:22.566      MV.Scale.X                                 = 1
14:34:22.566      MV.Scale.Y                                 = 1
14:34:22.567      Jitter.Offset.X                            = -0.125
14:34:22.567      Jitter.Offset.Y                            = -0.27777776
14:34:22.567      DLSS.Pre.Exposure                          = 1.2730614
14:34:22.567      DLSS.Exposure.Scale                        = 1
14:34:22.567      Reset                                      = 0
14:34:22.567  [stats] all NGX evaluations=26319, upscaling-carrier evaluations=6600 feature=1(SuperSampling)
14:34:22.567  [stats] session totals: Feature18-success=5600 transfer-prepared=6125 transfer-recorded=5580 transfer-bound=5580 transfer-submitted=5580 lifecycle-bypass=0; queue-affinity admitted=25 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
14:34:23.899  [post-SR] stopped after successful native SR: stage=command-state reason=null-guard-graphics-state-used prepared-only=1 count=600; original Output retained, no Feature18 call was made.
14:34:25.704  [state] bypass transition #1800: none/active -> command-state-unverified.
14:34:27.189  [perf] stage=B-post-SR-NR epoch=1233 transfer=2.000 samples=600 avgFPS=161.970 avgMs=6.1740 p95Ms=24.1771 p99Ms=25.9430 onePercentLowFPS=37.029
14:34:27.507  [command-state] NR bypass: reason=null-guard-graphics-state-used token=16 registered=1 installing=0 dirty=0x0 count=1200 baseline=Reset resetSerial=8579 pipelineWrites=0 nullBaseline=1 guard=1/1/1 tainted=0 dynamic=0x8 vb=0x0 so=0x0; unknown state remains native.
```

## 20260906-unpacked_wang_feedback_small__logs__nr-previous-summary.txt — KEY LINES (98) + TAIL
```
00:04:27.944  [stats] current contract: generation=1 mode=2 carrier-requests=3 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
00:04:27.944  [command-state] NR bypass: reason=native-hooks-unavailable token=42 registered=1 installing=1 dirty=0x0 count=3 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0; successful native Reset and game bindings can recover.
00:04:27.954  [command-state] NR bypass: reason=native-hooks-unavailable token=17 registered=1 installing=1 dirty=0x0 count=4 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0; successful native Reset and game bindings can recover.
00:04:27.967  [command-state] NR bypass: reason=native-hooks-unavailable token=18 registered=1 installing=1 dirty=0x0 count=5 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0; successful native Reset and game bindings can recover.
00:04:27.976  [command-state] NR bypass: reason=native-hooks-unavailable token=65 registered=1 installing=1 dirty=0x0 count=6 baseline=unobserved resetSerial=0 pipelineWrites=0 nullBaseline=0; successful native Reset and game bindings can recover.
00:04:43.400  [command-state] NR bypass: reason=null-pso-policy-disabled token=59 registered=1 installing=0 dirty=0x0 count=600 baseline=Reset resetSerial=351 pipelineWrites=0 nullBaseline=1; successful native Reset and game bindings can recover.
00:04:43.400  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
00:04:43.400  [stats] current contract: generation=1 mode=2 carrier-requests=600 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
00:04:59.335  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
00:04:59.335  [stats] current contract: generation=1 mode=2 carrier-requests=1200 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
00:04:59.335  [command-state] NR bypass: reason=null-pso-policy-disabled token=51 registered=1 installing=0 dirty=0x0 count=1200 baseline=Reset resetSerial=1534 pipelineWrites=0 nullBaseline=1; successful native Reset and game bindings can recover.
00:05:04.982  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
00:05:04.982  [stats] current contract: generation=1 mode=2 carrier-requests=1800 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
00:05:04.982  [command-state] NR bypass: reason=null-pso-policy-disabled token=44 registered=1 installing=0 dirty=0x0 count=1800 baseline=Reset resetSerial=2093 pipelineWrites=0 nullBaseline=1; successful native Reset and game bindings can recover.
00:05:10.026  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
00:05:10.026  [stats] current contract: generation=1 mode=2 carrier-requests=2400 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
00:05:10.026  [command-state] NR bypass: reason=null-pso-policy-disabled token=75 registered=1 installing=0 dirty=0x0 count=2400 baseline=Reset resetSerial=3043 pipelineWrites=0 nullBaseline=1; successful native Reset and game bindings can recover.
00:05:35.250  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
00:05:35.251  [stats] current contract: generation=1 mode=2 carrier-requests=3000 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
00:05:35.251  [command-state] NR bypass: reason=null-pso-policy-disabled token=64 registered=1 installing=0 dirty=0x0 count=3000 baseline=Reset resetSerial=4097 pipelineWrites=0 nullBaseline=1; successful native Reset and game bindings can recover.
00:05:48.658  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
00:05:48.658  [stats] current contract: generation=1 mode=2 carrier-requests=3600 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
00:05:48.658  [command-state] NR bypass: reason=null-pso-policy-disabled token=78 registered=1 installing=0 dirty=0x0 count=3600 baseline=Reset resetSerial=4749 pipelineWrites=0 nullBaseline=1; successful native Reset and game bindings can recover.
00:06:03.460  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
00:06:03.460  [stats] current contract: generation=1 mode=2 carrier-requests=4200 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
00:06:03.460  [command-state] NR bypass: reason=null-pso-policy-disabled token=140 registered=1 installing=0 dirty=0x0 count=4200 baseline=Reset resetSerial=1871 pipelineWrites=0 nullBaseline=1; successful native Reset and game bindings can recover.
00:06:18.276  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
00:06:18.276  [stats] current contract: generation=1 mode=2 carrier-requests=4800 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
00:06:18.276  [command-state] NR bypass: reason=null-pso-policy-disabled token=132 registered=1 installing=0 dirty=0x0 count=4800 baseline=Reset resetSerial=2491 pipelineWrites=0 nullBaseline=1; successful native Reset and game bindings can recover.
00:06:18.671  [state] bypass transition #2: command-state-unverified -> master-disabled.
00:06:18.708  Config updated from ReShade simplified settings: enabled=0 nrBeforeSr=1 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1195 feature18Evals=0
00:06:20.277  [state] bypass transition #3: master-disabled -> waiting-feature1.
00:06:20.313  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1196 feature18Evals=0
00:06:20.313  [state] bypass transition #4: waiting-feature1 -> command-state-unverified.
00:06:24.448  [state] bypass transition #5: command-state-unverified -> master-disabled.
00:06:24.483  Config updated from F6: enabled=0 nrBeforeSr=1 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1197 feature18Evals=0
00:06:24.770  [state] bypass transition #6: master-disabled -> waiting-feature1.
00:06:24.815  Config updated from F6: enabled=1 nrBeforeSr=1 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1198 feature18Evals=0
00:06:24.815  [state] bypass transition #7: waiting-feature1 -> command-state-unverified.
00:06:28.350  [state] bypass transition #8: command-state-unverified -> waiting-feature1.
00:06:28.386  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1199 feature18Evals=0
00:06:28.386  [stats] current contract: generation=1 mode=1 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
00:06:28.387  [state] bypass transition #9: waiting-feature1 -> command-state-unverified.
00:06:28.404  [stats] current contract: generation=1 mode=1 carrier-requests=2 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
00:06:28.421  [stats] current contract: generation=1 mode=1 carrier-requests=3 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
00:06:29.363  [state] bypass transition #10: command-state-unverified -> master-disabled.
00:06:29.399  Config updated from ReShade simplified settings: enabled=0 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1200 feature18Evals=0
00:06:29.853  [state] bypass transition #11: master-disabled -> waiting-feature1.
00:06:29.892  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1201 feature18Evals=0
00:06:29.892  [state] bypass transition #12: waiting-feature1 -> command-state-unverified.
00:06:30.261  Config updated from ReShade simplified settings: enabled=0 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1202 feature18Evals=0
00:06:30.532  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1203 feature18Evals=0
00:06:31.076  Config updated from ReShade simplified settings: enabled=0 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1204 feature18Evals=0
00:06:31.573  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1205 feature18Evals=0
00:06:32.091  Config updated from ReShade simplified settings: enabled=0 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1206 feature18Evals=0
00:06:32.407  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1207 feature18Evals=0
00:06:33.473  Config updated from ReShade simplified settings: enabled=0 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1208 feature18Evals=0
00:06:33.889  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1209 feature18Evals=0
00:06:37.022  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
00:06:37.022  [command-state] NR bypass: reason=null-pso-policy-disabled token=31 registered=1 installing=0 dirty=0x0 count=5400 baseline=Reset resetSerial=7938 pipelineWrites=0 nullBaseline=1; successful native Reset and game bindings can recover.
00:06:43.211  [stats] current contract: generation=1 mode=1 carrier-requests=600 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
00:06:46.901  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
00:06:46.902  [command-state] NR bypass: reason=null-pso-policy-disabled token=144 registered=1 installing=0 dirty=0x0 count=6000 baseline=Reset resetSerial=5342 pipelineWrites=0 nullBaseline=1; successful native Reset and game bindings can recover.
00:06:51.351  [stats] current contract: generation=1 mode=1 carrier-requests=1200 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
00:06:55.961  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
00:06:55.961  [command-state] NR bypass: reason=null-pso-policy-disabled token=11 registered=1 installing=0 dirty=0x0 count=6600 baseline=Reset resetSerial=8882 pipelineWrites=0 nullBaseline=1; successful native Reset and game bindings can recover.
00:06:56.371  Config updated from ReShade simplified settings: enabled=0 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1210 feature18Evals=0
00:06:56.911  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1211 feature18Evals=0
00:06:57.311  Config updated from ReShade simplified settings: enabled=0 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1212 feature18Evals=0
00:06:57.773  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1213 feature18Evals=0
00:07:02.251  Config updated from ReShade simplified settings: enabled=0 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1214 feature18Evals=0
00:07:02.766  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1215 feature18Evals=0
00:07:05.637  [stats] current contract: generation=1 mode=1 carrier-requests=1800 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
00:07:08.076  Config updated from ReShade simplified settings: enabled=0 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1216 feature18Evals=0
00:07:08.397  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1217 feature18Evals=0
00:07:09.433  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1218 feature18Evals=0
00:07:09.433  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
00:07:09.447  [stats] current contract: generation=1 mode=2 carrier-requests=2 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
00:07:09.467  [stats] current contract: generation=1 mode=2 carrier-requests=3 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
00:07:10.199  Config updated from ReShade simplified settings: enabled=0 nrBeforeSr=1 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1219 feature18Evals=0
--- TAIL ---
00:06:55.960      DLSS.Render.Subrect.Dimensions.Width       = 2295
00:06:55.960      DLSS.Render.Subrect.Dimensions.Height      = 961
00:06:55.961      MV.Scale.X                                 = 1
00:06:55.961      MV.Scale.Y                                 = 1
00:06:55.961      Jitter.Offset.X                            = -0.1875
00:06:55.961      Jitter.Offset.Y                            = -0.12962961
00:06:55.961      DLSS.Pre.Exposure                          = 1.2750016
00:06:55.961      DLSS.Exposure.Scale                        = 1
00:06:55.961      Reset                                      = 0
00:06:55.961  [stats] all NGX evaluations=17972, upscaling-carrier evaluations=6600 feature=1(SuperSampling)
00:06:55.961  [stats] session totals: Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0 lifecycle-bypass=0; queue-affinity admitted=0 unknown-bypass=0 predicted-change-bypass=0 actual-mismatch=0.
00:06:55.961  [command-state] NR bypass: reason=null-pso-policy-disabled token=11 registered=1 installing=0 dirty=0x0 count=6600 baseline=Reset resetSerial=8882 pipelineWrites=0 nullBaseline=1; successful native Reset and game bindings can recover.
00:06:56.371  Config updated from ReShade simplified settings: enabled=0 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1210 feature18Evals=0
00:06:56.372  [perf] reset stage=A-SR-only epoch=1210 transfer=2.000; warming 180 frames
00:06:56.911  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1211 feature18Evals=0
00:06:56.912  [perf] reset stage=B-post-SR-NR epoch=1211 transfer=2.000; warming 180 frames
00:06:57.311  Config updated from ReShade simplified settings: enabled=0 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1212 feature18Evals=0
00:06:57.312  [perf] reset stage=A-SR-only epoch=1212 transfer=2.000; warming 180 frames
00:06:57.773  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1213 feature18Evals=0
00:06:57.776  [perf] reset stage=B-post-SR-NR epoch=1213 transfer=2.000; warming 180 frames
00:07:02.251  Config updated from ReShade simplified settings: enabled=0 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1214 feature18Evals=0
00:07:02.252  [perf] reset stage=A-SR-only epoch=1214 transfer=2.000; warming 180 frames
00:07:02.766  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1215 feature18Evals=0
00:07:02.767  [perf] reset stage=B-post-SR-NR epoch=1215 transfer=2.000; warming 180 frames
00:07:05.637  [stats] current contract: generation=1 mode=1 carrier-requests=1800 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
00:07:08.076  Config updated from ReShade simplified settings: enabled=0 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1216 feature18Evals=0
00:07:08.077  [perf] reset stage=A-SR-only epoch=1216 transfer=2.000; warming 180 frames
00:07:08.397  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=0 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1217 feature18Evals=0
00:07:08.398  [perf] reset stage=B-post-SR-NR epoch=1217 transfer=2.000; warming 180 frames
00:07:09.433  Config updated from ReShade simplified settings: enabled=1 nrBeforeSr=1 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1218 feature18Evals=0
00:07:09.433  Pipeline diagnostic scope reset: epoch=3 Feature1 generation=1 mode=2.
00:07:09.433  [stats] current contract: generation=1 mode=2 carrier-requests=1 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
00:07:09.438  [perf] reset stage=C-NR-transfer epoch=1218 transfer=2.000; warming 180 frames
00:07:09.447  [stats] current contract: generation=1 mode=2 carrier-requests=2 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
00:07:09.467  [stats] current contract: generation=1 mode=2 carrier-requests=3 Feature18-success=0 transfer-prepared=0 transfer-recorded=0 transfer-bound=0 transfer-submitted=0.
00:07:10.199  Config updated from ReShade simplified settings: enabled=0 nrBeforeSr=1 transfer=2.000 colorFollowRequested=1.000 intensity=1.500 style=2 autoMask=1 skin=0.000 localTone=1.600 localStructure=2.000 uiCorrection=0 jitterMode=1 paperWhiteScale=1.000 peakShoulderRequested=0 epoch=1219 feature18Evals=0
00:07:10.200  [perf] reset stage=A-SR-only epoch=1219 transfer=2.000; warming 180 frames
00:07:19.071  core ReleaseFeature handle=[address] id=11 (FrameGeneration)
00:07:19.112  D3D12 graphics queue retired identity=2 native=[address] live=1.
00:07:20.167  core ReleaseFeature handle=[address] id=1 (SuperSampling)
```

## 20260906-unpacked_wang_feedback_small__logs__reshade-addon-summary.txt — TAIL
```
14:30:05:442 [40312] | INFO  |   | SwapEffect                              | 4                                       |
14:30:05:442 [40312] | INFO  |   | AlphaMode                               | 0                                       |
14:30:05:442 [40312] | INFO  |   | Flags                                   | 0x842                                   |
14:30:05:442 [40312] | INFO  |   +-----------------------------------------+-----------------------------------------+
14:30:05:450 [40312] | INFO  | Running on NVIDIA GeForce RTX 5080 Driver 616.64.
14:30:05:642 [40312] | INFO  | Recreated runtime environment on runtime [address] ('[local-path]
14:30:05:643 [40312] | INFO  | Redirecting ID3D12Device::CreateCommandQueue(this = [address], pDesc = [address], riid = {0EC870A6-5D7E-4C22-8CFC-[address]}, ppCommandQueue = [address]) ...
14:30:05:643 [40312] | INFO  | > Dumping command queue description:
14:30:05:643 [40312] | INFO  |   +-----------------------------------------+-----------------------------------------+
14:30:05:643 [40312] | INFO  |   | Parameter                               | Value                                   |
14:30:05:643 [40312] | INFO  |   +-----------------------------------------+-----------------------------------------+
14:30:05:643 [40312] | INFO  |   | Type                                    | 2                                       |
14:30:05:643 [40312] | INFO  |   | Priority                                | 100                                     |
14:30:05:644 [40312] | INFO  |   | Flags                                   | 0                                       |
14:30:05:644 [40312] | INFO  |   | NodeMask                                | 0                                       |
14:30:05:644 [40312] | INFO  |   +-----------------------------------------+-----------------------------------------+
14:30:05:684 [40312] | INFO  | Redirecting IDXGISwapChain::ResizeBuffers(this = [address], BufferCount = 6, Width = 3440, Height = 1440, NewFormat = 24, SwapChainFlags = 0x842) ...
14:30:05:685 [40312] | INFO  | Destroyed runtime environment on runtime [address] ('[local-path]
14:30:05:692 [40312] | INFO  | Recreated runtime environment on runtime [address] ('[local-path]
14:30:05:731 [25192] | INFO  | Successfully compiled '[local-path]
14:30:05:733 [10556] | INFO  | Successfully compiled '[local-path]
14:30:05:734 [ 1968] | INFO  | Successfully compiled '[local-path]
14:30:05:749 [40372] | INFO  | Successfully compiled '[local-path]
14:30:05:760 [22292] | INFO  | Successfully compiled '[local-path]
14:30:06:676 [40312] | INFO  | Redirecting D3D12CreateDevice(pAdapter = [address], MinimumFeatureLevel = 1000, riid = {77ACCE80-638E-4E65-8895-[address]}, ppDevice = [address]) ...
14:30:06:677 [40312] | INFO  | Redirecting D3D12CreateDevice(pAdapter = [address], MinimumFeatureLevel = 1000, riid = {77ACCE80-638E-4E65-8895-[address]}, ppDevice = [address]) ...
14:30:06:677 [40312] | INFO  | Redirecting D3D12CreateDevice(pAdapter = [address], MinimumFeatureLevel = 1000, riid = {77ACCE80-638E-4E65-8895-[address]}, ppDevice = [address]) ...
14:30:06:677 [40312] | INFO  | Redirecting D3D12CreateDevice(pAdapter = [address], MinimumFeatureLevel = 1000, riid = {77ACCE80-638E-4E65-8895-[address]}, ppDevice = [address]) ...
14:30:07:766 [40312] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
14:30:07:829 [40312] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
14:30:07:874 [40312] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
14:30:10:376 [32424] | INFO  | Redirecting D3D11CreateDevice(pAdapter = [address], DriverType = 1, Software = [address], Flags = 0, pFeatureLevels = [address], FeatureLevels = 3, SDKVersion = 7, ppDevice = [address], pFeatureLevel = [address], ppImmediateContext = [address]) ...
14:30:10:376 [32424] | INFO  | > Passing on to D3D11CreateDeviceAndSwapChain:
14:30:10:376 [32424] | INFO  | Redirecting D3D11CreateDeviceAndSwapChain(pAdapter = [address], DriverType = 1, Software = [address], Flags = 0, pFeatureLevels = [address], FeatureLevels = 3, SDKVersion = 7, pSwapChainDesc = [address], ppSwapChain = [address], ppDevice = [address], pFeatureLevel = [address], ppImmediateContext = [address]) ...
14:30:10:376 [32424] | INFO  | Redirecting CreateDXGIFactory2(Flags = 0x80000000, riid = {7B7166EC-21C7-44AE-B21A-[address]}, ppFactory = [address]) ...
14:30:10:434 [32424] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
14:30:10:492 [32424] | INFO  | Using feature level b000.
14:30:10:518 [32424] | INFO  | Redirecting CreateDXGIFactory1(riid = {7B7166EC-21C7-44AE-B21A-[address]}, ppFactory = [address]) ...
14:30:13:792 [40312] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
14:30:13:793 [40312] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
14:30:21:365 [36712] | INFO  | Redirecting RegisterClassW(lpWndClass = [address] { "CPGPMObilePcGameBridgeWnd", style = 0x3 }) ...
14:30:21:365 [36712] | INFO  | > Adding 'CS_OWNDC' window class style flag to "CPGPMObilePcGameBridgeWnd".
14:30:23:064 [40312] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
14:30:23:069 [40312] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
14:30:23:071 [40312] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
14:30:36:882 [40312] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
14:30:37:043 [40312] | INFO  | Redirecting RegisterClassExA(lpWndClassEx = [address] { "CustomHDHandle_88486663104", style = 0 }) ...
14:30:37:081 [40312] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
14:30:50:143 [40312] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
14:30:50:144 [40312] | INFO  | Redirecting CreateDXGIFactory1(riid = {770AAE78-F26F-4DBA-A829-[address]}, ppFactory = [address]) ...
```
