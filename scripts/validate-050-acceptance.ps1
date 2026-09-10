param(
    [Parameter(Mandatory=$true)][string]$UpstreamRoot,
    [Parameter(Mandatory=$true)][string]$ToolchainRoot,
    [Parameter(Mandatory=$true)][string]$ReShadeSourceRoot,
    [Parameter(Mandatory=$true)][string]$BaselineSource,
    [Parameter(Mandatory=$true)][string]$DirectionalBaselineSource,
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
New-Item -ItemType Directory -Path $Output | Out-Null
Start-Transcript -LiteralPath (Join-Path $Output 'validation.log') | Out-Null
Push-Location $Root
try {
    $env:DLSS5_LLVM_MINGW_ROOT = $Toolchain
    $env:CXX = $Compiler
    $env:POST_COMPAT_CORE_SOURCE = Join-Path $Root 'src/nr_before_sr.cpp'
    $env:NR_RESHADE_ROOT = $ReShade
    $env:NR_LLVM_BIN = Join-Path $Toolchain 'bin'
    if ((Get-FileHash -LiteralPath $DirectionalBaseline -Algorithm SHA256).Hash.ToLowerInvariant() -ne 'c681b208ef320b1b9fbd497e3c46485bf3836296a084cd4a741a2b179f1c8208' -or
        (Get-FileHash -LiteralPath $DirectionalManifest -Algorithm SHA256).Hash.ToLowerInvariant() -ne '212d863167d4b55b5cc613528a2df68600243c73d07195c9136a6fe8b43c18c6') { throw 'The generated D4 reference does not match the frozen acceptance source.' }
    $Checks.Add('frozen generated D4 directional reference source')
    & python (Join-Path $PSScriptRoot 'prepare-multi-api-beta.py') --core $Root --upstream $UpstreamRoot --output $Stage
    if ($LASTEXITCODE) { throw 'Pinned source staging failed.' }
    $ManifestPath = Join-Path $Stage 'source-manifest.json'
    $Manifest = Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($Manifest.source_commit -ne $Commit -or $Manifest.integration.version -ne 'beta0.5-dline11') { throw 'Source identity mismatch.' }
    $Checks.Add('canonical pinned full-source staging')
    foreach ($Test in @('validate-color-policy.py', 'validate-bug-sweep.py', 'validate-strength-migration.py', 'validate-f8-controller.py', 'validate-core-feedback.py', 'validate-command-creation.py', 'validate-first-recording.py', 'validate-reset-notification-gap.py')) {
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
    foreach ($Test in @('tests/test_settings_notices_ui.py', 'tests/test_face_selection_ui.py', 'tests/test_ui_refresh.py', 'tests/test_imgui_abi.py', 'tests/test_picture_reset.py', 'tests/test_light_style.py', 'tests/multi_api/test_rr_post_diagnostics.py', 'tests/test_rr_depth_projection.py', 'tests/test_rr_depth_device_identity.py', 'tests/test_compat_route.py', 'tests/test_lighting_edge_weights.py')) {
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
    & (Join-Path $PSScriptRoot 'validate-codec-shader.ps1') -Source (Join-Path $Stage 'core/src/nr_before_sr.cpp')
    $Checks.Add('all staged production SM5 entries compiled')
    & (Join-Path $Stage 'core/scripts/build.ps1') -ToolchainRoot $Toolchain -ReShadeSourceRoot $ReShade
    $Checks.Add('full staged bilingual Core and nrchain compile/link')
    & (Join-Path $PSScriptRoot 'validate-codec-gpu.ps1') -ToolchainRoot $Toolchain -SourceRoot (Join-Path $Stage 'core') -BaselineSource $Baseline -DirectionalBaselineSource $DirectionalBaseline -PostLightStyle -CompileOnly
    $Checks.Add('generated legacy-post light fixture compiled without GPU dispatch')
    & (Join-Path $PSScriptRoot 'validate-command-state-native.ps1') -ToolchainRoot $Toolchain -FirstRecordingOnly
    $Checks.Add('first-recording real Create Capture Restore native API')
    & (Join-Path $PSScriptRoot 'validate-command-state-native.ps1') -ToolchainRoot $Toolchain -FirstRecordingNegativeControl
    $Checks.Add('first-recording old-policy native API negative control')
    if ($RunWarp) {
        & (Join-Path $PSScriptRoot 'validate-replay.ps1') -ToolchainRoot $Toolchain -OutputRoot (Join-Path $Output 'replay')
        $Checks.Add('full-input replay actual WARP capture and independent reader; isolated model lab compile')
        & python (Join-Path $PSScriptRoot 'validate-f8-controller.py') --compiler $Compiler --gpu
        if ($LASTEXITCODE) { throw 'F8 capture GPU group index, fence or readback gate failed.' }
        $Checks.Add('face diagnostic group8 production GPU ring and invalid group9 WARP')
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
        & (Join-Path $PSScriptRoot 'validate-codec-gpu.ps1') -ToolchainRoot $Toolchain -SourceRoot (Join-Path $Stage 'core') -BaselineSource $Baseline -CombinedLowGrid
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
    $Addons = @(Get-ChildItem -LiteralPath $CoreBuild -Filter '*beta0.5-dline11*.addon64')
    if ($Addons.Count -ne 2) { throw 'Expected exactly two bilingual dline11 Core artifacts.' }
    foreach ($File in @($Addons) + @(Get-Item -LiteralPath (Join-Path $CoreBuild 'nrchain_nvngx.dll'))) {
        if ($File.Extension -eq '.addon64') {
            $Version = $File.VersionInfo
            if ($Version.FileMajorPart -ne 0 -or $Version.FileMinorPart -ne 5 -or $Version.FileBuildPart -ne 1 -or $Version.FilePrivatePart -ne 11) { throw 'Wrong numeric PE version.' }
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
        log_sha256=(Get-FileHash -LiteralPath (Join-Path $Output 'validation.log')).Hash.ToLowerInvariant()
    }
    $Result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Output 'validation.json') -Encoding UTF8
}
if ($null -ne $Failure) { throw $Failure }
Write-Host "PASS: $Commit; exact-source bilingual build; WARP=$($RunWarp.IsPresent); game/performance unverified."
