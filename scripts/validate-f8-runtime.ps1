$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$toolchain = $env:DLSS5_LLVM_MINGW_ROOT
if ([string]::IsNullOrWhiteSpace($toolchain)) {
    $toolchain = Join-Path (Split-Path -Parent (Split-Path -Parent $repo)) 'toolchains/llvm-mingw-20260826-ucrt-x86_64'
}
$compiler = Join-Path $toolchain 'bin/x86_64-w64-mingw32-clang++.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw "Missing compiler: $compiler" }
$out = Join-Path $env:TEMP 'f8-runtime-harness.exe'
& $compiler -std=c++20 -Wall -Wextra -Werror (Join-Path $repo 'tests/f8_runtime_harness.cpp') -o $out
if ($LASTEXITCODE -ne 0) { throw "F8 runtime harness compilation failed" }
& $out
if ($LASTEXITCODE -ne 0) { throw "F8 runtime harness failed" }
Write-Output 'F8 runtime validation passed.'
