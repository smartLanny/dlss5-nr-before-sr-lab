param(
    [string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([string]::IsNullOrWhiteSpace($ToolchainRoot)) {
    throw 'Provide -ToolchainRoot or set DLSS5_LLVM_MINGW_ROOT.'
}
$projectRoot = Split-Path -Parent $PSScriptRoot
$compiler = Join-Path $ToolchainRoot 'bin\x86_64-w64-mingw32-clang++.exe'
$source = Get-Content -LiteralPath (Join-Path $projectRoot 'src\nr_before_sr.cpp') -Raw
$harness = Get-Content -LiteralPath (Join-Path $projectRoot 'tests\dlssg_feature11_harness.cpp.in') -Raw

function Read-SourceSlice([string]$begin, [string]$end) {
    $start = $source.IndexOf($begin, [StringComparison]::Ordinal)
    if ($start -lt 0) { throw "Missing source marker: $begin" }
    $finish = $source.IndexOf($end, $start, [StringComparison]::Ordinal)
    if ($finish -lt 0) { throw "Missing source marker: $end" }
    return $source.Substring($start, $finish - $start)
}

$harness = $harness.Replace('// @PRODUCTION_DLSSG_CREATE@',
    (Read-SourceSlice 'NgxResult HookDlssgCreate(' 'bool ShouldGuardFeature1ForRayReconstruction('))
$harness = $harness.Replace('// @PRODUCTION_DLSSG_EVALUATE@',
    (Read-SourceSlice 'NgxResult HookDlssgEvaluate(' 'NgxResult HookRelease('))
$harness = $harness.Replace('// @PRODUCTION_DLSSG_RELEASE@',
    (Read-SourceSlice 'NgxResult HookDlssgRelease(' 'bool CreateHook('))

if ($source -notmatch 'void\s*\*g_evaluate_hook_targets\[8\]') {
    throw 'Evaluate target registry does not have the required DLSSG headroom.'
}

$build = Join-Path $projectRoot 'build\dlssg-feature11-validation'
New-Item -ItemType Directory -Path $build -Force | Out-Null
$generated = Join-Path $build 'dlssg_feature11_validation.cpp'
$binary = Join-Path $build 'dlssg_feature11_validation.exe'
[IO.File]::WriteAllText($generated, $harness, [Text.UTF8Encoding]::new($false))
& $compiler '-std=c++20' '-O0' '-static' '-Wall' '-Wextra' '-Werror' `
    $generated '-o' $binary
if ($LASTEXITCODE -ne 0) { throw 'DLSSG Feature 11 harness compilation failed.' }
& $binary
if ($LASTEXITCODE -ne 0) { throw 'DLSSG Feature 11 regression validation failed.' }
