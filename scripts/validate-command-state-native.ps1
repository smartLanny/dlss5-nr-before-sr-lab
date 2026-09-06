param([string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT, [switch]$PoolNegativeControl)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([string]::IsNullOrWhiteSpace($ToolchainRoot)) { throw 'Provide -ToolchainRoot.' }
$projectRoot = Split-Path -Parent $PSScriptRoot
$production = Get-Content -LiteralPath (Join-Path $projectRoot 'src\nr_command_state.inl') -Raw
$production = $production.Replace('#include "nr_command_pool_observation.inl"', (Get-Content -LiteralPath (Join-Path $projectRoot 'src\nr_command_pool_observation.inl') -Raw))
if ($PoolNegativeControl) {
    $needle='PrimeCommandListBeforeResetLocked(*record, native);'
    if (!$production.Contains($needle)) { throw 'Pool negative anchor missing' }
    $production=$production.Replace($needle,'(void)0; // exact old lazy-only defect')
}
$main = Get-Content -LiteralPath (Join-Path $projectRoot 'src\nr_before_sr.cpp') -Raw
foreach ($expression in @(
    'Substitution before_sr = PrepareBeforeSr\(\s*before_state.command_list,',
    'PostSrSubstitution after_sr = PrepareAfterSr\(\s*before_state.command_list,',
    'before_state.Restore\(\);\s*if \(!before_sr.active && !after_sr.active\)',
    'NativeCommandStateScope after_state\(command_list\);[\s\S]*?FinishAfterSr\(after_state.command_list,',
    'const HRESULT result = original\(command_list, allocator, initial_state\);\s*OnCommandStateResetResult\(command_list, result, initial_state\);'
)) {
    if ($main -notmatch $expression) { throw "Command-state integration invariant missing: $expression" }
}
$harness = Get-Content -LiteralPath (Join-Path $projectRoot 'tests\command_state_native_harness.cpp.in') -Raw
$harness = $harness.Replace('// @PRODUCTION_COMMAND_STATE@', $production)
$pristine = Get-Content -LiteralPath (Join-Path $projectRoot 'tests\pristine_null_native.inl') -Raw
$pristine = (Get-Content -LiteralPath (Join-Path $projectRoot 'tests\command_pool_native.inl') -Raw) + "`n" + $pristine
$harness = $harness.Replace('// @PRISTINE_NULL_TEST@', $pristine)
$build = Join-Path $projectRoot 'build\command-state-native-validation'
New-Item -ItemType Directory -Path $build -Force | Out-Null
$generated = Join-Path $build 'command_state_native.cpp'
$binary = Join-Path $build 'command_state_native.exe'
[IO.File]::WriteAllText($generated, $harness, [Text.UTF8Encoding]::new($false))
$compiler = Join-Path $ToolchainRoot 'bin\x86_64-w64-mingw32-clang++.exe'
$cCompiler = Join-Path $ToolchainRoot 'bin\x86_64-w64-mingw32-clang.exe'
$minHook = Join-Path $projectRoot 'third_party\minhook'
$objects = @()
foreach ($source in @('buffer', 'hook', 'trampoline', 'hde\hde64')) {
    $object = Join-Path $build ((Split-Path -Leaf $source) + '.o')
    & $cCompiler '-O2' '-c' (Join-Path $minHook "src\$source.c") '-o' $object
    if ($LASTEXITCODE -ne 0) { throw "MinHook validation build failed: $source" }
    $objects += $object
}
& $compiler '-std=c++20' '-O2' '-static' '-Wall' '-Wextra' '-Werror' '-fstack-usage' "-I$(Join-Path $minHook 'include')" $generated @objects '-o' $binary '-ld3d12' '-ldxgi' '-ldxguid' '-ld3dcompiler'
if ($LASTEXITCODE -ne 0) { throw 'Native command-state harness compilation failed.' }
if ($PoolNegativeControl) {
    $output=& $binary '--pristine-null' 2>&1
    $code=$LASTEXITCODE
    $output | ForEach-Object { Write-Host $_ }
    if ($code -ne 41 -or ($output -join "`n") -notmatch 'POOL_FIRST_CAPTURE_ORACLE') { throw 'Wrong pool negative failure' }
    Write-Output 'Negative control passed: old lazy-only observation misses first carrier.'
    exit 0
}
& $binary '--pristine-null'

if ($LASTEXITCODE -ne 0) { throw 'Default pristine-null native/GPU validation failed.' }
& $binary '--pristine-null-negative'
if ($LASTEXITCODE -ne 42) { throw 'Pristine-null negative control did not detect the deliberately omitted restore.' }
& $binary '--default-enabled'
if ($LASTEXITCODE -ne 0) { throw 'Default-enabled command-state startup validation failed.' }
& $binary
if ($LASTEXITCODE -ne 0) { throw 'Native command-state validation failed.' }
$stackFiles = Get-ChildItem -LiteralPath $build -Filter '*.su'
$critical = @()
foreach ($stackFile in $stackFiles) {
    foreach ($line in (Get-Content -LiteralPath $stackFile.FullName)) {
        if ($line -match 'NativeCommandState|SmallStackProbe') {
            $parts = $line -split "`t"
            if ($parts.Count -ge 2 -and [int]$parts[1] -gt 4096) { throw "Command-state frame exceeds 4 KiB: $line" }
            $critical += $line
        }
    }
}
if ($critical.Count -eq 0) { throw 'Compiler stack-use report not found.' }
$critical | Set-Content -LiteralPath (Join-Path $build 'critical-stack-usage.txt') -Encoding utf8
Write-Output "Stack-use gate passed for $($critical.Count) command-state functions (each <= 4096 bytes)."
