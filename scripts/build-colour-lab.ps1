param(
    [Parameter(Mandatory=$true)][string]$UpstreamRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT,
    [string]$ReShadeSourceRoot = $env:DLSS5_RESHADE_ROOT,
    [string]$FaceRuntimeRoot = $env:DLSS5_FACE_RUNTIME_ROOT,
    [string]$NgxSdkRoot = $env:DLSS5_NGX_SDK_ROOT,
    [string]$VcVarsPath = $env:DLSS5_VCVARS_PATH,
    [string]$ReuseBridgeRoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Root = Split-Path -Parent $PSScriptRoot
if (Test-Path -LiteralPath $OutputRoot) { throw 'OutputRoot must be a new directory; no existing build is overwritten.' }
foreach ($Required in @($ToolchainRoot, $ReShadeSourceRoot)) {
    if ([string]::IsNullOrWhiteSpace($Required)) { throw 'Set the existing LLVM-MinGW and ReShade paths before generating.' }
}
& python (Join-Path $PSScriptRoot 'prepare-colour-lab.py') --core $Root --upstream $UpstreamRoot --output $OutputRoot
if ($LASTEXITCODE -ne 0) { throw 'Colour-lab source preparation failed.' }
$BuildScript = Join-Path $OutputRoot 'core/scripts/build.ps1'
$Options = @{ToolchainRoot=$ToolchainRoot; ReShadeSourceRoot=$ReShadeSourceRoot}
if ($FaceRuntimeRoot) { $Options.FaceRuntimeRoot = $FaceRuntimeRoot }
if ($NgxSdkRoot) { $Options.NgxSdkRoot = $NgxSdkRoot }
if ($VcVarsPath) { $Options.VcVarsPath = $VcVarsPath }
if ($ReuseBridgeRoot) { $Options.ReuseBridgeRoot = $ReuseBridgeRoot }
& $BuildScript @Options
if (-not $?) { throw 'Windows Core build failed.' }
$Manifest = Join-Path $OutputRoot 'source-manifest.json'
$Source = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
if ($Source.colour_lab.version -ne 'beta0.5-dline21-colour1') { throw 'Unexpected output identity.' }
Write-Host 'Build command completed. Inspect core/build outputs and existing build reports.'
Write-Host 'No game files were changed. This is not runtime/game verification.'
Write-Host 'M1 includes colour admission only. RGB restoration is not implemented.'
