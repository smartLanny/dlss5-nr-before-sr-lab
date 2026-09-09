param([string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([string]::IsNullOrWhiteSpace($ToolchainRoot)) { throw 'Provide -ToolchainRoot or DLSS5_LLVM_MINGW_ROOT.' }
$nvRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$nvBuild = Join-Path $nvRoot 'build\nvfp4-backend-tests'
New-Item -ItemType Directory -Path $nvBuild -Force | Out-Null
$nvC = Join-Path $ToolchainRoot 'bin\x86_64-w64-mingw32-clang.exe'
$nvCpp = Join-Path $ToolchainRoot 'bin\x86_64-w64-mingw32-clang++.exe'
$nvObjects = @()
foreach ($nvSource in @('buffer.c','hook.c','trampoline.c','hde\hde64.c')) {
    $nvObject = Join-Path $nvBuild (([IO.Path]::GetFileNameWithoutExtension($nvSource)) + '.o')
    & $nvC '-O1' '-c' (Join-Path $nvRoot ('third_party\minhook\src\' + $nvSource)) '-o' $nvObject
    if ($LASTEXITCODE -ne 0) { throw "MinHook test dependency compilation failed: $nvSource" }
    $nvObjects += $nvObject
}
foreach ($nvTest in @('backend','observer')) {
    $nvExe = Join-Path $nvBuild ($nvTest + '-test.exe')
    & $nvCpp '-std=c++20' '-O1' '-g' '-Wall' '-Wextra' '-Wpedantic' '-static' `
        ('-I' + (Join-Path $nvRoot 'third_party\minhook\include')) `
        (Join-Path $PSScriptRoot ($nvTest + '_test.cpp')) @nvObjects '-o' $nvExe '-lbcrypt' '-ld3d12' '-ldxgi' '-ldxguid' '-luuid'
    if ($LASTEXITCODE -ne 0) { throw "NVFP4 $nvTest test compilation failed." }
    & $nvExe
    if ($LASTEXITCODE -ne 0) { throw "NVFP4 $nvTest tests failed." }
}
