param(
    [Parameter(Mandatory=$true)][string]$UpstreamRoot,
    [Parameter(Mandatory=$true)][string]$ToolchainRoot,
    [Parameter(Mandatory=$true)][string]$ReShadeSourceRoot,
    [Parameter(Mandatory=$true)][string]$DirectXMathRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [string]$NrchainReuseRoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Root = Split-Path -Parent $PSScriptRoot
$Output = [IO.Path]::GetFullPath($OutputRoot)
$Gate = $Output + '.lockperf-checks'
$HotpathGate = $Output + '.hotpath-checks'
foreach ($Path in @($Output, $Gate, $HotpathGate)) {
    if (Test-Path -LiteralPath $Path) { throw 'Choose a new OutputRoot; existing evidence is never overwritten.' }
}
$Head = (& git -C $Root rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $Head -notmatch '^[0-9a-f]{40}$') { throw 'Cannot identify candidate HEAD.' }
$Dirty = @(& git -C $Root status --porcelain --untracked-files=no)
if ($LASTEXITCODE -ne 0 -or $Dirty.Count) { throw 'Commit the isolated candidate before generation; no automatic commit/reset is performed.' }
$Cxx = Join-Path (Resolve-Path -LiteralPath $ToolchainRoot).Path 'bin/x86_64-w64-mingw32-clang++.exe'
# Same-list exclusion, independent-list concurrency, registry lifetime and the
# actual descriptor readers must still pass with real Windows SRW primitives.
& python (Join-Path $PSScriptRoot 'validate-command-state-lockperf.py') --root $Root --compiler $Cxx --output $Gate
if ($LASTEXITCODE -ne 0) { throw 'Native SRW concurrency gate failed. Do not build/deploy.' }
# Frozen pre-fix source must FAIL both work budgets, while this source passes.
& python (Join-Path $PSScriptRoot 'validate-command-state-hotpath.py') --root $Root --compiler $Cxx --output $HotpathGate
if ($LASTEXITCODE -ne 0) { throw 'Hotpath budgets or state equivalence failed. Do not build/deploy.' }
# Full established bilingual generation/build/WARP, not a bare Core compile.
& (Join-Path $PSScriptRoot 'build-multi-api-candidate.ps1') @PSBoundParameters
if ($LASTEXITCODE -ne 0) { throw 'Canonical candidate build failed.' }
$Manifest = Get-Content -LiteralPath (Join-Path $Output 'source/source-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ($Manifest.source_commit -ne $Head -or $Manifest.integration.version -ne 'beta0.5-dline21-lockperf2') {
    throw 'Generated identity does not match the lockperf2 source HEAD.'
}
$ValidationPath = Join-Path $Output 'validation.json'
$Validation = Get-Content -LiteralPath $ValidationPath -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $Validation.source_verified -or -not $Validation.compile_verified -or -not $Validation.warp_verified) {
    throw 'Canonical source/compile/WARP evidence is incomplete.'
}
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
    $Leaf = 'nr-before-sr.lockperf2.' + $Language + '.addon64'
    $Target = Join-Path $Folder $Leaf
    Copy-Item -LiteralPath $Addon.FullName -Destination $Target
    $Hash = (Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash.ToLowerInvariant()
    $Note = @(
        'D21 lockperf2 - owner game-test candidate, NOT a game-verified release.'
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
    $Zip = Join-Path $Output ('DLSS5-D21-lockperf2-' + $Language + '-test.zip')
    Compress-Archive -Path (Join-Path $Folder '*') -DestinationPath $Zip
    $Rows += [ordered]@{ language=$Language; file=$Leaf; sha256=$Hash; source_commit=$Head
        archive=(Split-Path -Leaf $Zip); archive_sha256=(Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$Final = [ordered]@{
    candidate='beta0.5-dline21-lockperf2'; source_commit=$Head; ordinary_build=$true
    concurrency_gate=(Join-Path $Gate 'validation.json'); hotpath_gate=(Join-Path $HotpathGate 'validation.json')
    canonical_validation=$ValidationPath; game_tested=$false; nvidia_feature18_tested=$false; deployed=$false
    created_utc=[DateTime]::UtcNow.ToString('o'); binaries=$Rows
}
$Final | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $Output 'LOCKPERF2-CANDIDATE.json') -Encoding UTF8
Write-Host 'Candidate built for owner testing. No game folder was changed.'
Write-Host $Output
