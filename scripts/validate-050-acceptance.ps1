param(
    [Parameter(Mandatory=$true)][string]$UpstreamRoot,
    [Parameter(Mandatory=$true)][string]$ToolchainRoot,
    [Parameter(Mandatory=$true)][string]$ReShadeSourceRoot,
    [Parameter(Mandatory=$true)][string]$BaselineSource,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
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
$Commit = (& git -C $Root rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve source commit.' }
$Toolchain = (Resolve-Path -LiteralPath $ToolchainRoot).Path
$ReShade = (Resolve-Path -LiteralPath $ReShadeSourceRoot).Path
$Baseline = (Resolve-Path -LiteralPath $BaselineSource).Path
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
$OldImGuiSdk = $env:NR_RESHADE_ROOT
$OldImGuiLlvm = $env:NR_LLVM_BIN
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
    $env:NR_VALIDATION_WARP_ONLY = '1'
    & python (Join-Path $PSScriptRoot 'prepare-multi-api-beta.py') --core $Root --upstream $UpstreamRoot --output $Stage
    if ($LASTEXITCODE) { throw 'Pinned source staging failed.' }
    $ManifestPath = Join-Path $Stage 'source-manifest.json'
    $Manifest = Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($Manifest.source_commit -ne $Commit -or $Manifest.integration.version -ne '0.4.7beta-corefix.2') { throw 'Source identity mismatch.' }
    $Checks.Add('canonical pinned full-source staging')
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
    & python (Join-Path $PSScriptRoot 'validate-ngx-lifecycle.py') --compiler $Compiler --source (Join-Path $Stage 'core/src/nr_before_sr.cpp') --output-dir (Join-Path $Output 'ngx-lifecycle')
    if ($LASTEXITCODE) { throw 'Layered native NGX lifecycle gate failed.' }
    $Checks.Add('production layered NGX lifecycle and ownership with actual Win32 locks')
    & python (Join-Path $PSScriptRoot 'validate-post-boundary.py') --compiler $Compiler --reshade $ReShade --source (Join-Path $Stage 'core/src/nr_before_sr.cpp') --build-dir (Join-Path $Output 'post-boundary-native')
    if ($LASTEXITCODE) { throw 'Scoped native post-boundary gate failed.' }
    $Checks.Add('production native boundary detours and immutable scoped window CPU')
    $ActivityProbe = Join-Path $Output 'ui-activity.exe'
    & $Compiler -std=c++17 -O2 -static -Wall -Wextra -Werror -I (Join-Path $Stage 'core/src') (Join-Path $Root 'tests/ui_activity_test.cpp') -o $ActivityProbe
    if ($LASTEXITCODE) { throw 'UI activity probe compile failed.' }
    & $ActivityProbe
    if ($LASTEXITCODE) { throw 'UI activity probe failed.' }
    $Checks.Add('bounded UI activity alternating/stall/scope/reopen CPU gate')
    $ProbePolicy = Join-Path $Output 'probe-policy.exe'
    & $Compiler -std=c++17 -O2 -static -Wall -Wextra -Werror -I (Join-Path $Stage 'core/src') (Join-Path $Root 'tests/probe_policy_test.cpp') -o $ProbePolicy
    if ($LASTEXITCODE) { throw 'Probe policy compile failed.' }
    & $ProbePolicy
    if ($LASTEXITCODE) { throw 'Probe phase isolation failed.' }
    $Checks.Add('process-local automatic GPU sampling policy and concurrent phase snapshots CPU')
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
    foreach ($Control in @('omit-end-timing','old-probe-current','old-tail-reserve')) {
        & python (Join-Path $PSScriptRoot 'validate-feedback-timing-full.py') --compiler $Compiler --reshade $ReShade --source-root (Join-Path $Stage 'core') --output-dir (Join-Path $Output "full-feedback-$Control") --negative-control $Control
        if ($LASTEXITCODE) { throw "Full Core F8 negative control failed: $Control" }
    }
    $Checks.Add('full assembled Core timing probe phases reserved tails and negative controls CPU')
    foreach ($Test in @('validate-color-policy.py', 'validate-bug-sweep.py', 'validate-strength-migration.py', 'validate-f8-controller.py', 'validate-command-creation.py', 'validate-first-recording.py', 'validate-reset-notification-gap.py', 'validate-core-feedback.py')) {
        & python (Join-Path $PSScriptRoot $Test) --compiler $Compiler
        if ($LASTEXITCODE) { throw "Owner feedback regression failed: $Test" }
        $Checks.Add($Test)
    }
    & python (Join-Path $PSScriptRoot 'validate-first-recording.py') --compiler $Compiler --negative-control
    if ($LASTEXITCODE) { throw 'First-recording old-policy negative control failed.' }
    $Checks.Add('first-recording old-policy CPU negative control')
    foreach ($Test in @('tests/test_runtime_timing_summary.py', 'tests/test_ui_refresh.py', 'tests/test_imgui_abi.py', 'tests/multi_api/test_rr_post_diagnostics.py', 'tests/test_rr_depth_projection.py', 'tests/test_rr_depth_device_identity.py', 'tests/test_compat_route.py', 'tests/test_lighting_edge_weights.py', 'tests/test_picture_reset.py')) {
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
    & (Join-Path $PSScriptRoot 'validate-codec-shader.ps1') -Source (Join-Path $Stage 'core/src/nr_before_sr.cpp')
    $Checks.Add('all staged production SM5 entries compiled')
    & (Join-Path $Stage 'core/scripts/build.ps1') -ToolchainRoot $Toolchain -ReShadeSourceRoot $ReShade
    $Checks.Add('full staged bilingual Core and nrchain compile/link')
    # Dedicated API-only WARP-object fixture: no ExecuteCommandLists/Present or
    # GPU submission. This does not set warp_verified; RunWarp owns those gates.
    & (Join-Path $PSScriptRoot 'validate-command-state-native.ps1') -ToolchainRoot $Toolchain -FirstRecordingOnly
    $Checks.Add('first-recording real Create Capture Restore native API')
    & (Join-Path $PSScriptRoot 'validate-command-state-native.ps1') -ToolchainRoot $Toolchain -FirstRecordingNegativeControl
    $Checks.Add('first-recording old-policy native API negative control')
    if ($RunWarp) {
        & python (Join-Path $PSScriptRoot 'validate-native-color-view.py') --compiler $Compiler --reshade $ReShade --source (Join-Path $Stage 'core/src/nr_before_sr.cpp') --output (Join-Path $Output 'native-color-view-warp')
        if ($LASTEXITCODE) { throw 'Actual typed native color view WARP gate failed.' }
        $Checks.Add('production typed native color admission descriptors and storage WARP')
        & python (Join-Path $PSScriptRoot 'validate-store-quantization.py') --compiler $Compiler --reshade $ReShade --source (Join-Path $Stage 'core/src/nr_before_sr.cpp') --output (Join-Path $Output 'stored-quantization-warp')
        if ($LASTEXITCODE) { throw 'Actual storage quantization WARP gate failed.' }
        $Checks.Add('production PRE POST actual storage quantization and wrong-target WARP')
        & python (Join-Path $PSScriptRoot 'validate-resource-recovery-runtime.py') --compiler $Compiler --reshade $ReShade --source (Join-Path $Stage 'core/src/nr_before_sr.cpp') --output (Join-Path $Output 'resource-recovery-runtime-warp') --run-warp
        if ($LASTEXITCODE) { throw 'Same-process production resource recovery WARP gate failed.' }
        $Checks.Add('production same-process semantic recovery with real descriptor submit and readback WARP')
        & python (Join-Path $PSScriptRoot 'validate-queue-retirement.py') --compiler $Compiler --reshade $ReShade --source (Join-Path $Stage 'core/src/nr_before_sr.cpp') --build-dir (Join-Path $Output 'queue-retirement')
        if ($LASTEXITCODE) { throw 'Production one-shot queue retirement WARP gate failed.' }
        $Checks.Add('production one-shot queue retirement WARP')
        $Checks.Add('production bounded submitted-retirement and output binding WARP')
        $Checks.Add('production post-store readback with unused-target negative control WARP')
        $Checks.Add('production ordinary paused sampling equivalence and late-readback phase WARP')
        $Checks.Add('production read-only post-boundary callbacks and identity isolation CPU')
        & (Join-Path $PSScriptRoot 'validate-command-state-native.ps1') -ToolchainRoot $Toolchain
        $Checks.Add('native command creation and lifecycle; full state and negative restore WARP')
        & (Join-Path $PSScriptRoot 'validate-rr-depth-gpu.ps1') -ToolchainRoot $Toolchain -OutputRoot (Join-Path $Output 'rr-depth-warp') -RunWarp
        $Checks.Add('production RR depth cs_5_0 and private R32 WARP readback')
        # The caller owns the serial GPU/WARP slot. Baseline must be the actual
        # frozen reference source, never a synthetic replacement of the model.
        & (Join-Path $PSScriptRoot 'validate-codec-gpu.ps1') -ToolchainRoot $Toolchain -SourceRoot (Join-Path $Stage 'core') -BaselineSource $Baseline -OutputRoot (Join-Path $Output 'codec-default-warp')
        $Checks.Add('production codec FLOAT exposure finite extreme and composition regression WARP')
        & (Join-Path $PSScriptRoot 'validate-codec-gpu.ps1') -ToolchainRoot $Toolchain -SourceRoot (Join-Path $Stage 'core') -ExposureTyped -OutputRoot (Join-Path $Output 'typed-exposure-warp')
        $Checks.Add('production typed R8 exposure versus R32 normalized control WARP')
        & (Join-Path $PSScriptRoot 'validate-codec-gpu.ps1') -ToolchainRoot $Toolchain -SourceRoot (Join-Path $Stage 'core') -BaselineSource $Baseline -CombinedLowGrid -OutputRoot (Join-Path $Output 'combined-low-grid-warp')
        $Checks.Add('combined low-grid WARP fixture; constructed neural planes, no NVIDIA NR')
    }
    foreach ($Entry in $Manifest.files.PSObject.Properties) {
        $Hash = (Get-FileHash -LiteralPath (Join-Path $Stage $Entry.Name) -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($Hash -ne $Entry.Value) { throw "Staged source changed: $($Entry.Name)" }
    }
    $CoreBuild = Join-Path $Stage 'core/build'
    $Addons = @(Get-ChildItem -LiteralPath $CoreBuild -Filter '*0.4.7beta-corefix.2*.addon64')
    if ($Addons.Count -ne 2) { throw 'Expected exactly two bilingual 0.4.7beta-corefix.2 Core artifacts.' }
    foreach ($File in @($Addons) + @(Get-Item -LiteralPath (Join-Path $CoreBuild 'nrchain_nvngx.dll'))) {
        if ($File.Extension -eq '.addon64') {
            $Version = $File.VersionInfo
            if ($Version.FileMajorPart -ne 0 -or $Version.FileMinorPart -ne 4 -or $Version.FileBuildPart -ne 7 -or $Version.FilePrivatePart -ne 7) { throw 'Wrong numeric PE version.' }
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
    $env:NR_RESHADE_ROOT = $OldImGuiSdk
    $env:NR_LLVM_BIN = $OldImGuiLlvm
    $env:NR_VALIDATION_WARP_ONLY = $OldWarpOnly
    Stop-Transcript | Out-Null
    $Result = [ordered]@{
        schema='nr050-focused-acceptance-v1'; source_commit=$Commit;
        success=($null -eq $Failure); failure=$Failure; checks=@($Checks.ToArray());
        source_manifest_sha256=$(if (Test-Path -LiteralPath (Join-Path $Stage 'source-manifest.json')) { (Get-FileHash -LiteralPath (Join-Path $Stage 'source-manifest.json')).Hash.ToLowerInvariant() } else { $null });
        baseline_source_sha256=(Get-FileHash -LiteralPath $Baseline).Hash.ToLowerInvariant();
        compiler_sha256=(Get-FileHash -LiteralPath $Compiler).Hash.ToLowerInvariant();
        reshade_commit=$ReShadeCommit; artifacts=$Artifacts;
        warp_verified=($RunWarp.IsPresent -and ($null -eq $Failure));
        nvidia_feature18_verified=$false; game_runtime_verified=$false;
        five_percent_performance_gate='unmeasured'; hosted_ci_verified=$false;
        carrier_compiled=$false;
        log_sha256=(Get-FileHash -LiteralPath (Join-Path $Output 'validation.log')).Hash.ToLowerInvariant()
    }
    $Result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Output 'validation.json') -Encoding UTF8
}
if ($null -ne $Failure) { throw $Failure }
Write-Host "PASS: $Commit; exact-source bilingual build; WARP=$($RunWarp.IsPresent); game/performance unverified."
