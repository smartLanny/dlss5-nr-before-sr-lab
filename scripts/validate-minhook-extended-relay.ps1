param(
    [Parameter(Mandatory = $true)]
    [string]$ToolchainRoot
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $projectRoot 'build\test-minhook-extended-relay'
$cc = Join-Path $ToolchainRoot 'bin\x86_64-w64-mingw32-clang.exe'
$cxx = Join-Path $ToolchainRoot 'bin\x86_64-w64-mingw32-clang++.exe'
$include = Join-Path $projectRoot 'third_party\minhook\include'
$source = Join-Path $projectRoot 'third_party\minhook\src'

Remove-Item -LiteralPath $buildRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $buildRoot | Out-Null

$objects = @()
foreach ($name in @('buffer', 'hook', 'trampoline')) {
    $object = Join-Path $buildRoot "$name.o"
    & $cc -std=c11 -O2 -I $include -I $source -I (Join-Path $source 'hde') `
        -c (Join-Path $source "$name.c") -o $object
    if ($LASTEXITCODE -ne 0) { throw "Failed to compile MinHook $name.c" }
    $objects += $object
}
$hde = Join-Path $buildRoot 'hde64.o'
& $cc -std=c11 -O2 -I (Join-Path $source 'hde') `
    -c (Join-Path $source 'hde\hde64.c') -o $hde
if ($LASTEXITCODE -ne 0) { throw 'Failed to compile hde64.c' }
$objects += $hde

$exe = Join-Path $buildRoot 'minhook-extended-relay.exe'
& $cxx -std=c++20 -O2 -I $include `
    (Join-Path $projectRoot 'tests\minhook_extended_relay_harness.cpp') `
    @objects -o $exe
if ($LASTEXITCODE -ne 0) { throw 'Failed to link extended relay harness' }

& $exe
if ($LASTEXITCODE -eq 77) {
    throw 'Extended relay harness could not create its deterministic address-space fixture.'
}
if ($LASTEXITCODE -ne 0) { throw 'Extended relay allocation regression failed.' }
