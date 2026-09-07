# 内测收集表 30n66b — 兦 巫师3 闪烁（beta0.4.5-multiapi-dev3）

- Source: https://docs.qq.com/sheet/DY0dwbUVYZEZIU01P?tab=30n66b row3
- Submitter auto: 匿名用户 / ID: 兦
- Submit time (sheet): 2026-09-07 23:12:53 Asia/Shanghai
- Symptom (sheet): 巫师3 闪烁问题
- F8: `260907-witcher3-beta0.4.5-multiapi-dev3-231015919-13952-1.txt` SHA256 `98caebd164231f1284d370bd856d9e0de885a1110dad764ada6b5183760dd1df` (447895 B)
- No matching open issue for this submitter+symptom（≠#40 无日志崩溃；≠#146 0.4.2 弱效果）
- Do NOT nag for nr+ReShade; F8 TXT present.

## Metadata / verdict sketch
- build=**beta0.4.5-multiapi-dev3** / witcher3.exe / RTX 5070 / driver.raw=**61656** / api=49152 / 3413x1920 colorSpace=3
- requested.mode=**2** (NR active in capture); start.bypass=**none/active**; start.scope.nrSuccess high (~51774)
- Capture shows successful NR-before-SR evaluates; sheet complaint is **闪烁**, not NR skip
- Also loads `renodx-thewitcher3.addon64`; sourceCommit=`b03f474…` sourceDirty=1
- Suggest **new GitHub issue** (or ask DLSS5仓库); do not fold into #146 without owner call

## Key greps
```
13:build=beta0.4.5-multiapi-dev3
15:exe.leaf=witcher3.exe
16:gpu=NVIDIA GeForce RTX 5070
17:driver.raw=61656
20:api=49152
21:display.extent=3413x1920
23:display.colorSpace=3
25:requested.config=24
26:requested.enabled=1
27:requested.mode=2
30:requested.workMode=0
43:applied.instance=10
44:applied.frame=51774
45:applied.config=24
57:capture.localTime=2026-09-07T23:10:15.919
87:start.scope.mode=2
88:start.scope.nrSuccess=51774
104:start.commandState.reason=none
105:start.commandState.route=typed-pso
128:start.nrSuccess=51774
130:start.bypass=none/active
199:end.scope.nrSuccess=51825
241:end.bypass=none/active
328:artifact.sourceCommit=b03f474ba5127ab057fbb5e93708c47eb9dbb866
331:artifact.identityStatus=verified
```

## Notable log lines
```
554:22:57:49.994  NR-before-SR V2 evaluate succeeded: count=1 work=1394x781 render=1394x781 mode=Reference scale=1.000 identity=0 strength=1.000
403:22:56:01.658  Config: enabled=1 mode=2 identity=0 preTransfer=1.000 postTransfer=1.000 colorFollowRequested=1.000 preserveLighting=0 proxyScale=0.250 paperWhiteScale=4.000 peakShou
430:22:56:11.078  [post-compat] initial RR-post=1 Npost/O=100%; requested Mode=2 retained.
547:22:57:49.730  NR jitter actual: mode=1 state=forwarded input=valid,valid source=1394x781 work=1394x781 scale=1.000000,1.000000 value=0.277778,-0.125000.
340:module.8=renodx-thewitcher3.addon64
340:module.8=renodx-thewitcher3.addon64
```

## Tail
```
23:00:59:110 [26508] | INFO  | Redirecting CreateDXGIFactory1(riid = {1BC6EA02-EF36-464F-BF0C-[address]}, ppFactory = [address]) ...
23:00:59:111 [26508] | INFO  | Redirecting CreateDXGIFactory1(riid = {1BC6EA02-EF36-464F-BF0C-[address]}, ppFactory = [address]) ...
23:03:11:338 [ 2808] | INFO  | [RenoDX] mods::shader::OnCreatePipelineLayout(will insert cbuffer 13 at root_index 16 with slot count 32 creating new size of 17, root_dwords: 16 => 48, constant_injection_cost: 32, descriptor_injection_cost: 0, newParams: [address] )
23:03:11:339 [ 2808] | INFO  | [RenoDX] mods::shader::OnInitPipelineLayout([address], injection index: 16, descriptor injection index: -1, descriptor injection count: 0, injection layout: [address], cbvIndex:13 )
23:10:15:919 [25824] | INFO  | Redirecting CreateDXGIFactory2(Flags = 0, riid = {1BC6EA02-EF36-464F-BF0C-[address]}, ppFactory = [address]) ...
23:10:15:920 [25824] | INFO  | Redirecting CreateDXGIFactory2(Flags = 0, riid = {1BC6EA02-EF36-464F-BF0C-[address]}, ppFactory = [address]) ...
23:10:15:920 [25824] | INFO  | Redirecting CreateDXGIFactory2(Flags = 0, riid = {1BC6EA02-EF36-464F-BF0C-[address]}, ppFactory = [address]) ...
evidence.carrier=missing; required=0; source-or-read-unavailable

text.optionalMissing=1
text.complete=1
END DLSS5 FEEDBACK TEXT
```

## Suggested GitHub issue (for DLSS5仓库)
Title: `[beta0.4.5-multiapi-dev3][巫师3][RTX 5070] 闪烁（内测收集表 兦；F8 NR active）`
Body bullets: sheet row3 ID 兦; F8 archive `20260907-sheet-30n66b-ji-witcher3-multiapi-dev3/`; mode=2 NR succeeding; renodx-thewitcher3 present; distinct from #146/#40.
