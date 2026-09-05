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
$harness = Get-Content -LiteralPath (Join-Path $projectRoot 'tests\observer_harness.cpp.in') -Raw
$primaryStart = $source.IndexOf('NgxResult HookCreate(', [StringComparison]::Ordinal)
$primaryEnd = $source.IndexOf('NgxResult HookCoreCreate(', $primaryStart, [StringComparison]::Ordinal)
if ($primaryStart -lt 0 -or $primaryEnd -le $primaryStart) {
    throw 'Cannot locate the primary Create hook for preflight ordering validation.'
}
$primaryCreate = $source.Substring($primaryStart, $primaryEnd - $primaryStart)
$preflight = $primaryCreate.IndexOf('PreflightFeature1LifecycleModules();', [StringComparison]::Ordinal)
$entryCounter = $primaryCreate.IndexOf('g_wrapper_create_entries.fetch_add', [StringComparison]::Ordinal)
$depthCounter = $primaryCreate.IndexOf('g_create_hook_depth++', [StringComparison]::Ordinal)
if ($preflight -lt 0 -or $entryCounter -lt 0 -or $depthCounter -lt 0 -or
    $preflight -gt $entryCounter -or $preflight -gt $depthCounter) {
    throw 'Feature 1 module preflight must run before primary Create activity/depth counters.'
}
function Read-SourceSlice([string]$begin, [string]$end) {
    $start = $source.IndexOf($begin, [StringComparison]::Ordinal)
    if ($start -lt 0) { throw "Missing source marker: $begin" }
    $finish = $source.IndexOf($end, $start, [StringComparison]::Ordinal)
    if ($finish -lt 0) { throw "Missing source marker: $end" }
    return $source.Substring($start, $finish - $start)
}
$harness = $harness.Replace('// @PRODUCTION_CREATE_HOOK@',
    (Read-SourceSlice 'bool CreateHook(' 'bool EnsureNativeQueueExecuteHook('))
$harness = $harness.Replace('// @PRODUCTION_FINISH_OBSERVATION@',
    (Read-SourceSlice 'void FinishFeature1CreateObservation(' 'void ProcessSubmittedCommandLists('))
$harness = $harness.Replace('// @PRODUCTION_MODULE_OBSERVER@',
    (Read-SourceSlice '// The loader reference closes' 'DWORD WINAPI HookWorker('))
$build = Join-Path $projectRoot 'build\observer-validation'
New-Item -ItemType Directory -Path $build -Force | Out-Null
$generated = Join-Path $build 'observer_validation.cpp'
$binary = Join-Path $build 'observer_validation.exe'
[IO.File]::WriteAllText($generated, $harness, [Text.UTF8Encoding]::new($false))
& $compiler '-std=c++20' '-O0' '-static' '-Wall' '-Wextra' '-Werror' $generated '-o' $binary
if ($LASTEXITCODE -ne 0) { throw 'Observer harness compilation failed.' }
& $binary
if ($LASTEXITCODE -ne 0) { throw 'Observer regression validation failed.' }
