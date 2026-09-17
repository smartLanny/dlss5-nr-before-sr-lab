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
if ((Test-Path -LiteralPath $Output) -or (Test-Path -LiteralPath $Gate)) {
    throw 'Choose a new OutputRoot. Existing source, logs and binaries are not overwritten.'
}
$Head = (& git -C $Root rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $Head -notmatch '^[0-9a-f]{40}$') { throw 'Cannot identify candidate HEAD.' }
$Dirty = @(& git -C $Root status --porcelain --untracked-files=no)
if ($LASTEXITCODE -ne 0 -or $Dirty.Count) { throw 'Commit the isolated reviewed candidate before generation; no automatic commit or reset is performed.' }
$Cxx = Join-Path (Resolve-Path -LiteralPath $ToolchainRoot).Path 'bin/x86_64-w64-mingw32-clang++.exe'
& python (Join-Path $PSScriptRoot 'validate-command-state-lockperf.py') --root $Root --compiler $Cxx --output $Gate
if ($LASTEXITCODE -ne 0) { throw 'New real-Windows SRW concurrency gate failed. Do not build/deploy.' }
# Reuse the exact established D21 generator, contracts, ordinary bilingual build
# and WARP gates. Do not bypass them with a bare nr_before_sr.cpp compilation.
& (Join-Path $PSScriptRoot 'build-multi-api-candidate.ps1') @PSBoundParameters
if ($LASTEXITCODE -ne 0) { throw 'Established candidate build failed.' }
$ManifestPath = Join-Path $Output 'source/source-manifest.json'
$Manifest = Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($Manifest.source_commit -ne $Head -or $Manifest.integration.version -ne 'beta0.5-dline21-uniform3-lockperf1-sottr1') {
    throw 'Generated source identity does not match uniform3-lockperf1-sottr1 HEAD.'
}
$ValidationPath = Join-Path $Output 'validation.json'
$Validation = Get-Content -LiteralPath $ValidationPath -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $Validation.source_verified -or -not $Validation.compile_verified -or -not $Validation.warp_verified) {
    throw 'Canonical compile/source/WARP evidence is incomplete.'
}
$Build = Join-Path $Output 'source/core/build'
$Addons = @(Get-ChildItem -LiteralPath $Build -Filter '*.addon64' -File)
if ($Addons.Count -ne 2) { throw 'Expected exactly two ordinary language add-ons.' }
$Maps = @(Get-ChildItem -LiteralPath (Join-Path $Build 'obj') -Filter '*.map' -File)
if ($Maps.Count -lt 2) { throw 'Exact linker maps are missing; preserve a debuggable candidate.' }
$PrivateSymbols = Join-Path $Output 'PRIVATE-SYMBOLS'
New-Item -ItemType Directory -Path $PrivateSymbols | Out-Null
foreach ($Map in $Maps) { Copy-Item -LiteralPath $Map.FullName -Destination $PrivateSymbols }
$Rows = @()
foreach ($Addon in $Addons) {
    $Language = if ($Addon.Name -match '\.en\.addon64$') { 'en' } else { 'zh-CN' }
    $Folder = Join-Path $Output ('TEST-' + $Language)
    New-Item -ItemType Directory -Path $Folder | Out-Null
    $Leaf = 'nr-before-sr.uniform3-lockperf1-sottr1.' + $Language + '.addon64'
    $Target = Join-Path $Folder $Leaf
    Copy-Item -LiteralPath $Addon.FullName -Destination $Target
    $Hash = (Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash.ToLowerInvariant()
    $Rows += [ordered]@{ language=$Language; file=$Leaf; sha256=$Hash; source_commit=$Head }
    $Note = @(
        'D21 uniform3-lockperf1-sottr1 - SOTTR game testing candidate, not a verified release.'
        ('Source: ' + $Head)
        ('SHA256: ' + $Hash)
        'Exit the game. Keep only ONE Core add-on loaded; move the old Core outside every add-on search folder.'
        'Keep all existing INI, models, nrchain, ReShade, game SR/RR/FG DLLs and image settings unchanged.'
        'For SOTTR: start with game Ray Tracing OFF, enable DLSS, then verify F6 ON/OFF and capture F8 after one minute. Do not use two language DLLs together.'
        'Expected log: recovered legacy nvngx_dlss -> _nvngx Feature 1 wrapper alias. If absent, attach the F8 TXT and nr-before-sr.log.'
        'Use existing D21 as rollback. Linker maps are in PRIVATE-SYMBOLS, not a game payload.'
    ) -join "`r`n"
    [IO.File]::WriteAllText((Join-Path $Folder 'READ-FIRST.txt'),$Note,(New-Object Text.UTF8Encoding($false)))
}
$Final = [ordered]@{
    candidate='beta0.5-dline21-uniform3-lockperf1-sottr1'; source_commit=$Head; ordinary_build=$true
    concurrency_gate=(Join-Path $Gate 'validation.json'); canonical_validation=$ValidationPath
    game_tested=$false; nvidia_feature18_tested=$false; deployed=$false
    created_utc=[DateTime]::UtcNow.ToString('o'); binaries=$Rows
}
$Final | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $Output 'UNIFORM3-LOCKPERF1-SOTTR1-CANDIDATE.json') -Encoding UTF8
Write-Host 'Candidate ready for owner testing. No game folder has been modified.'
Write-Host $Output
