# Package only the frozen full-canonical result. No runtimes, deployment or Release.
param(
    [Parameter(Mandatory=$true)][string]$BuildRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Build = (Resolve-Path -LiteralPath $BuildRoot).Path
$Version = (Get-Content -LiteralPath (Join-Path $Repo 'scripts/core-source-layout.json') -Raw | ConvertFrom-Json).product.version
$Candidate = Join-Path $Build ($Version.ToUpperInvariant() + '-CANDIDATE.json')
$Receipt = Get-Content -LiteralPath $Candidate -Raw | ConvertFrom-Json
if ($Receipt.candidate -ne $Version) { throw 'Canonical candidate identity differs from the committed product' }
$Face = Get-Content -LiteralPath ($Build + '.face-checks/validation.json') -Raw | ConvertFrom-Json
if (-not $Face.success -or -not $Face.warp_tracker_verified -or -not $Face.cpu_detector_worker_verified) {
    throw 'Complete face WARP/CPU execution evidence is required before hosted packaging'
}
$CoreBuild = Join-Path $Build 'source/core/build'
foreach ($Language in @('zh-CN', 'en')) {
    $Rows = @($Receipt.binaries | Where-Object { $_.language -eq $Language })
    if ($Rows.Count -ne 1 -or [IO.Path]::GetFileName($Rows[0].file) -ne $Rows[0].file) { throw 'Missing/unsafe canonical language row' }
    $Core = Join-Path (Join-Path $Build ('TEST-' + $Language)) $Rows[0].file
    & python (Join-Path $Repo 'scripts/package-core-ota.py') --candidate $Candidate --checkout $Repo `
        --core $Core --bridge (Join-Path $CoreBuild 'nrchain_nvngx.dll') --language $Language `
        --changelog (Join-Path $Repo 'docs/maintenance/CLOUD_WINDOWS_CANDIDATE.md') `
        --notice (Join-Path $CoreBuild 'obj/temporal/AMD-FidelityFX-LICENSE.txt') `
        --output (Join-Path $OutputRoot $Language)
    if ($LASTEXITCODE) { throw "Frozen Core-only packaging failed: $Language" }
}
Write-Host 'Bilingual Core-only candidates packaged. No NVIDIA runtime, face assets, INI, game deployment or Release publication.'
