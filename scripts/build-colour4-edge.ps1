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
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$Root=Split-Path -Parent $PSScriptRoot
if (Test-Path -LiteralPath $OutputRoot) { throw 'Use a new output directory; tested packages are never replaced.' }
foreach ($Required in @($ToolchainRoot,$ReShadeSourceRoot)) {
    if ([string]::IsNullOrWhiteSpace($Required)) { throw 'Supply the existing LLVM-MinGW and ReShade paths.' }
}
& python (Join-Path $Root 'tests/test_quality_panel.py') -v
if ($LASTEXITCODE -ne 0) { throw 'Quality recipe/panel source checks failed.' }
$Compiler=Join-Path $ToolchainRoot 'bin/x86_64-w64-mingw32-clang++.exe'
if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) { throw 'Host compiler not found.' }
& python -m py_compile (Join-Path $PSScriptRoot 'patch-colour-restore.py') (Join-Path $PSScriptRoot 'patch-colour-stage.py') (Join-Path $PSScriptRoot 'patch-colour-edge.py') (Join-Path $PSScriptRoot 'prepare-colour4-edge.py')
if ($LASTEXITCODE -ne 0) { throw 'Generator syntax failed.' }
& python (Join-Path $Root 'tests/test_colour_source_gates.py') -v
if ($LASTEXITCODE -ne 0) { throw 'Source-selection gates failed.' }
foreach ($MathTest in @('run_colour_restore_math.py','run_colour_stage_math.py','run_colour_edge_math.py','run_quality_recipe.py')) {
    & python (Join-Path $Root "tests/$MathTest") --compiler $Compiler
    if ($LASTEXITCODE -ne 0) { throw "Production host math failed: $MathTest" }
}
foreach ($SourceTest in @('test_colour_lab.py','test_colour_restore.py','test_colour_stage.py','test_colour_edge.py')) {
    & python (Join-Path $Root "tests/$SourceTest") --repo $Root -v
    if ($LASTEXITCODE -ne 0) { throw "Actual source gate failed: $SourceTest" }
}
& python (Join-Path $PSScriptRoot 'prepare-colour4-edge.py') --core $Root --upstream $UpstreamRoot --output $OutputRoot
if ($LASTEXITCODE -ne 0) { throw 'Pinned full generation and intermediate gates failed.' }
$GeneratedRoot=Join-Path $OutputRoot 'core'
& python (Join-Path $Root 'tests/test_colour_edge.py') --repo $Root --generated $GeneratedRoot -v
if ($LASTEXITCODE -ne 0) { throw 'Final edge stage check failed.' }
$Options=@{ToolchainRoot=$ToolchainRoot;ReShadeSourceRoot=$ReShadeSourceRoot}
if ($FaceRuntimeRoot) { $Options.FaceRuntimeRoot=$FaceRuntimeRoot }
if ($NgxSdkRoot) { $Options.NgxSdkRoot=$NgxSdkRoot }
if ($VcVarsPath) { $Options.VcVarsPath=$VcVarsPath }
if ($ReuseBridgeRoot) { $Options.ReuseBridgeRoot=$ReuseBridgeRoot }
& (Join-Path $GeneratedRoot 'scripts/build.ps1') @Options
if (-not $?) { throw 'Full Windows Core/canonical build failed.' }
$Manifest=Get-Content -LiteralPath (Join-Path $OutputRoot 'source-manifest.json') -Raw | ConvertFrom-Json
if ($Manifest.colour_lab.version -ne 'beta0.5-dline21-unified4') { throw 'Wrong candidate identity.' }
Write-Host 'Build command returned successfully. Inspect the actual Core/shader validation reports and binary identity.'
Write-Host 'No game is modified. Keep the existing INI, bridge and NVIDIA DLLs. No game/performance result is implied.'
Write-Host 'Quality1: new configs use colour-priority + D21 composition. Saved experiments are preserved and warned, not silently changed.'
Write-Host 'Compare Colour baseline with N2 shadow comparison at a fixed raster. This is not an RGB halo or 99/100 colour fix.'
Write-Host 'On no effect, halos, flicker or bypass, capture F7/F8, revert the option and report a BUG.'
