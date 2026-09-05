param(
    [string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($ToolchainRoot)) {
    throw 'Provide -ToolchainRoot or set DLSS5_LLVM_MINGW_ROOT.'
}

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Compiler = Join-Path (Resolve-Path -LiteralPath $ToolchainRoot).Path `
    'bin\x86_64-w64-mingw32-clang++.exe'
if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
    throw "Compiler not found: $Compiler"
}

$OutputDirectory = Join-Path $ProjectRoot 'test-output'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$Output = Join-Path $OutputDirectory 'present-api-status.exe'
$Source = Join-Path $ProjectRoot 'tests\present_api_status_harness.cpp'

& $Compiler '-std=c++20' '-O2' '-Wall' '-Wextra' '-Wpedantic' '-static' `
    $Source '-o' $Output
if ($LASTEXITCODE -ne 0) { throw 'Present API status harness compilation failed.' }

& $Output
if ($LASTEXITCODE -ne 0) { throw 'Present API status harness failed.' }
