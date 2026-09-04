param(
    [string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT,
    [Parameter(Mandatory = $true)][string]$Python,
    [switch]$Gpu
)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$compiler = Join-Path $ToolchainRoot 'bin/x86_64-w64-mingw32-clang++.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw 'A real compiler is required; no text-only pass.' }
$arguments = @((Join-Path $PSScriptRoot 'validate-f8-controller.py'), '--compiler', $compiler)
if ($Gpu) { $arguments += '--gpu' }
& $Python @arguments
if ($LASTEXITCODE -ne 0) { throw 'Current production F8 pipeline regression failed.' }
& $Python -m unittest discover -s (Join-Path $project 'tests') -p 'test_f8_parser.py'
if ($LASTEXITCODE -ne 0) { throw 'Independent F8 parser fixtures failed.' }
