param(
    [Parameter(Mandatory=$true)][string]$UpstreamRoot,
    [Parameter(Mandatory=$true)][string]$ToolchainRoot,
    [Parameter(Mandatory=$true)][string]$ReShadeSourceRoot,
    [Parameter(Mandatory=$true)][string]$DirectXMathRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [string]$NrchainReuseRoot,
    [string]$CodecCacheRoot,
    [string]$FaceRuntimeRoot = $env:DLSS5_FACE_RUNTIME_ROOT,
    [switch]$QualitySuccessor,
    [switch]$AllowMissingDebugLayer
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Root = Split-Path -Parent $PSScriptRoot
if ($QualitySuccessor) {
    # The final generator owns identity. A new successor must not compile under
    # one version while its receipt, file names and test ZIPs retain another.
    $PrivateVersionPath = Join-Path $PSScriptRoot 'patch-private-sr.py'
    if (Test-Path -LiteralPath $PrivateVersionPath -PathType Leaf) {
        $VersionSource = Get-Content -LiteralPath $PrivateVersionPath -Raw
        $VersionMatch = [regex]::Match($VersionSource, "(?m)^VERSION\s*=\s*'((?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)-beta\.[1-9][0-9]*)'\r?$")
        if (-not $VersionMatch.Success) { throw 'Cannot resolve exact final private SR product identity.' }
        $CandidateVersion = $VersionMatch.Groups[1].Value
        $LeafVersion = $CandidateVersion
    } else {
        $VersionSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'patch-colour-edge.py') -Raw
        $VersionMatch = [regex]::Match($VersionSource, "(?m)^VERSION = '(0\.5\.2-beta\.[1-9][0-9]*|beta0\.5-dline21-(unified[0-9]+(?:-[a-z0-9]+)?(?:-ui[0-9]+)?))'\r?$")
        if (-not $VersionMatch.Success) { throw 'Cannot resolve exact final unified product identity.' }
        $CandidateVersion = $VersionMatch.Groups[1].Value
        $LeafVersion = if ($VersionMatch.Groups[2].Success) { $VersionMatch.Groups[2].Value } else { $CandidateVersion }
    }
} else {
    $LeafVersion = 'perf12'
    $CandidateVersion = 'beta0.5-dline21-' + $LeafVersion
}
if ($QualitySuccessor -and [string]::IsNullOrWhiteSpace($FaceRuntimeRoot)) { throw 'Unified candidate requires the complete face backend; supply FaceRuntimeRoot.' }
$Output = [IO.Path]::GetFullPath($OutputRoot)
$Gate = $Output + '.lockperf-checks'
$HotpathGate = $Output + '.hotpath-checks'
$PerfGate = $Output + '.perf3-checks'
$Perf4Gate = $Output + '.perf4-checks'
$Perf5Gate = $Output + '.perf5-checks'
$Perf6Gate = $Output + '.perf6-checks'
$Perf7Gate = $Output + '.perf7-checks'
$Perf8Gate = $Output + '.perf8-checks'
$Perf9Gate = $Output + '.perf9-checks'
$Perf10Gate = $Output + '.perf10-checks'
$AuxGate = $Output + '.aux-history-checks'
$MemoryGate = $Output + '.aux-parent-checks'
$InheritedGate = $Output + '.inherited-checks'
foreach ($Path in @($Output, $Gate, $HotpathGate, $InheritedGate, $PerfGate, $Perf4Gate, $Perf5Gate, $Perf6Gate, $Perf7Gate, $Perf8Gate, $Perf9Gate, $Perf10Gate, $AuxGate, $MemoryGate)) {
    if (Test-Path -LiteralPath $Path) { throw 'Choose a new OutputRoot; existing evidence is never overwritten.' }
}
$Head = (& git -C $Root rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $Head -notmatch '^[0-9a-f]{40}$') { throw 'Cannot identify candidate HEAD.' }
$Dirty = @(& git -C $Root status --porcelain --untracked-files=no)
if ($LASTEXITCODE -ne 0 -or $Dirty.Count) { throw 'Commit the isolated candidate before generation; no automatic commit/reset is performed.' }
# Resolve before Push-Location so callers may supply paths relative to their CWD.
$ToolchainRoot = (Resolve-Path -LiteralPath $ToolchainRoot).Path
$Cxx = Join-Path $ToolchainRoot 'bin/x86_64-w64-mingw32-clang++.exe'
# Same-list exclusion, independent-list concurrency, registry lifetime and the
# actual descriptor readers must still pass with real Windows SRW primitives.
& python (Join-Path $PSScriptRoot 'validate-command-state-lockperf.py') --root $Root --compiler $Cxx --output $Gate
if ($LASTEXITCODE -ne 0) { throw 'Native SRW concurrency gate failed. Do not build/deploy.' }
# Frozen pre-fix source must FAIL both work budgets, while this source passes.
& python (Join-Path $PSScriptRoot 'validate-command-state-hotpath.py') --root $Root --compiler $Cxx --output $HotpathGate
if ($LASTEXITCODE -ne 0) { throw 'Hotpath budgets or state equivalence failed. Do not build/deploy.' }
& python (Join-Path $PSScriptRoot 'validate-perf3-hotpaths.py') --compiler $Cxx --output $PerfGate
if ($LASTEXITCODE -ne 0) { throw 'Perf3 differential/work budgets failed.' }
& python (Join-Path $PSScriptRoot 'validate-source-frame.py') --compiler $Cxx --output ($PerfGate + '/source-frame')
if ($LASTEXITCODE -ne 0) { throw 'Source ownership regression failed.' }
& python (Join-Path $PSScriptRoot 'validate-perf4-hotpaths.py') --compiler $Cxx --output $Perf4Gate
if ($LASTEXITCODE -ne 0) { throw 'Perf4 multi-heap/saturation differential gates failed.' }
& python (Join-Path $PSScriptRoot 'validate-perf5-ranges.py') --compiler $Cxx --output $Perf5Gate
if ($LASTEXITCODE -ne 0) { throw 'Prepared-range differential and fallback coverage failed.' }
& python (Join-Path $PSScriptRoot 'validate-perf6-lookup.py') --compiler $Cxx --output $Perf6Gate
if ($LASTEXITCODE -ne 0) { throw 'Perf6 slot-cache and sorted-range gates failed.' }
& python (Join-Path $PSScriptRoot 'validate-perf7-tracking.py') --compiler $Cxx --output $Perf7Gate
if ($LASTEXITCODE -ne 0) { throw 'Perf7 actual tracking/identity regression failed.' }
& python (Join-Path $PSScriptRoot 'validate-perf8-lifetime.py') --compiler $Cxx --output $Perf8Gate
if ($LASTEXITCODE -ne 0) { throw 'Perf8 lifecycle/normalization regression failed.' }
& python (Join-Path $PSScriptRoot 'validate-perf9-observation.py') --compiler $Cxx --output $Perf9Gate
if ($LASTEXITCODE -ne 0) { throw 'Perf9 lineage/snapshot regression failed.' }
& python (Join-Path $PSScriptRoot 'validate-perf10-recording.py') --compiler $Cxx --output $Perf10Gate
if ($LASTEXITCODE -ne 0) { throw 'Perf10 reset/binding/guide-submission gate failed.' }
& (Join-Path $PSScriptRoot 'validate-perf9-capture.ps1') -OutputRoot ($Perf9Gate + '/capture-preflight')
& python (Join-Path $PSScriptRoot 'validate-aux-history-reuse.py') --compiler $Cxx --output $AuxGate
if ($LASTEXITCODE -ne 0) { throw 'Auxiliary allocation/history separation gate failed.' }
& python (Join-Path $PSScriptRoot 'validate-aux-parent-retirement.py') --compiler $Cxx --output $MemoryGate
if ($LASTEXITCODE -ne 0) { throw 'Auxiliary parent retirement/fence/memory gate failed.' }
if ($QualitySuccessor) {
    $ResetObservationProbe = $Output + '.reset-observation.exe'
    & $Cxx '-std=c++17' '-Wall' '-Wextra' '-Werror' '-static' (Join-Path $Root 'tests/reset_observation_test.cpp') '-o' $ResetObservationProbe
    if ($LASTEXITCODE) { throw 'Reset observation schema test compilation failed.' }
    & $ResetObservationProbe
    if ($LASTEXITCODE) { throw 'Reset observation schema/count/source identity regression failed.' }
    & python (Join-Path $PSScriptRoot 'validate-context-release-admission.py') --compiler $Cxx --output ($Output + '.create-release-checks')
    if ($LASTEXITCODE) { throw 'Model replacement release ordering and route preservation failed.' }
    & python (Join-Path $PSScriptRoot 'validate-submit-batch.py') --compiler $Cxx --output ($Output + '.submit-checks')
    if ($LASTEXITCODE) { throw 'Submission batch relevance and inverse controls failed.' }
    & python (Join-Path $Root 'tests/run_d21_energy_gate.py') --compiler $Cxx --output ($Output + '.energy-checks')
    if ($LASTEXITCODE) { throw 'Energy default, saved configuration or route checks failed.' }
    $env:NR_LLVM_BIN = Join-Path $ToolchainRoot 'bin'
    & python (Join-Path $Root 'tests/test_d13_ui_compile.py') -v
    if ($LASTEXITCODE) { throw 'Layer UI transaction checks failed.' }
    & python (Join-Path $Root 'tests/test_working_scale_ui.py')
    if ($LASTEXITCODE) { throw 'Selected working-scale UI transaction checks failed.' }
    & python (Join-Path $Root 'tests/test_unified_ui.py') -v
    if ($LASTEXITCODE) { throw 'Final flat UI contract checks failed.' }
    & python (Join-Path $Root 'tests/test_quality_panel.py') -v
    if ($LASTEXITCODE) { throw 'Quality policy and UI checks failed.' }
    foreach ($MathTest in @('run_colour_restore_math.py','run_colour_stage_math.py','run_colour_edge_math.py','run_quality_recipe.py')) {
        & python (Join-Path $Root "tests/$MathTest") --compiler $Cxx
        if ($LASTEXITCODE) { throw "Production colour math failed: $MathTest" }
    }
}
# Retain the old beta050 Windows gates not already covered by the canonical
# generated-source builder. Routing research CI to that builder must not silently
# drop its existing ownership, observer, MinHook or negative-control checks.
New-Item -ItemType Directory -Path $InheritedGate | Out-Null
$OldLlvmRoot = $env:DLSS5_LLVM_MINGW_ROOT
Push-Location $Root
try {
    $env:DLSS5_LLVM_MINGW_ROOT = (Resolve-Path -LiteralPath $ToolchainRoot).Path
    foreach ($Test in @('validate-044-compat.py', 'validate-retirement-return.py', 'validate-compat-sweep.py',
            'validate-diagnostic-identity.py', 'validate-guide-history.py', 'validate-ui-font-fallback.py', 'validate-043r2.py')) {
        & python (Join-Path $PSScriptRoot $Test) --compiler $Cxx
        if ($LASTEXITCODE -ne 0) { throw "Inherited Windows regression failed: $Test" }
    }
    & python (Join-Path $PSScriptRoot 'validate-null-pso-policy.py')
    if ($LASTEXITCODE -ne 0) { throw 'Null-PSO policy regression failed.' }
    foreach ($Test in @('validate-resource-contracts.ps1', 'validate-observer.ps1', 'validate-stage-capture.ps1',
            'validate-minhook-extended-relay.ps1', 'validate-present-api-status.ps1')) {
        & (Join-Path $PSScriptRoot $Test) -ToolchainRoot $ToolchainRoot
    }
    & python (Join-Path $PSScriptRoot 'validate-generic-renodx-conflict.py') --toolchain-root $ToolchainRoot
    if ($LASTEXITCODE -ne 0) { throw 'Generic RenoDX conflict regression failed.' }
    & (Join-Path $PSScriptRoot 'validate-command-state-gpu.ps1') -ToolchainRoot $ToolchainRoot -NegativeControl
    & (Join-Path $PSScriptRoot 'validate-command-state-gpu.ps1') -ToolchainRoot $ToolchainRoot -DynamicStateNegativeControl
    $Snapshot = Join-Path $InheritedGate 'post-snapshot.exe'
    & $Cxx '-std=c++20' '-O0' '-Wall' '-Wextra' '-Werror' '-static' '-Isrc' 'tests/post_sr_snapshot_gpu.cpp' '-o' $Snapshot '-ld3d12' '-ldxgi' '-ldxguid'
    if ($LASTEXITCODE -ne 0) { throw 'Post-SR snapshot WARP compilation failed.' }
    $SnapshotArgs = @()
    if ($AllowMissingDebugLayer) { $SnapshotArgs += '--allow-no-debug' }
    $Positive = & $Snapshot @SnapshotArgs
    if ($LASTEXITCODE -ne 0) { throw 'Post-SR snapshot WARP oracle failed.' }
    $Positive | Set-Content -LiteralPath (Join-Path $InheritedGate 'post-snapshot-positive.txt') -Encoding UTF8
    $SnapshotDebug = ($Positive -join "`n") -match 'snapshot_debug_layer=1'
    $OldErrorAction = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $Negative = & $Snapshot @SnapshotArgs --negative-control 2>&1
        $NegativeExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $OldErrorAction }
    $Negative | Out-String | Set-Content -LiteralPath (Join-Path $InheritedGate 'post-snapshot-negative.txt') -Encoding UTF8
    if ($NegativeExit -ne 1 -or ($Negative -join "`n") -notmatch 'native/snapshot FP16 bytes') {
        throw 'Post-SR negative control did not reject the expected wrong pixel.'
    }
    $global:LASTEXITCODE = 0
} finally {
    Pop-Location
    $env:DLSS5_LLVM_MINGW_ROOT = $OldLlvmRoot
}
# Full established bilingual generation/build/WARP, not a bare Core compile.
$CanonicalArgs = @{}
foreach ($Key in $PSBoundParameters.Keys) {
    if ($Key -ne 'AllowMissingDebugLayer') { $CanonicalArgs[$Key] = $PSBoundParameters[$Key] }
}
& (Join-Path $PSScriptRoot 'build-multi-api-candidate.ps1') @CanonicalArgs
if ($LASTEXITCODE -ne 0) { throw 'Canonical candidate build failed.' }
$Manifest = Get-Content -LiteralPath (Join-Path $Output 'source/source-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$GeneratedVersion = if ($QualitySuccessor -and $Manifest.PSObject.Properties['private_residual_sr']) {
    if ($Manifest.private_residual_sr.source_commit -ne $Head) { throw 'Private SR source identity differs from the candidate.' }
    $Manifest.private_residual_sr.version
} elseif ($QualitySuccessor) { $Manifest.colour_lab.version } else { $Manifest.integration.version }
if ($Manifest.source_commit -ne $Head -or $GeneratedVersion -ne $CandidateVersion) {
    throw 'Generated identity does not match the perf12 source HEAD.'
}
$ValidationPath = Join-Path $Output 'validation.json'
$Validation = Get-Content -LiteralPath $ValidationPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($Validation.source_commit -ne $Head -or $Validation.candidate -ne $CandidateVersion -or
    -not $Validation.source_verified -or -not $Validation.compile_verified -or -not $Validation.warp_verified) {
    throw 'Canonical source/compile/WARP evidence is incomplete.'
}
# Run the changed descriptor registry through the whole exact staged Core, and
# the source transport GPU oracle separately. They are not replaced by CPU tests.
& python (Join-Path $PSScriptRoot 'validate-descriptor-provenance.py') --compiler $Cxx --source (Join-Path $Output 'source/core/src/nr_before_sr.cpp') --reshade $ReShadeSourceRoot --output ($PerfGate + '/descriptor-warp')
if ($LASTEXITCODE -ne 0) { throw 'Exact Core descriptor WARP failed.' }
& python (Join-Path $PSScriptRoot 'validate-source-transport.py') --compiler $Cxx --output ($PerfGate + '/source-transport')
if ($LASTEXITCODE -ne 0) { throw 'Source transport WARP failed.' }
$Build = Join-Path $Output 'source/core/build'
$Addons = @(Get-ChildItem -LiteralPath $Build -Filter '*.addon64' -File)
$Maps = @(Get-ChildItem -LiteralPath (Join-Path $Build 'obj') -Filter '*.map' -File)
if ($Addons.Count -ne 2 -or $Maps.Count -lt 2) { throw 'Expected two ordinary language builds and exact linker maps.' }
if (@($Addons | Where-Object Name -Match '\.en\.addon64$').Count -ne 1) { throw 'Expected exactly one English and one Chinese Core.' }
$ShortIdentity = $CandidateVersion -match '^[0-9]+\.[0-9]+\.[0-9]+-beta\.[0-9]+$'
$PackagePrefix = if ($ShortIdentity) { 'DLSS5-Core-' } else { 'DLSS5-D21-' }
$ReadmeIdentity = if ($ShortIdentity) { 'Core ' + $CandidateVersion } else { 'D21 ' + $LeafVersion }
$PrivateSymbols = Join-Path $Output 'PRIVATE-SYMBOLS'
New-Item -ItemType Directory -Path $PrivateSymbols | Out-Null
foreach ($Map in $Maps) { Copy-Item -LiteralPath $Map.FullName -Destination $PrivateSymbols }
$Rows = @()
foreach ($Addon in $Addons) {
    $Language = if ($Addon.Name -match '\.en\.addon64$') { 'en' } else { 'zh-CN' }
    $Folder = Join-Path $Output ('TEST-' + $Language)
    New-Item -ItemType Directory -Path $Folder | Out-Null
    $Leaf = 'nr-before-sr.' + $LeafVersion + '.' + $Language + '.addon64'
    $Target = Join-Path $Folder $Leaf
    Copy-Item -LiteralPath $Addon.FullName -Destination $Target
    $Hash = (Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash.ToLowerInvariant()
    $Note = @(
        ($ReadmeIdentity + ' - owner game-test candidate, NOT a game-verified release.')
        ('Source: ' + $Head)
        ('SHA256: ' + $Hash)
        'Exit the game. Keep only ONE Core add-on; move the old Core outside all add-on search folders.'
        'Keep INI, NR models, nrchain, ReShade, SR/RR/FG DLLs, settings and image workload unchanged.'
        'First compare cold-start F6 OFF, then ON -> OFF, then single NR -> SR / SR -> NR. Test dual NR separately.'
        'Use identical scene/settings. Compare frame times and synchronized GPU data, not total CPU percentage alone.'
        'Any bypass, missing effect, flicker or crash: roll back and upload F8 TXT plus the Core log with this build identity.'
        'Safe bypass is not a passing performance test. Do not enable a debug layer or verbose per-setter logging for timing.'
        $(if ($ShortIdentity) { 'Retain the previous tested Core package for rollback. PRIVATE-SYMBOLS is not a game payload.' } else { 'Retain the old D21lock package for rollback. PRIVATE-SYMBOLS is not a game payload.' })
    ) -join "`r`n"
    [IO.File]::WriteAllText((Join-Path $Folder 'READ-FIRST.txt'), $Note, (New-Object Text.UTF8Encoding($false)))
    $Zip = Join-Path $Output ($PackagePrefix + $LeafVersion + '-' + $Language + '-test.zip')
    Compress-Archive -Path (Join-Path $Folder '*') -DestinationPath $Zip
    $Rows += [ordered]@{ language=$Language; file=$Leaf; sha256=$Hash; source_commit=$Head
        archive=(Split-Path -Leaf $Zip); archive_sha256=(Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$Final = [ordered]@{
    candidate=$CandidateVersion; source_commit=$Head; ordinary_build=$true
    concurrency_gate=(Join-Path $Gate 'validation.json'); hotpath_gate=(Join-Path $HotpathGate 'validation.json')
    canonical_validation=$ValidationPath; inherited_windows_regressions=$true
    perf3_gate=(Join-Path $PerfGate 'validation.json'); perf4_gate=(Join-Path $Perf4Gate 'validation.json'); perf5_gate=(Join-Path $Perf5Gate 'validation.json'); perf6_gate=(Join-Path $Perf6Gate 'validation.json'); perf7_gate=(Join-Path $Perf7Gate 'validation.json'); perf8_gate=(Join-Path $Perf8Gate 'validation.json'); perf9_gate=(Join-Path $Perf9Gate 'validation.json'); perf10_gate=(Join-Path $Perf10Gate 'validation.json'); snapshot_debug_layer_verified=$SnapshotDebug
    capture_preflight_gate=(Join-Path $Perf9Gate 'capture-preflight/validation.json')
    aux_history_gate=(Join-Path $AuxGate 'validation.json'); aux_parent_gate=(Join-Path $MemoryGate 'validation.json')
    snapshot_gpu_readback_verified=$true; snapshot_negative_control_verified=$true
    game_tested=$false; nvidia_feature18_tested=$false; deployed=$false
    created_utc=[DateTime]::UtcNow.ToString('o'); binaries=$Rows
}
$Final | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $Output ($LeafVersion.ToUpperInvariant() + '-CANDIDATE.json')) -Encoding UTF8
Write-Host 'Candidate built for owner testing. No game folder was changed.'
Write-Host $Output
