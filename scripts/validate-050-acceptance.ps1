param(
    [Parameter(Mandatory=$true)][string]$UpstreamRoot,
    [Parameter(Mandatory=$true)][string]$ToolchainRoot,
    [Parameter(Mandatory=$true)][string]$ReShadeSourceRoot,
    [Parameter(Mandatory=$true)][string]$BaselineSource,
    [Parameter(Mandatory=$true)][string]$DirectionalBaselineSource,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [string]$ReShadeDllPath,
    [string]$CodecCacheRoot,
    [string]$NgxSdkRoot = $env:DLSS5_NGX_SDK_ROOT,
    [string]$VcVarsPath = $env:DLSS5_VCVARS_PATH,
    [switch]$RunWarp
)
# Focused acceptance of the configuration/wiring slice, not the full historical
# matrix. Never changes games, the driver, other worktrees or existing evidence.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Root = Split-Path -Parent $PSScriptRoot
$Output = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $Output) { throw 'OutputRoot exists; preserve previous evidence.' }
$Tracked = @(& git -C $Root status --porcelain --untracked-files=no)
if ($LASTEXITCODE -ne 0 -or $Tracked.Count) { throw 'Commit reviewed source before exact-source validation.' }
$UntrackedInputs = @(& git -C $Root ls-files --others --exclude-standard -- src scripts tests config third_party docs/testing)
if ($LASTEXITCODE -ne 0 -or $UntrackedInputs.Count) { throw 'Commit the explicit production, test and packaging input inventory before exact-source validation.' }
$Commit = (& git -C $Root rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve source commit.' }
$Toolchain = (Resolve-Path -LiteralPath $ToolchainRoot).Path
$ReShade = (Resolve-Path -LiteralPath $ReShadeSourceRoot).Path
$Baseline = (Resolve-Path -LiteralPath $BaselineSource).Path
$DirectionalBaseline = (Resolve-Path -LiteralPath $DirectionalBaselineSource).Path
$DirectionalManifest = Join-Path (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $DirectionalBaseline))) 'source-manifest.json'
$Compiler = Join-Path $Toolchain 'bin/x86_64-w64-mingw32-clang++.exe'
if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) { throw 'Compiler missing.' }
$ReShadeCommit = (& git -C $ReShade rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $ReShadeCommit -ne '4a50d1eddace85734871d91792ff214f13f66c01') { throw 'Wrong ReShade dependency.' }
$ReShadeDirty = @(& git -C $ReShade status --porcelain --untracked-files=no)
if ($LASTEXITCODE -ne 0 -or $ReShadeDirty.Count) { throw 'Dirty ReShade dependency.' }
$Stage = Join-Path $Output 'source'
$Checks = New-Object 'System.Collections.Generic.List[string]'
$Artifacts = [ordered]@{}
$Failure = $null
$OldToolchain = $env:DLSS5_LLVM_MINGW_ROOT
$OldCxx = $env:CXX
$OldPostSource = $env:POST_COMPAT_CORE_SOURCE
$OldReShadeRoot = $env:NR_RESHADE_ROOT
$OldLlvmBin = $env:NR_LLVM_BIN
$OldWarpOnly = $env:NR_VALIDATION_WARP_ONLY
New-Item -ItemType Directory -Path $Output | Out-Null
Start-Transcript -LiteralPath (Join-Path $Output 'validation.log') | Out-Null
Push-Location $Root
try {
    $env:DLSS5_LLVM_MINGW_ROOT = $Toolchain
    $env:CXX = $Compiler
    $env:POST_COMPAT_CORE_SOURCE = Join-Path $Root 'src/nr_before_sr.cpp'
    $env:NR_RESHADE_ROOT = $ReShade
    $env:NR_LLVM_BIN = Join-Path $Toolchain 'bin'
    & python (Join-Path $PSScriptRoot 'validate-nr-api-cold-policy.py') --compiler $Compiler
    if ($LASTEXITCODE) { throw 'Production cold NGX bootstrap/retry policy failed.' }
    $Checks.Add('production cold NGX bootstrap warm reuse bounded retry and compiled negative controls CPU')
    if ((Get-FileHash -LiteralPath $DirectionalBaseline -Algorithm SHA256).Hash.ToLowerInvariant() -ne 'c681b208ef320b1b9fbd497e3c46485bf3836296a084cd4a741a2b179f1c8208' -or
        (Get-FileHash -LiteralPath $DirectionalManifest -Algorithm SHA256).Hash.ToLowerInvariant() -ne '212d863167d4b55b5cc613528a2df68600243c73d07195c9136a6fe8b43c18c6') { throw 'The generated D4 reference does not match the frozen acceptance source.' }
    $Checks.Add('frozen generated D4 directional reference source')
    $env:NR_VALIDATION_WARP_ONLY = '1'
    & python (Join-Path $PSScriptRoot 'prepare-multi-api-beta.py') --core $Root --upstream $UpstreamRoot --output $Stage
    if ($LASTEXITCODE) { throw 'Pinned source staging failed.' }
    $ManifestPath = Join-Path $Stage 'source-manifest.json'
    $Manifest = Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($Manifest.source_commit -ne $Commit -or $Manifest.integration.version -ne 'beta0.5-dline15') { throw 'Source identity mismatch.' }
    $Checks.Add('canonical pinned full-source staging')
    & python (Join-Path $PSScriptRoot 'embed-dual-shaders.py') --check
    if($LASTEXITCODE){throw 'Actual dual shader embedding differs.'}
    & python (Join-Path $Root 'tests/test_nr_dual_reference_math.py')
    if($LASTEXITCODE){throw 'Dual matched residual numeric oracle failed.'}
    $DualPolicy=Join-Path $Output 'dual-policy.exe'
    & $Compiler -std=c++20 -O2 -static -Wall -Wextra -Werror -I (Join-Path $Root 'src') (Join-Path $Root 'tests/nr_dual_policy_test.cpp') -o $DualPolicy
    if($LASTEXITCODE){throw 'Dual policy compile failed.'}
    & $DualPolicy
    if($LASTEXITCODE){throw 'Dual policy admission/opaque failure contract failed.'}
    $Checks.Add('dual policy admission/history and independent matched-residual numeric oracle')
    $FullPolicy = Join-Path $Output 'full-nr-policy.exe'
    & $Compiler -std=c++20 -O2 -static -Wall -Wextra -Werror -I (Join-Path $Stage 'core/src') (Join-Path $Root 'tests/nr_full_policy_test.cpp') -o $FullPolicy
    if ($LASTEXITCODE) { throw 'Full NR policy compile failed.' }
    & $FullPolicy
    if ($LASTEXITCODE) { throw 'Full NR two to four pass admission/failure policy failed.' }
    $Checks.Add('full NR two to four pass plan and partial failure policy CPU')
    $FullRuntime = Join-Path $Output 'full-nr-runtime.exe'
    & $Compiler -std=c++20 -O2 -static -Wall -Wextra -Werror -I (Join-Path $Stage 'core/src') (Join-Path $Root 'tests/nr_full_runtime_test.cpp') -o $FullRuntime
    if ($LASTEXITCODE) { throw 'Full NR production wrapper compile failed.' }
    & $FullRuntime
    if ($LASTEXITCODE) { throw 'Full NR production wrapper chaining and ownership failed.' }
    $Checks.Add('full NR production wrapper serial inputs ownership and opaque failure CPU')
    & python (Join-Path $PSScriptRoot 'validate-source-frame.py') --compiler $Compiler --output (Join-Path $Output 'source-frame-cpu')
    if ($LASTEXITCODE) { throw 'Source-frame ownership/queue ordering CPU gate failed.' }
    $Checks.Add('source-frame ownership current-copy and submitted-queue ordering CPU')
    $ProcessingRoute = Join-Path $Output 'processing-route.exe'
    & $Compiler -std=c++20 -O2 -static -Wall -Wextra -Werror -I (Join-Path $Stage 'core/src') (Join-Path $Root 'tests/nr_processing_route_test.cpp') -o $ProcessingRoute
    if ($LASTEXITCODE) { throw 'Processing route session compile failed.' }
    & $ProcessingRoute
    if ($LASTEXITCODE) { throw 'Processing route suffix/reselection/source retry policy failed.' }
    $Checks.Add('processing route sticky suffix reselection and bounded source retry CPU')
    & python (Join-Path $Root 'tests/test_nr_dual_observation.py') --compiler $Compiler
    if($LASTEXITCODE){throw 'NR2 current observation/defaults/two-layer model parameter contract failed.'}
    & python (Join-Path $Root 'tests/test_d13_ui_compile.py')
    if($LASTEXITCODE){throw 'Actual dual quick/detail UI compilation failed.'}
    $Checks.Add('NR2 observation window, preserved scales, per-layer model parameters and bilingual UI contracts')
    & python -m unittest tests.test_motion_slot_selection -v
    if ($LASTEXITCODE) { throw 'Motion binding slot selection CPU regression failed.' }
    $Checks.Add('production motion slot selection and old-first-match negative control CPU')
    foreach ($Pair in @(@('validate-resource-semantics.py','resource-semantics'), @('validate-resource-recovery.py','resource-recovery'), @('validate-native-format-observer.py','native-format-observer'))) {
        & python (Join-Path $PSScriptRoot $Pair[0]) --source-root (Join-Path $Stage 'core') --compiler $Compiler --output (Join-Path $Output $Pair[1])
        if ($LASTEXITCODE) { throw "Resource contract gate failed: $($Pair[0])" }
        $Checks.Add($Pair[0])
    }
    foreach ($Pair in @(@('validate-compatibility-verdict.py','compatibility-verdict'), @('validate-compatibility-verdict-feedback.py','compatibility-writer'), @('validate-feedback-combined.py','combined-feedback'))) {
        & python (Join-Path $PSScriptRoot $Pair[0]) --compiler $Compiler --output-dir (Join-Path $Output $Pair[1])
        if ($LASTEXITCODE) { throw "Compatibility report gate failed: $($Pair[0])" }
        $Checks.Add($Pair[0])
    }
    foreach ($Control in @('old-capacity-negative','omit-end-verdict-negative','ascii-metadata-negative','disable-privacy-negative','unknown-store-negative')) {
        & python (Join-Path $PSScriptRoot 'validate-feedback-combined.py') --compiler $Compiler --output-dir (Join-Path $Output $Control) "--$Control"
        if ($LASTEXITCODE) { throw "Combined report negative oracle failed: $Control" }
    }
    $Checks.Add('combined F8 capacity UTF8 privacy and end-verdict negative controls')
    $Checks.Add('combined F8 current unknown store cannot inherit historical match')
    & python (Join-Path $PSScriptRoot 'validate-resource-recovery-runtime.py') --compiler $Compiler --reshade $ReShade --source (Join-Path $Stage 'core/src/nr_before_sr.cpp') --output (Join-Path $Output 'resource-recovery-snapshot-cpu') --snapshot-cpu
    if ($LASTEXITCODE) { throw 'Recovery snapshot interruption and observation-cache gate failed.' }
    $Checks.Add('production recovery snapshot permits fresh proof after historical evidence loss CPU')
    & python (Join-Path $PSScriptRoot 'validate-compatibility-rr-scope.py') --compiler $Compiler --source-root (Join-Path $Stage 'core/src') --output (Join-Path $Output 'compatibility-rr-scope')
    if ($LASTEXITCODE) { throw 'Unclassified RR notification and Feature1 verdict isolation gate failed.' }
    $Checks.Add('production RR notifications cannot retain old blocks or borrow Feature1 recovery CPU')
    & python (Join-Path $PSScriptRoot 'validate-route-compatibility.py') --compiler $Compiler --source-root (Join-Path $Stage 'core') --output (Join-Path $Output 'route-compatibility-runtime-cpu')
    if ($LASTEXITCODE) { throw 'Scoped compatibility route runtime gate failed.' }
    $Checks.Add('route-compatibility-runtime-cpu')
    & python (Join-Path $PSScriptRoot 'validate-descriptor-provenance.py') --compiler $Compiler --source (Join-Path $Stage 'core/src/nr_before_sr.cpp') --output (Join-Path $Output 'descriptor-provenance-cpu') --cpu-only
    if ($LASTEXITCODE) { throw 'Bounded descriptor provenance CPU gate failed.' }
    $Checks.Add('descriptor-provenance-cpu')
    & python (Join-Path $PSScriptRoot 'validate-descriptor-alias.py') --compiler $Compiler --reshade $ReShade --source (Join-Path $Stage 'core/src/nr_before_sr.cpp') --output (Join-Path $Output 'descriptor-alias-runtime-cpu') --negative-controls
    if ($LASTEXITCODE) { throw 'Proxy/native identity and descriptor-domain gate failed.' }
    $Checks.Add('descriptor-alias-runtime-cpu')
    $Checks.Add('production bulk-copy destination scope and old-global negative control CPU')
    $Checks.Add('production native Clear CPU request and exact fresh-call guard CPU')
    & python (Join-Path $PSScriptRoot 'validate-ngx-lifecycle.py') --compiler $Compiler --source (Join-Path $Stage 'core/src/nr_before_sr.cpp') --output-dir (Join-Path $Output 'ngx-lifecycle')
    if ($LASTEXITCODE) { throw 'Layered native NGX lifecycle gate failed.' }
    $Checks.Add('production layered NGX lifecycle and ownership with actual Win32 locks')
    & python (Join-Path $PSScriptRoot 'validate-post-boundary.py') --compiler $Compiler --reshade $ReShade --source (Join-Path $Stage 'core/src/nr_before_sr.cpp') --build-dir (Join-Path $Output 'post-boundary-native')
    if ($LASTEXITCODE) { throw 'Scoped native post-boundary gate failed.' }
    $Checks.Add('production native boundary detours and immutable scoped window CPU')
    $Checks.Add('production paused current-phase read-only boundary and historical-store isolation CPU')
    $ActivityProbe = Join-Path $Output 'ui-activity.exe'
    & $Compiler -std=c++17 -O2 -static -Wall -Wextra -Werror -I (Join-Path $Stage 'core/src') (Join-Path $Root 'tests/ui_activity_test.cpp') -o $ActivityProbe
    if ($LASTEXITCODE) { throw 'UI activity probe compile failed.' }
    & $ActivityProbe
    if ($LASTEXITCODE) { throw 'UI activity probe failed.' }
    $Checks.Add('bounded UI activity alternating/stall/scope/reopen CPU gate')
    & python (Join-Path $PSScriptRoot 'validate-ui-activity-receipt.py') --compiler $Compiler --source-root (Join-Path $Stage 'core') --output (Join-Path $Output 'ui-activity-receipt-runtime-cpu')
    if ($LASTEXITCODE) { throw 'Actual UI observation and display receipt gate failed.' }
    $Checks.Add('ui-activity-receipt-runtime-cpu')
    $Checks.Add('UI compatibility receipt explicit observation origin and no native Window borrowing CPU')
    $ProbePolicy = Join-Path $Output 'probe-policy.exe'
    & $Compiler -std=c++17 -O2 -static -Wall -Wextra -Werror -I (Join-Path $Stage 'core/src') (Join-Path $Root 'tests/probe_policy_test.cpp') -o $ProbePolicy
    if ($LASTEXITCODE) { throw 'Probe policy compile failed.' }
    & $ProbePolicy
    if ($LASTEXITCODE) { throw 'Probe phase isolation failed.' }
    $Checks.Add('process-local automatic GPU sampling policy and concurrent phase snapshots CPU')
    & python (Join-Path $PSScriptRoot 'validate-store-interval.py') --compiler $Compiler --reshade $ReShade --source (Join-Path $Stage 'core/src/nr_before_sr.cpp') --build-dir (Join-Path $Output 'store-interval-cpu')
    if ($LASTEXITCODE) { throw 'Exact UNORM interval collection and shader gate failed.' }
    foreach ($Control in @('ignore-mismatch','accept-incomplete')) {
        & python (Join-Path $PSScriptRoot 'validate-store-interval.py') --compiler $Compiler --reshade $ReShade --source (Join-Path $Stage 'core/src/nr_before_sr.cpp') --build-dir (Join-Path $Output "store-interval-$Control") --negative-control $Control
        if ($LASTEXITCODE) { throw "UNORM interval negative control failed: $Control" }
    }
    $Checks.Add('production UNORM interval coverage mismatch and late-phase collection CPU')
    & python (Join-Path $PSScriptRoot 'validate-config-policy.py') --compiler $Compiler --source (Join-Path $Stage 'core/src/nr_before_sr.cpp') --output-dir (Join-Path $Output 'jitter-config-policy') --negative-control
    if ($LASTEXITCODE) { throw 'Jitter migration, persistence and fresh-process gate failed.' }
    $Checks.Add('production jitter migration Win32 persistence fresh processes and negative controls CPU')
    & python (Join-Path $PSScriptRoot 'validate-jitter-status.py') --compiler $Compiler
    if ($LASTEXITCODE) { throw 'Jitter PRE omission and POST zero binding gate failed.' }
    $Checks.Add('production PRE disabled jitter and POST resolved zero binding CPU')
    & python (Join-Path $PSScriptRoot 'validate-submitted-fence-wait.py') --compiler $Compiler --output (Join-Path $Output 'submitted-fence-event-cpu')
    if ($LASTEXITCODE) { throw 'Exact fence event wait and scoped CPU timing gate failed.' }
    $Checks.Add('exact submitted fence event wait and scoped CPU timing')
    & python (Join-Path $PSScriptRoot 'validate-rr-timing.py') --compiler $Compiler --output (Join-Path $Output 'rr-timing-cpu')
    if ($LASTEXITCODE) { throw 'Actual RR timing, original-call and no-recovery-borrowing gate failed.' }
    $Checks.Add('actual RR timing carrier original-call preservation and no Feature1 recovery borrowing CPU')
    & python (Join-Path $PSScriptRoot 'validate-feedback-timing-full.py') --compiler $Compiler --reshade $ReShade --source-root (Join-Path $Stage 'core') --output-dir (Join-Path $Output 'full-feedback-timing')
    if ($LASTEXITCODE) { throw 'Full assembled Core timing/probe F8 writer gate failed.' }
    foreach ($Control in @('omit-end-timing','old-probe-current','old-tail-reserve','old-metadata-limit','old-text-limit','redact-timing-numbers','bypass-timing-privacy','untyped-decimal-exemption','omit-end-ui-receipt','redact-ui-decimals','redact-boundary-numbers','bypass-boundary-privacy','borrow-historical-boundary')) {
        & python (Join-Path $PSScriptRoot 'validate-feedback-timing-full.py') --compiler $Compiler --reshade $ReShade --source-root (Join-Path $Stage 'core') --output-dir (Join-Path $Output "full-feedback-$Control") --negative-control $Control
        if ($LASTEXITCODE) { throw "Full Core F8 negative control failed: $Control" }
    }
    $Checks.Add('full assembled Core timing probe phases reserved tails and negative controls CPU')
    $Checks.Add('final F8 typed timing numbers privacy disk output and actual summary CPU')
    foreach ($Test in @('validate-color-policy.py', 'validate-bug-sweep.py', 'validate-strength-migration.py', 'validate-f8-controller.py', 'validate-command-creation.py', 'validate-first-recording.py', 'validate-reset-notification-gap.py', 'validate-core-feedback.py', 'validate-retirement-return.py')) {
        if ($Test -eq 'validate-f8-controller.py') {
            & python (Join-Path $PSScriptRoot $Test) --compiler $Compiler --negative-control
        } else {
            & python (Join-Path $PSScriptRoot $Test) --compiler $Compiler
        }
        if ($LASTEXITCODE) { throw "Owner feedback regression failed: $Test" }
        $Checks.Add($Test)
    }
    & python (Join-Path $PSScriptRoot 'validate-optional-layer-probe.py') --compiler $Compiler --negative-control
    if ($LASTEXITCODE) { throw 'Optional layer observer or poison-pointer/cadence negative controls failed.' }
    $Checks.Add('validate-optional-layer-probe.py')
    & python (Join-Path $PSScriptRoot 'validate-config-policy.py') --compiler $Compiler --negative-control --output-dir (Join-Path $Output 'config-policy')
    if ($LASTEXITCODE) { throw 'One-time user policy migration or persistence failure contract failed.' }
    $Checks.Add('validate-config-policy.py')
    & (Join-Path $PSScriptRoot 'validate-stage-capture-lifecycle.ps1') -ToolchainRoot $Toolchain
    $Checks.Add('legacy stage capture lifecycle and shared diagnostic lease')
    & python (Join-Path $PSScriptRoot 'validate-face-capture-geometry.py') --compiler $Compiler --negative-control
    if ($LASTEXITCODE) { throw 'Face diagnostic display geometry admission failed.' }
    $Checks.Add('validate-face-capture-geometry.py')
    & python (Join-Path $PSScriptRoot 'validate-first-recording.py') --compiler $Compiler --negative-control
    if ($LASTEXITCODE) { throw 'First-recording old-policy negative control failed.' }
    $Checks.Add('first-recording old-policy CPU negative control')
    foreach ($Test in @('tests/test_settings_notices_ui.py', 'tests/test_face_selection_ui.py', 'tests/test_runtime_timing_summary.py', 'tests/test_ui_refresh.py', 'tests/test_imgui_abi.py', 'tests/test_picture_reset.py', 'tests/test_light_style.py', 'tests/multi_api/test_rr_post_diagnostics.py', 'tests/test_rr_depth_projection.py', 'tests/test_rr_depth_device_identity.py', 'tests/test_compat_route.py', 'tests/test_compat_capture_ui.py', 'tests/test_lighting_edge_weights.py')) {
        & python (Join-Path $Root $Test)
        if ($LASTEXITCODE) { throw "Owner feedback regression failed: $Test" }
        $Checks.Add($Test)
    }
    $DepthProbe = Join-Path $Output 'rr-depth-evidence.exe'
    & $Compiler -std=c++17 -O2 -static -Wall -Wextra -Werror (Join-Path $Root 'tests/multi_api/rr_depth_evidence.cpp') -o $DepthProbe
    if ($LASTEXITCODE) { throw 'RR depth evidence probe compile failed.' }
    & $DepthProbe
    if ($LASTEXITCODE) { throw 'RR depth evidence probe failed.' }
    $Checks.Add('bounded RR projection CPU copy')
    foreach ($Pair in @(@('validate-work-config.py','work-config'), @('validate-recommended-picture.py','recommended'))) {
        $Directory = Join-Path $Output $Pair[1]
        New-Item -ItemType Directory -Path $Directory | Out-Null
        & python (Join-Path $PSScriptRoot $Pair[0]) --compiler $Compiler --build-dir $Directory
        if ($LASTEXITCODE) { throw "Host gate failed: $($Pair[0])" }
        $Checks.Add($Pair[0])
    }
    $Negative = Join-Path $Output 'recommended-negative'
    New-Item -ItemType Directory -Path $Negative | Out-Null
    & python (Join-Path $PSScriptRoot 'validate-recommended-picture.py') --compiler $Compiler --build-dir $Negative --negative-control
    if ($LASTEXITCODE) { throw 'Recommended negative oracle failed.' }
    $Checks.Add('recommended independent negative control')
    foreach ($Test in @('test_post_patch.py','test_picture_integration.py','test_integration.py','test_050_core_package.py')) {
        & python (Join-Path $Root "tests/multi_api/$Test")
        if ($LASTEXITCODE) { throw "Source gate failed: $Test" }
        $Checks.Add($Test)
    }
    foreach ($Test in @('validate-edit-field.py','validate-color-edit-field.py')) {
        & python (Join-Path $PSScriptRoot $Test)
        if ($LASTEXITCODE) { throw "EditField host gate failed: $Test" }
        $Checks.Add($Test)
    }
    & python (Join-Path $PSScriptRoot 'validate-four-region.py') --compiler $Compiler --negative-control
    if ($LASTEXITCODE) { throw 'Four-region production scalar contract failed.' }
    $Checks.Add('four-region production scalar and three negative controls')
    & python (Join-Path $PSScriptRoot 'validate-post-light-style.py') --compiler $Compiler --negative-control
    if ($LASTEXITCODE) { throw 'Legacy-post four-region scalar contract failed.' }
    $Checks.Add('legacy-post production scalar and four negative controls')
    & python (Join-Path $PSScriptRoot 'validate-directional-light-style.py') --compiler $Compiler --negative-control
    if ($LASTEXITCODE) { throw 'Directional light production scalar/source contract failed.' }
    $Checks.Add('directional light production scalar and independent negative controls')
    & python (Join-Path $PSScriptRoot 'validate-post-transform.py') --negative-control
    if ($LASTEXITCODE) { throw 'Post transformation provenance negative controls failed.' }
    $Checks.Add('post transformation exact adapter pair and negative controls')
    & (Join-Path $PSScriptRoot 'validate-codec-shader.ps1') -Source (Join-Path $Stage 'core/src/nr_before_sr.cpp') -AdditionalShaderSource @((Join-Path $Stage 'core/src/nr_compat_present_shader.h')) -PresentHudShaderSource (Join-Path $Stage 'core/src/nr_fg_hud_shader.h')
    $Checks.Add('all staged production SM5 entries compiled')
    $Checks.Add('Present conversion and initialization production SM5 entries compiled')
    $Checks.Add('Present fullscreen blit and exact joined FG HUD shader production SM5 entries compiled')
    & (Join-Path $Stage 'core/scripts/build.ps1') -ToolchainRoot $Toolchain -ReShadeSourceRoot $ReShade -CodecCacheRoot $CodecCacheRoot -NgxSdkRoot $NgxSdkRoot -VcVarsPath $VcVarsPath
    $Checks.Add('full staged bilingual Core and nrchain compile/link')
    & (Join-Path $PSScriptRoot 'validate-codec-gpu.ps1') -ToolchainRoot $Toolchain -SourceRoot (Join-Path $Stage 'core') -BaselineSource $Baseline -DirectionalBaselineSource $DirectionalBaseline -PostLightStyle -CompileOnly
    $Checks.Add('generated legacy-post light fixture compiled without GPU dispatch')
    & (Join-Path $PSScriptRoot 'validate-command-state-native.ps1') -ToolchainRoot $Toolchain -FirstRecordingOnly
    $Checks.Add('first-recording real Create Capture Restore native API')
    & (Join-Path $PSScriptRoot 'validate-command-state-native.ps1') -ToolchainRoot $Toolchain -FirstRecordingNegativeControl
    $Checks.Add('first-recording old-policy native API negative control')
    if ($RunWarp) {
if (-not $ReShadeDllPath) { throw 'Actual ReShade runtime is required for the dual device identity regression.' }
        & python (Join-Path $PSScriptRoot 'validate-dual-device.py') --compiler $Compiler --reshade-dll $ReShadeDllPath --output (Join-Path $Output 'dual-device')
        if ($LASTEXITCODE) { throw 'Actual ReShade dual device regression failed.' }
        $Checks.Add('actual ReShade dual device identity; legacy refusal and cross-adapter/budget negative controls; no NGX')
        & python (Join-Path $PSScriptRoot 'validate-dual-inflight.py') --compiler $Compiler --reshade-dll $ReShadeDllPath --output (Join-Path $Output 'dual-inflight')
        if($LASTEXITCODE){throw 'Actual queued descriptor reuse/retirement/negative controls failed.'}
        $Checks.Add('actual queued immutable bindings, bounded capacity, cross-queue/device negatives and recovery; no NVIDIA NGX')

        & (Join-Path $PSScriptRoot 'validate-codec-gpu.ps1') -ToolchainRoot $Toolchain -SourceRoot (Join-Path $Stage 'core') -DualGeneric
        $Checks.Add('dual spatial/fused residual and generic production WARP; synthetic NR; opaque failure retained')
        $FullSpatial = Join-Path $Output 'full-nr-spatial-warp.exe'
        & $Compiler -std=c++20 -O2 -static -Wall -Wextra -I (Join-Path $Stage 'core/src') (Join-Path $Root 'tests/nr_full_spatial_warp.cpp') (Join-Path $Stage 'core/src/nr_dual_spatial_d3d12.cpp') -ld3d12 -ldxgi -ld3dcompiler -ldxguid -o $FullSpatial
        if ($LASTEXITCODE) { throw 'Full NR spatial WARP fixture compile failed.' }
        & $FullSpatial
        if ($LASTEXITCODE) { throw 'Full NR spatial serial chain/fence/readback WARP failed.' }
        $Checks.Add('full NR two to four same-size spatial layers and exact retired readback WARP; synthetic model')
        $PresentUtility = Join-Path $Output 'present-utility-warp.exe'
        & $Compiler -std=c++20 -O2 -static -Wall -Wextra -Werror -I (Join-Path $Stage 'core/src') (Join-Path $Root 'tests/compat_present_utility_gpu.cpp') -ld3d12 -ldxgi -ld3dcompiler -ldxguid -o $PresentUtility
        if ($LASTEXITCODE) { throw 'Production Present utility WARP fixture compile failed.' }
        & $PresentUtility
        if ($LASTEXITCODE) { throw 'Present conversion RT copyback and alpha preservation WARP failed.' }
        $Checks.Add('Present SDR HDR conversion RT copyback and source alpha WARP; synthetic edit')
        $HudUtility = Join-Path $Output 'fg-hud-utility-warp.exe'
        & $Compiler -std=c++20 -O2 -static -Wall -Wextra -Werror -I (Join-Path $Stage 'core/src') (Join-Path $Root 'tests/fg_hud_utility_gpu.cpp') -ld3d12 -ldxgi -ld3dcompiler -ldxguid -o $HudUtility
        if ($LASTEXITCODE) { throw 'Production FG HUD utility WARP fixture compile failed.' }
        & $HudUtility
        if ($LASTEXITCODE) { throw 'FG HUD SDR HDR alpha and protection WARP failed.' }
        $Checks.Add('FG HUD SDR scRGB PQ alpha coverage protected pixels and identity WARP; synthetic neural')
        & python (Join-Path $PSScriptRoot 'validate-source-transport.py') --compiler $Compiler --output (Join-Path $Output 'source-transport-warp')
        if ($LASTEXITCODE) { throw 'Actual source guide transport resource and fence reuse WARP failed.' }
        $Checks.Add('owned source guide transport and resource fence reuse WARP; no NGX or FG inference')
        & python (Join-Path $PSScriptRoot 'validate-fg-capture.py') --compiler $Compiler --output (Join-Path $Output 'fg-capture-warp')
        if ($LASTEXITCODE) { throw 'Actual FG capture readback and exact submission ledger WARP failed.' }
        $Checks.Add('FG capture one to four layer readback and exact submitted ledger WARP; handoff substitute')

        & (Join-Path $PSScriptRoot 'validate-replay.ps1') -ToolchainRoot $Toolchain -OutputRoot (Join-Path $Output 'replay')
        $Checks.Add('full-input replay actual WARP capture and independent reader; isolated model lab compile')
        & python (Join-Path $PSScriptRoot 'validate-f8-controller.py') --compiler $Compiler --gpu
        if ($LASTEXITCODE) { throw 'F8 capture GPU group index, fence or readback gate failed.' }
        $Checks.Add('face diagnostic group8 production GPU ring and invalid group9 WARP')
        & python (Join-Path $PSScriptRoot 'validate-descriptor-provenance.py') --compiler $Compiler --reshade $ReShade --source (Join-Path $Stage 'core/src/nr_before_sr.cpp') --output (Join-Path $Output 'descriptor-provenance-warp')
        if ($LASTEXITCODE) { throw 'Production descriptor provenance and native output-use WARP gate failed.' }
        $Checks.Add('production descriptor provenance native uses RTV targets and Execute positions WARP')
        $Checks.Add('production native Clear CPU format declaration and fresh-call rejection WARP')
        $Checks.Add('production oversized native copies preserve unrelated Output CPU evidence WARP')
        & python (Join-Path $PSScriptRoot 'validate-native-color-view.py') --compiler $Compiler --reshade $ReShade --source (Join-Path $Stage 'core/src/nr_before_sr.cpp') --output (Join-Path $Output 'native-color-view-warp')
        if ($LASTEXITCODE) { throw 'Actual typed native color view WARP gate failed.' }
        $Checks.Add('production typed native color admission descriptors and storage WARP')
        $Checks.Add('production PRE POST rotating motion slots exact fence and descriptor readback WARP')
        & python (Join-Path $PSScriptRoot 'validate-store-quantization.py') --compiler $Compiler --reshade $ReShade --source (Join-Path $Stage 'core/src/nr_before_sr.cpp') --output (Join-Path $Output 'stored-quantization-warp')
        if ($LASTEXITCODE) { throw 'Actual storage quantization WARP gate failed.' }
        $Checks.Add('production PRE POST actual storage quantization and wrong-target WARP')
        $Checks.Add('production UNORM legal interval endpoints complete coverage and slot reuse WARP')
        & python (Join-Path $PSScriptRoot 'validate-resource-recovery-runtime.py') --compiler $Compiler --reshade $ReShade --source (Join-Path $Stage 'core/src/nr_before_sr.cpp') --output (Join-Path $Output 'resource-recovery-runtime-warp') --run-warp
        if ($LASTEXITCODE) { throw 'Same-process production resource recovery WARP gate failed.' }
        $Checks.Add('production same-process semantic recovery with real descriptor submit and readback WARP')
        & python (Join-Path $PSScriptRoot 'validate-queue-retirement.py') --compiler $Compiler --reshade $ReShade --source (Join-Path $Stage 'core/src/nr_before_sr.cpp') --build-dir (Join-Path $Output 'queue-retirement')
        if ($LASTEXITCODE) { throw 'Production one-shot queue retirement WARP gate failed.' }
        $Checks.Add('production one-shot queue retirement WARP')
        $Checks.Add('production bounded submitted-retirement and output binding WARP')
        $Checks.Add('production post-store readback with unused-target negative control WARP')
        $Checks.Add('production ordinary paused sampling equivalence and late-readback phase WARP')
        $Checks.Add('production output-state-only barriers without diagnostic dispatch or readback WARP')
        $Checks.Add('production read-only post-boundary callbacks and identity isolation CPU')
        & (Join-Path $PSScriptRoot 'validate-command-state-native.ps1') -ToolchainRoot $Toolchain
        $Checks.Add('native command creation and lifecycle; full state and negative restore WARP')
        & (Join-Path $PSScriptRoot 'validate-rr-depth-gpu.ps1') -ToolchainRoot $Toolchain -OutputRoot (Join-Path $Output 'rr-depth-warp') -RunWarp
        $Checks.Add('production RR depth cs_5_0 and private R32 WARP readback')
        # The caller owns the serial GPU/WARP slot. Baseline must be the actual
        # frozen reference source, never a synthetic replacement of the model.
        & (Join-Path $PSScriptRoot 'validate-codec-gpu.ps1') -ToolchainRoot $Toolchain -SourceRoot (Join-Path $Stage 'core') -BaselineSource $Baseline -DirectionalBaselineSource $DirectionalBaseline -PostLightStyle
        $Checks.Add('legacy-post light controls production WARP; synthetic Neural only')
        & (Join-Path $PSScriptRoot 'validate-codec-gpu.ps1') -ToolchainRoot $Toolchain -SourceRoot (Join-Path $Stage 'core') -BaselineSource $Baseline -DirectionalBaselineSource $DirectionalBaseline -LightStyle
        $Checks.Add('four-region light controls production WARP; synthetic Neural only')
        & (Join-Path $PSScriptRoot 'validate-codec-gpu.ps1') -ToolchainRoot $Toolchain -SourceRoot (Join-Path $Stage 'core') -BaselineSource $Baseline -OutputRoot (Join-Path $Output 'codec-default-warp')
        $Checks.Add('production codec FLOAT exposure finite extreme and composition regression WARP')
        & (Join-Path $PSScriptRoot 'validate-codec-gpu.ps1') -ToolchainRoot $Toolchain -SourceRoot (Join-Path $Stage 'core') -ExposureTyped -OutputRoot (Join-Path $Output 'typed-exposure-warp')
        $Checks.Add('production typed R8 exposure versus R32 normalized control WARP')
        & (Join-Path $PSScriptRoot 'validate-codec-gpu.ps1') -ToolchainRoot $Toolchain -SourceRoot (Join-Path $Stage 'core') -BaselineSource $Baseline -CombinedLowGrid -OutputRoot (Join-Path $Output 'combined-low-grid-warp')
        $Checks.Add('combined low-grid WARP fixture; constructed neural planes, no NVIDIA NR')
        & (Join-Path $PSScriptRoot 'validate-regions.ps1') -ToolchainRoot $Toolchain -OutputRoot (Join-Path $Output 'regions')
        $Checks.Add('engine region production adapter and snapshot WARP')
        & (Join-Path $PSScriptRoot 'validate-codec-gpu.ps1') -ToolchainRoot $Toolchain -SourceRoot (Join-Path $Stage 'core') -Regions
        $Checks.Add('fine region composition production WARP')
        & (Join-Path $PSScriptRoot 'validate-face-runtime.ps1') -ToolchainRoot $Toolchain -OutputRoot (Join-Path $Output 'face-runtime')
        $Checks.Add('face tracker exact lifetime WARP and optional native CPU worker')
        & (Join-Path $PSScriptRoot 'validate-codec-gpu.ps1') -ToolchainRoot $Toolchain -SourceRoot (Join-Path $Stage 'core') -FaceProtection
        $Checks.Add('independent face C1 and original endpoints production WARP')
        & (Join-Path $PSScriptRoot 'validate-codec-gpu.ps1') -ToolchainRoot $Toolchain -SourceRoot (Join-Path $Stage 'core') -FaceProtection -FaceNegativeControl
        $Checks.Add('independent face production C1 negative control')
    }
    foreach ($Entry in $Manifest.files.PSObject.Properties) {
        $Hash = (Get-FileHash -LiteralPath (Join-Path $Stage $Entry.Name) -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($Hash -ne $Entry.Value) { throw "Staged source changed: $($Entry.Name)" }
    }
    $CoreBuild = Join-Path $Stage 'core/build'
    $ExpectedPeVersion = [Version]'0.5.1.15'
    if ($Manifest.source_commit -ne $Commit -or $Manifest.integration.version -ne 'beta0.5-dline15' -or [Version]$Manifest.integration.core_pe_version -ne $ExpectedPeVersion) { throw 'Source identity or PE version mismatch.' }
    $Addons = @(Get-ChildItem -LiteralPath $CoreBuild -Filter '*beta0.5-dline15*.addon64')
    if ($Addons.Count -ne 2) { throw 'Expected exactly two bilingual dline15 Core artifacts.' }
    foreach ($File in @($Addons) + @(Get-Item -LiteralPath (Join-Path $CoreBuild 'nrchain_nvngx.dll'))) {
        if ($File.Extension -eq '.addon64') {
            $Version = $File.VersionInfo
            $ActualPeVersion = [Version]::new($Version.FileMajorPart,$Version.FileMinorPart,$Version.FileBuildPart,$Version.FilePrivatePart)
            if ($ActualPeVersion -ne $ExpectedPeVersion) { throw 'Wrong numeric PE version.' }
        }
        $Artifacts[$File.Name] = [ordered]@{ bytes=$File.Length; sha256=(Get-FileHash -LiteralPath $File.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    }
    $Checks.Add('manifest source hashes and actual numeric PE identities')
} catch {
    $Failure = $_.Exception.Message
    Write-Host "FAILED: $Failure"
} finally {
    Pop-Location
    $env:DLSS5_LLVM_MINGW_ROOT = $OldToolchain
    $env:CXX = $OldCxx
    $env:POST_COMPAT_CORE_SOURCE = $OldPostSource
    $env:NR_RESHADE_ROOT = $OldReShadeRoot
    $env:NR_LLVM_BIN = $OldLlvmBin
    $env:NR_VALIDATION_WARP_ONLY = $OldWarpOnly
    Stop-Transcript | Out-Null
    $Result = [ordered]@{
        schema='nr050-focused-acceptance-v1'; source_commit=$Commit;
        success=($null -eq $Failure); failure=$Failure; checks=@($Checks.ToArray());
        source_manifest_sha256=$(if (Test-Path -LiteralPath (Join-Path $Stage 'source-manifest.json')) { (Get-FileHash -LiteralPath (Join-Path $Stage 'source-manifest.json')).Hash.ToLowerInvariant() } else { $null });
        baseline_source_sha256=(Get-FileHash -LiteralPath $Baseline).Hash.ToLowerInvariant();
        directional_baseline_source_sha256=(Get-FileHash -LiteralPath $DirectionalBaseline).Hash.ToLowerInvariant();
        directional_baseline_manifest_sha256=(Get-FileHash -LiteralPath $DirectionalManifest).Hash.ToLowerInvariant();
        compiler_sha256=(Get-FileHash -LiteralPath $Compiler).Hash.ToLowerInvariant();
        reshade_commit=$ReShadeCommit; artifacts=$Artifacts;
        warp_verified=($RunWarp.IsPresent -and ($null -eq $Failure));
        post_light_style_warp_verified=($RunWarp.IsPresent -and ($null -eq $Failure));
        directional_light_style_warp_verified=($RunWarp.IsPresent -and ($null -eq $Failure));
        nvidia_feature18_verified=$false; game_runtime_verified=$false;
        five_percent_performance_gate='unmeasured'; hosted_ci_verified=$false;
        carrier_compiled=$false;
        face_backend_enabled=([bool]$env:DLSS5_FACE_RUNTIME_ROOT);
        face_backend_receipt_sha256=$(if(Test-Path -LiteralPath (Join-Path $Stage 'core/build/face-backend.json')){(Get-FileHash -LiteralPath (Join-Path $Stage 'core/build/face-backend.json')).Hash.ToLowerInvariant()}else{$null});
        nrchain_build_receipt_sha256=$(if(Test-Path -LiteralPath (Join-Path $Stage 'core/build/nrchain-build.json')){(Get-FileHash -LiteralPath (Join-Path $Stage 'core/build/nrchain-build.json')).Hash.ToLowerInvariant()}else{$null});
        log_sha256=(Get-FileHash -LiteralPath (Join-Path $Output 'validation.log')).Hash.ToLowerInvariant()
    }
    $Result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Output 'validation.json') -Encoding UTF8
}
if ($null -ne $Failure) { throw $Failure }
Write-Host "PASS: $Commit; exact-source bilingual build; WARP=$($RunWarp.IsPresent); game/performance unverified."
