# 内测收集表 30n66b — neroeggs 艾尔登法环+erss（beta0.4.5-dx11-compat）

- Source: https://docs.qq.com/sheet/DY0dwbUVYZEZIU01P?tab=30n66b row2
- Submitter auto: 匿名用户 / ID: neroeggs / QQ map: 290587187
- Submit time (sheet serial→local): 2026-09-07 21:54:05 Asia/Shanghai
- Symptom (sheet): 艾尔登法环+erss 后置NR不生效
- F8: `260907-eldenring-beta0.4.5-dx11-compat-215209913-30612-1.txt` SHA256 `9518c98926efd7fd8f5f0db910f7ee5da3120a7746a188b51a8bd6d9d76957b1` (46538 B)
- Optional image: `sheet-image-7a7842c854c12b25.png` SHA256 `d661447bd64127654a9e099353b0c4182bfcfb487c0b35c843147f0dd1ca45ec` (2741188 B)
- Maps to existing **#64** (same NeroEggs + 艾尔登法环+ERSS). Prior same-day packet was **beta0.4.5-fix1**; this is **dx11-compat** build — treat as new build evidence, not duplicate of fix1 folder.
- Do NOT nag for nr+ReShade; F8 TXT present.

## Metadata / verdict sketch
- build=**beta0.4.5-dx11-compat** / eldenring.exe / RTX 5080 / driver.raw=**61656** / api=49152 / 3840x2160 colorSpace=3
- Capture requested.mode=1 (post), workMode=0; applied.frame=1225 is historical NR success count
- start/end.scope.nrSuccess=0; bypass=command-state-unverified; reason=null-guard-graphics-state-used route=native-bypass
- Same post-NR null-guard pattern as fix1 packet under `20260907-NeroEggs_0.4.5-fix1_elden-erss/`

## Key greps
```
14:build=beta0.4.5-dx11-compat
16:exe.leaf=eldenring.exe
17:gpu=NVIDIA GeForce RTX 5080
18:driver.raw=61656
21:api=49152
22:display.extent=3840x2160
24:display.colorSpace=3
26:requested.config=7
27:requested.enabled=1
28:requested.mode=1
31:requested.workMode=0
44:applied.instance=2
45:applied.frame=1225
46:applied.config=4
58:capture.localTime=2026-09-07T21:52:09.913
89:start.scope.nrSuccess=0
105:start.commandState.reason=null-guard-graphics-state-used
106:start.commandState.route=native-bypass
129:start.nrSuccess=1225
131:start.bypass=command-state-unverified
134:start.rrLiveHandles=0
160:resource.Color=dim:3 size:3840x2160 array:1 mip:1 format:24 samples:1 flags:0
161:resource.Output=dim:3 size:3840x2160 array:1 mip:1 format:24 samples:1 flags:5
162:resource.Depth=dim:3 size:3840x2160 array:1 mip:1 format:19 samples:1 flags:0
163:resource.MotionVectors=dim:3 size:3840x2160 array:1 mip:1 format:34 samples:1 flags:0
200:end.scope.nrSuccess=0
216:end.commandState.reason=null-guard-graphics-state-used
240:end.nrSuccess=1225
242:end.bypass=command-state-unverified
```

## Tail
```
21:51:21:374 [ 6584] | INFO  |   | Priority                                | 0                                       |
21:51:21:374 [ 6584] | INFO  |   | Flags                                   | 0                                       |
21:51:21:374 [ 6584] | INFO  |   | NodeMask                                | 0                                       |
21:51:21:374 [ 6584] | INFO  |   +-----------------------------------------+-----------------------------------------+
21:51:22:793 [28136] | INFO  | Redirecting CreateDXGIFactory2(Flags = 0, riid = {7B7166EC-21C7-44AE-B21A-[address]}, ppFactory = [address]) ...
21:51:28:961 [ 6584] | WARN  | Reference count for ID3D12DescriptorHeap object [address] ([address]) is inconsistent (1).
[repetition.suppressed=1; representative=latest-equivalent-line]
21:51:30:877 [28136] | INFO  | Redirecting CreateDXGIFactory2(Flags = 0, riid = {7B7166EC-21C7-44AE-B21A-[address]}, ppFactory = [address]) ...
21:52:09:920 [25936] | INFO  | Redirecting CreateDXGIFactory2(Flags = 0, riid = {1BC6EA02-EF36-464F-BF0C-[address]}, ppFactory = [address]) ...
[repetition.suppressed=3; representative=latest-equivalent-line]
evidence.carrier=missing; required=0; source-or-read-unavailable
text.optionalMissing=1
text.complete=1
END DLSS5 FEEDBACK TEXT
```

## Suggested GitHub comment (for DLSS5仓库)
Append to #64: neroeggs re-filed via 内测收集结果表 with F8 on **beta0.4.5-dx11-compat** (2026-09-07 21:54 CST); post NR still null-guard / scope nrSuccess=0; archive `{txt_path.parent.name}/`.
