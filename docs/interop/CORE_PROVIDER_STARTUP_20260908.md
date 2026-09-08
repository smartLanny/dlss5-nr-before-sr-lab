# Core native observer startup, 2026-09-08

The new authorized continuation fixes the startup gap recorded in #164/#175.
The combined renderer now uses the production Query API before Claim/Create;
it no longer reads a Core log to authorize SR creation. This is a controlled
NVIDIA renderer result, not a game integration or general TAA extractor.

## Source composition and ABI

The isolated working branch started at fetched main
`cd10d96b817d83ea4d84da4286c52cbdc0ca377a`, ordinarily merged #175
`b59c818c742d53f0735512874182bee6b003c706` and #174
`ce63796aefe7a7ca330a7733b1bed50d380bfef0`. All current heads of
#149/#150/#151/#155/#156/#157/#160/#166/#171 are verified ancestors.
#154 is the closed duplicate, not a second blend implementation.
Only CURRENT_STATUS conflicted; both provider and final checkpoint text remain.

Production change: `7659d4c`. Build source:
`492f802f554dcdbb2bb0a5b5c0c2e8860a0141a3`. Later failure-fixture source:
`97135614b5393b917cccf8f7c9a4f9f56976f265`. The latter changes tests only.

- QueryV1 flag `2` explicitly asks for native observer startup readiness.
  Flags `0`/`1`, ABI version, structure sizes, and existing enum values retain
  their NR-only meaning. Old consumers reject the new bit; the client does not
  retry with an unsafe old flag.
- HookWorker publishes installation failure, missing-module deadline failure,
  and self-pin failure. Process detach publishes stopping. The existing
  process-pinned lifetime and unknown-handle/late-module protections remain.
- Preflight samples hook publication **before** forcing the existing module
  reconciliation; publication racing an early poll cannot manufacture Ready.
  An epoch may legitimately be zero after successful initial installation.
  Ready depends on actual hook publication and coverage, not a nonzero epoch.
- Native preflight is independent of NR master enablement and ownership; it
  does not overwrite NR feedback counters. The caller retains the DLL, performs
  official NGX Init/capability loading first, waits outside loader callbacks,
  then Claims the one external NR owner and creates SR. Repeat the preflight
  before every new injected SR lifecycle.
- `WaitForNativeObserver` polls actual status to a caller-specified deadline;
  the polling interval is not permission to create. Failed, stopping, uncertain,
  invalid ABI/status and expired Starting all refuse startup. An asynchronous
  game integration can instead query once per scheduling opportunity.
- Preflight is not a cross-thread reservation against arbitrary concurrent
  module loading or shutdown. Existing epochs, ownership and unknown-handle
  isolation continue protecting later lifecycle changes; callers must serialize
  their own Create/Release/shutdown as required by NGX.

## Actual build and evidence lock

The existing pinned full-source generator was run on the committed integration,
then the existing external-provider transformer produced the consumer TU. Both
languages and nrchain were rebuilt with the cached LLVM/MinHook/ReShade toolchain.
No previous Core or nrchain DLL was relabelled. The controlled host was rebuilt
with MSVC 14.44, official fixed NGX and FFX headers/library.

| Object | SHA256 |
| --- | --- |
| Generated source manifest | `2152b8e1d792f1352685b5548e6464af4c659ecee412f1d786f7992ba40eb156` |
| Transformed provider TU | `db8b70b7a4c61f094fc1bd4807800b3866c6cfd025b2fbef4acb340edc2372b3` |
| Chinese experimental Core | `27664e946e3d8d18c1c60819000903f7d9b6ca779b3423ac55eb4f964b8270f8` |
| English experimental Core | `42f5d7e6eed87c45cc7a5993c16857d245cc66a3d850fc81dad89809865a6cac` |
| Fresh nrchain | `46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb` |
| Combined renderer | `cde1452f00e89895b570244c3f35b606acb0955bad077965d5b6a1e40dc0cf26` |

Private archive `core-provider-ready-20260908` retains `validation.json`, the
verified generated manifest, per-file hashes, exact build scripts/commands,
compiler logs, `off`/`on` results and real missing/failed startup logs. The
manifest's generated files were independently rehashed after compilation.
Vendor runtimes are the fixed prior fixture dependencies; their hashes remain
in the new private index. Source scripts, tests and this summary alone are shared.

Reproduction uses `prepare-multi-api-beta.py --core <checkout> --upstream
<pinned-bridge> --output <new-stage>`, then
`prepare-external-provider-recovery.py --source <new-stage>/core/src/nr_before_sr.cpp
--output <provider.cpp>`. The recorded `build-core.ps1` builds that transformed
TU against the fresh stage and embeds the manifest's source commit;
`build-host.cmd` compiles the current `combined_nr_sr_gpu.cpp`. Run the host with
`<absolute-core.dll> <fresh-runtime-directory> off|on`. Use fresh separate
directories and the recorded matching configuration; do not overwrite evidence.

The first generation attempt refused the changed Core pin, as intended. After
reviewing the small Core diff the integration pin was updated in `492f802`;
generation and build passed. Compiler warnings are preserved, not claimed zero.

## Validation layers

1. Local Windows: actual RR admission/dispatcher and nested ownership tests
   passed, including cached RR handle recovery. Existing provider transformation
   passed. Two Feeder-dependent tests were skipped because their dependency
   environment was not set; these were unchanged paths, not new passes.
2. Client: production client with deterministic query peers passed delayed
   Starting/Busy/Ready, legacy bit rejection, timeout, terminal failures,
   odd epoch and malformed replies (MSVC `/W4 /WX /utf-8`).
3. Production startup function: early epoch-zero calls never poll or authorize
   Create; published coverage, unknown-handle uncertainty, late coverage failure,
   odd epoch and stopping tested through the actual included implementation.
4. Actual worker body: deterministic module delay, install failure, self-pin
   failure, exhausted module wait, stop before/during wait all passed. This
   controls installation outcomes; it is not a real MinHook Enable failure run.
5. Real rebuilt Core DLL: no SR module causes bounded Starting refusal; an
   intentionally incomplete SR DLL causes real InstallHooks failure and
   ObserverFailed. Both retain the old NR-only Query. Neither creates SR or
   performs GPU work. Specific MH_EnableHook failure injection remains untested.
6. Real RTX 5090 Laptop: API-preflight NR-off completed 8 SR frames; first three
   O hashes remain `5324495337928740927`, `10969640350692307865`,
   `16404233325737454114`, identical to the earlier SR-only baseline. NR-on
   completed 8 SR frames, 5 consuming actual project NR after affinity training
   and warmup. Every O frame completed a real fence and read back all 3,686,400
   finite half channels with spatial RGB variation. Each process registered one
   Feature1 contract; NR-on created one external Feature18 instance. Release,
   parameter destruction and official shutdown succeeded. Actual hook trace
   precedes preflight and SR Create, with observerGap=0/observerFault=none.

No full 40-gate rerun, hosted CI, new WARP suite, game or live F6 test is claimed.
Hosted CI remains billing-blocked/not started and was not retried. This sample
uses artificial guides and ReShade registration substitutes; full Feeder game
callbacks, game resize/recovery, temporal image quality, FG and TAA extraction
remain separate work. #164/#14 stay open for these remaining scopes.

## RR evidence correction and next action

Sol verified RR fix `cd85dc6` in the #166/#171/#175 ancestry. The cached-handle
admission and dispatcher code remain inherited, not rewritten. Maintainer's
latest report that 0.4.6 fix RR switching works is the current game feedback;
do not ask for another 2077 RR off/on/off run. Earlier two 2077 F8 files bind
the pre-fix `a2a3380` DLL and are root-cause evidence. They are not relabelled
as post-fix F8. The later NTE F8 binds 0.4.6 but is not a 2077 RR retest.
#162 remains the reported repeated-toggle face-mask visual bug.

Next: fix one actual no-native-DLSS game/API/SDK and connect its dispatch to the
existing mapper, first SR-only then project NR→SR with native fallback and
lifecycle evidence. A game choice was requested from the maintainer while
startup validation continued. Existing native DLSS games retain their route.
No stable release, tag movement, main merge or Manager payload change occurred.
