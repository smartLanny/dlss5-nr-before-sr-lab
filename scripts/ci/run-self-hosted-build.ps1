# Run the canonical build on the maintainer's self-hosted runner.
#
# Dependency roots come from repository variables (passed in as VAR_<name>) or,
# when those are empty, from the runner machine's environment variables of the
# same name. Nothing machine-specific is stored in the workflow. The build keeps
# every gate of scripts/build-unified-candidate.ps1; this wrapper only resolves
# inputs, chooses a fresh output directory and prunes old CI outputs.
param(
    [string]$RunLabel = $env:GITHUB_RUN_ID,
    [int]$KeepOutputs = 3,
    [switch]$CheckInputsOnly
)
$ErrorActionPreference = 'Stop'
$Repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

function Resolve-Input([string]$Name, [switch]$Optional) {
    $value = [Environment]::GetEnvironmentVariable("VAR_$Name")
    if ([string]::IsNullOrWhiteSpace($value)) { $value = [Environment]::GetEnvironmentVariable($Name) }
    if ([string]::IsNullOrWhiteSpace($value)) {
        if ($Optional) { return $null }
        $script:Missing += $Name
        return $null
    }
    return $value
}

$script:Missing = @()
$Inputs = [ordered]@{
    UpstreamRoot      = Resolve-Input 'DLSS5_UPSTREAM_ROOT'
    ToolchainRoot     = Resolve-Input 'DLSS5_TOOLCHAIN_ROOT'
    ReShadeSourceRoot = Resolve-Input 'DLSS5_RESHADE_SOURCE_ROOT'
    DirectXMathRoot   = Resolve-Input 'DLSS5_DIRECTXMATH_ROOT'
    FaceRuntimeRoot   = Resolve-Input 'DLSS5_FACE_RUNTIME_ROOT'
    FfxSdkRoot        = Resolve-Input 'DLSS5_FFX_SDK_ROOT'
    FfxCacheRoot      = Resolve-Input 'DLSS5_FFX_CACHE_ROOT'
}
$BuildRoot = Resolve-Input 'DLSS5_BUILD_ROOT'
$NrchainReuseRoot = Resolve-Input 'DLSS5_NRCHAIN_REUSE_ROOT' -Optional
$CodecCacheRoot = Resolve-Input 'DLSS5_CODEC_CACHE_ROOT' -Optional
$AllowMissingDebugLayer = (Resolve-Input 'DLSS5_ALLOW_MISSING_DEBUG_LAYER' -Optional) -ne 'false'

if ($script:Missing.Count) {
    throw ('Missing build inputs: ' + ($script:Missing -join ', ') +
        '. Set them as repository variables or as environment variables on the runner machine.')
}
foreach ($entry in $Inputs.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $entry.Value -PathType Container)) {
        throw "$($entry.Key) does not exist: $($entry.Value)"
    }
}
foreach ($optional in @(@('NrchainReuseRoot', $NrchainReuseRoot), @('CodecCacheRoot', $CodecCacheRoot))) {
    if ($optional[1] -and -not (Test-Path -LiteralPath $optional[1] -PathType Container)) {
        throw "$($optional[0]) does not exist: $($optional[1])"
    }
}
if ([string]::IsNullOrWhiteSpace($RunLabel)) { $RunLabel = Get-Date -Format 'yyyyMMdd-HHmmss' }
$Attempt = if ($env:GITHUB_RUN_ATTEMPT) { $env:GITHUB_RUN_ATTEMPT } else { '1' }
$CiRoot = Join-Path $BuildRoot 'ci'
$OutputRoot = Join-Path $CiRoot "ci-$RunLabel-$Attempt"
if (Test-Path -LiteralPath $OutputRoot) { throw "Output directory already exists: $OutputRoot" }

Write-Host "Repository: $Repo"
Write-Host "Source commit: $(git -C $Repo rev-parse HEAD)"
Write-Host "Output: $OutputRoot"
Write-Host "Missing debug layer allowed: $AllowMissingDebugLayer"
if ($CheckInputsOnly) { Write-Host 'Inputs resolved; build not started (-CheckInputsOnly).'; return }

New-Item -ItemType Directory -Path $CiRoot -Force | Out-Null
# Windows PowerShell 5.1 cannot add an ordered dictionary to a hashtable.
$Parameters = @{}
foreach ($entry in $Inputs.GetEnumerator()) { $Parameters[$entry.Key] = $entry.Value }
$Parameters.OutputRoot = $OutputRoot
if ($NrchainReuseRoot) { $Parameters.NrchainReuseRoot = $NrchainReuseRoot }
if ($CodecCacheRoot) { $Parameters.CodecCacheRoot = $CodecCacheRoot }
if ($AllowMissingDebugLayer) { $Parameters.AllowMissingDebugLayer = $true }

& (Join-Path $Repo 'scripts/build-unified-candidate.ps1') @Parameters
if ($LASTEXITCODE) { throw "Canonical build failed with exit code $LASTEXITCODE; output kept at $OutputRoot" }

$Manifest = Join-Path $OutputRoot 'source/source-manifest.json'
if (Test-Path -LiteralPath $Manifest) {
    Write-Host "source-manifest.json SHA256: $((Get-FileHash -LiteralPath $Manifest -Algorithm SHA256).Hash.ToLowerInvariant())"
}
$Validation = Join-Path $OutputRoot 'validation.json'
if (Test-Path -LiteralPath $Validation) {
    Write-Host "validation.json SHA256: $((Get-FileHash -LiteralPath $Validation -Algorithm SHA256).Hash.ToLowerInvariant())"
}

# Prune only after a successful build: a failed run throws above and keeps every
# output. A build writes <OutputRoot> plus sibling <OutputRoot>.<gate>-checks
# folders, so whole runs are pruned together and the current run is never touched.
# The current run and the newest $KeepOutputs - 1 other runs remain.
$Others = @(Get-ChildItem -LiteralPath $CiRoot -Directory |
    Where-Object { $_.Name -match '^ci-[^.]+$' -and $_.FullName -ne $OutputRoot } |
    Sort-Object LastWriteTime -Descending | Select-Object -Skip ([Math]::Max($KeepOutputs - 1, 0)))
foreach ($run in $Others) {
    $prefix = $run.Name + '.'
    $group = @($run) + @(Get-ChildItem -LiteralPath $CiRoot -Directory |
        Where-Object { $_.Name.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) })
    foreach ($directory in $group) {
        Write-Host "Removing old CI output: $($directory.FullName)"
        Remove-Item -LiteralPath $directory.FullName -Recurse -Force
    }
}
