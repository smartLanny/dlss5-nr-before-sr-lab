param(
    [Parameter(Mandatory=$true)][string]$UpstreamRoot,
    [Parameter(Mandatory=$true)][string]$ToolchainRoot,
    [Parameter(Mandatory=$true)][string]$ReShadeSourceRoot,
    [Parameter(Mandatory=$true)][string]$DirectXMathRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [string]$NrchainReuseRoot,
    [string]$CodecCacheRoot,
    [switch]$AllowMissingDebugLayer
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Root = Split-Path -Parent $PSScriptRoot
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
$InheritedGate = $Output + '.inherited-checks'
foreach ($Path in @($Output, $Gate, $HotpathGate, $InheritedGate, $PerfGate, $Perf4Gate, $Perf5Gate, $Perf6Gate, $Perf7Gate, $Perf8Gate, $Perf9Gate, $Perf10Gate)) {
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
if ($Manifest.source_commit -ne $Head -or $Manifest.integration.version -ne 'beta0.5-dline21-perf10') {
    throw 'Generated identity does not match the perf10 source HEAD.'
}
$ValidationPath = Join-Path $Output 'validation.json'
$Validation = Get-Content -LiteralPath $ValidationPath -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $Validation.source_verified -or -not $Validation.compile_verified -or -not $Validation.warp_verified) {
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
$PrivateSymbols = Join-Path $Output 'PRIVATE-SYMBOLS'
New-Item -ItemType Directory -Path $PrivateSymbols | Out-Null
foreach ($Map in $Maps) { Copy-Item -LiteralPath $Map.FullName -Destination $PrivateSymbols }
$Rows = @()
foreach ($Addon in $Addons) {
    $Language = if ($Addon.Name -match '\.en\.addon64$') { 'en' } else { 'zh-CN' }
    $Folder = Join-Path $Output ('TEST-' + $Language)
    New-Item -ItemType Directory -Path $Folder | Out-Null
    $Leaf = 'nr-before-sr.perf10.' + $Language + '.addon64'
    $Target = Join-Path $Folder $Leaf
    Copy-Item -LiteralPath $Addon.FullName -Destination $Target
    $Hash = (Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash.ToLowerInvariant()
    $Note = @(
        'D21 perf10 - owner game-test candidate, NOT a game-verified release.'
        ('Source: ' + $Head)
        ('SHA256: ' + $Hash)
        'Exit the game. Keep only ONE Core add-on; move the old Core outside all add-on search folders.'
        'Keep INI, NR models, nrchain, ReShade, SR/RR/FG DLLs, settings and image workload unchanged.'
        'First compare cold-start F6 OFF, then ON -> OFF, then single NR -> SR / SR -> NR. Test dual NR separately.'
        'Use identical scene/settings. Compare frame times and synchronized GPU data, not total CPU percentage alone.'
        'Any bypass, missing effect, flicker or crash: roll back and upload F8 TXT plus the Core log with this build identity.'
        'Safe bypass is not a passing performance test. Do not enable a debug layer or verbose per-setter logging for timing.'
        'Retain the old D21lock package for rollback. PRIVATE-SYMBOLS is not a game payload.'
    ) -join "`r`n"
    [IO.File]::WriteAllText((Join-Path $Folder 'READ-FIRST.txt'), $Note, (New-Object Text.UTF8Encoding($false)))
    $Zip = Join-Path $Output ('DLSS5-D21-perf10-' + $Language + '-test.zip')
    Compress-Archive -Path (Join-Path $Folder '*') -DestinationPath $Zip
    $Rows += [ordered]@{ language=$Language; file=$Leaf; sha256=$Hash; source_commit=$Head
        archive=(Split-Path -Leaf $Zip); archive_sha256=(Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$Final = [ordered]@{
    candidate='beta0.5-dline21-perf10'; source_commit=$Head; ordinary_build=$true
    concurrency_gate=(Join-Path $Gate 'validation.json'); hotpath_gate=(Join-Path $HotpathGate 'validation.json')
    canonical_validation=$ValidationPath; inherited_windows_regressions=$true
    perf3_gate=(Join-Path $PerfGate 'validation.json'); perf4_gate=(Join-Path $Perf4Gate 'validation.json'); perf5_gate=(Join-Path $Perf5Gate 'validation.json'); perf6_gate=(Join-Path $Perf6Gate 'validation.json'); perf7_gate=(Join-Path $Perf7Gate 'validation.json'); perf8_gate=(Join-Path $Perf8Gate 'validation.json'); perf9_gate=(Join-Path $Perf9Gate 'validation.json'); perf10_gate=(Join-Path $Perf10Gate 'validation.json'); snapshot_debug_layer_verified=$SnapshotDebug
    capture_preflight_gate=(Join-Path $Perf9Gate 'capture-preflight/validation.json')
    snapshot_gpu_readback_verified=$true; snapshot_negative_control_verified=$true
    game_tested=$false; nvidia_feature18_tested=$false; deployed=$false
    created_utc=[DateTime]::UtcNow.ToString('o'); binaries=$Rows
}
$Final | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $Output 'PERF10-CANDIDATE.json') -Encoding UTF8
Write-Host 'Candidate built for owner testing. No game folder was changed.'
Write-Host $Output
