known-null snapshot: token=120 baseline=Reset resetSerial=87 route=pristine-null dynamic=0x0; native ClearState(NULL) restores the dummy PSO.
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
10:18:02.531      DLSS.Input.Color.Subrect