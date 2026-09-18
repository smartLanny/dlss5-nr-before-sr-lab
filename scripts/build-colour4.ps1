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
if (Test-Path -LiteralPath $OutputRoot) { throw 'Use a new OutputRoot; existing test packages/worktrees are preserved.' }
foreach ($Required in @($ToolchainRoot, $ReShadeSourceRoot)) {
    if ([string]::IsNullOrWhiteSpace($Required)) { throw 'Supply the existing LLVM-MinGW and ReShade paths.' }
}
$Compiler = Join-Path $ToolchainRoot 'bin/x86_64-w64-mingw32-clang++.exe'
if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) { throw 'LLVM-MinGW host compiler not found.' }
& python -m py_compile (Join-Path $Root 'scripts/patch-colour-restore.py') (Join-Path $Root 'scripts/patch-colour-stage.py') (Join-Path $Root 'scripts/prepare-colour4.py')
if ($LASTEXITCODE -ne 0) { throw 'Colour generator syntax check failed.' }
& python (Join-Path $Root 'tests/test_colour_source_gates.py') -v
if ($LASTEXITCODE -ne 0) { throw 'Selected-source test routing failed.' }
& python (Join-Path $Root 'tests/run_colour_restore_math.py') --compiler $Compiler
if ($LASTEXITCODE -ne 0) { throw 'Inherited production RGB math failed.' }
& python (Join-Path $Root 'tests/run_colour_stage_math.py') --compiler $Compiler
if ($LASTEXITCODE -ne 0) { throw 'Colour4 production reference/sampling math failed.' }
& python (Join-Path $Root 'tests/test_colour_lab.py') --repo $Root -v
if ($LASTEXITCODE -ne 0) { throw 'Colour admission source gate failed.' }
& python (Join-Path $Root 'tests/test_colour_restore.py') --repo $Root -v
if ($LASTEXITCODE -ne 0) { throw 'Colour3 source gate failed.' }
& python (Join-Path $Root 'tests/test_colour_stage.py') --repo $Root -v
if ($LASTEXITCODE -ne 0) { throw 'Colour4 actual-source gate failed.' }
& python (Join-Path $PSScriptRoot 'prepare-colour4.py') --core $Root --upstream $UpstreamRoot --output $OutputRoot
if ($LASTEXITCODE -ne 0) { throw 'Pinned colour4 generation failed.' }
$GeneratedRoot = Join-Path $OutputRoot 'core'
& python (Join-Path $Root 'tests/test_colour_stage.py') --generated $GeneratedRoot -v
if ($LASTEXITCODE -ne 0) { throw 'Colour4 final generated-source gate failed.' }
$Options = @{ToolchainRoot=$ToolchainRoot; ReShadeSourceRoot=$ReShadeSourceRoot}
if ($FaceRuntimeRoot) { $Options.FaceRuntimeRoot = $FaceRuntimeRoot }
if ($NgxSdkRoot) { $Options.NgxSdkRoot = $NgxSdkRoot }
if ($VcVarsPath) { $Options.VcVarsPath = $VcVarsPath }
if ($ReuseBridgeRoot) { $Options.ReuseBridgeRoot = $ReuseBridgeRoot }
& (Join-Path $GeneratedRoot 'scripts/build.ps1') @Options
if (-not $?) { throw 'Complete Windows Core/canonical build failed.' }
$Manifest = Get-Content -LiteralPath (Join-Path $OutputRoot 'source-manifest.json') -Raw | ConvertFrom-Json
if ($Manifest.colour_lab.version -ne 'beta0.5-dline21-colour4') { throw 'Wrong candidate identity.' }
Write-Host "Windows builder returned successfully for $($Manifest.colour_lab.source_commit). Inspect core/build and its actual validation reports."
Write-Host 'No package is installed automatically. Keep existing INI, bridge, signed model and game SR/RR/FG DLLs.'
Write-Host 'Start at single-layer 99%, old input curve, probe OFF: compare protected RGB to raw RGB. Then turn ON the session-only zero-edit probe and collect F7/F8.'
Write-Host 'Native SR output/game quality/performance are NOT validated by this command. Bypass/halos/flicker: revert and submit F8 + Core log + binary hash.'
