param([string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT, [switch]$PoolNegativeControl, [switch]$NativeRecordOnly, [switch]$FirstRecordingOnly, [switch]$FirstRecordingNegativeControl, [switch]$BuildOnly, [switch]$VrsNullOnly, [switch]$VrsNullNegativeControl, [switch]$VrsReplayNegativeControl)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([string]::IsNullOrWhiteSpace($ToolchainRoot)) { throw 'Provide -ToolchainRoot.' }
$projectRoot = Split-Path -Parent $PSScriptRoot
$production = Get-Content -LiteralPath (Join-Path $projectRoot 'src\nr_command_state.inl') -Raw
$production = $production.Replace('#include "nr_command_pool_observation.inl"', (Get-Content -LiteralPath (Join-Path $projectRoot 'src\nr_command_pool_observation.inl') -Raw))
$production = $production.Replace('#include "nr_command_creation_observer.inl"', (Get-Content -LiteralPath (Join-Path $projectRoot 'src\nr_command_creation_observer.inl') -Raw))
$production = $production.Replace('#include "nr_native_command_record.inl"', (Get-Content -LiteralPath (Join-Path $projectRoot 'src\nr_native_command_record.inl') -Raw))
if ($VrsNullNegativeControl) {
    $needle='kDynamicBlend | kDynamicTargets | kDynamicShadingRate))'
    if (!$production.Contains($needle)) { throw 'VRS admission inverse anchor missing' }
    $production=$production.Replace($needle,'kDynamicBlend | kDynamicTargets))')
}
if ($VrsReplayNegativeControl) {
    $needle='if (d.recorded & kDynamicShadingRate) CommandStateExtendedCall('
    if (!$production.Contains($needle)) { throw 'VRS replay inverse anchor missing' }
    $production=$production.Replace($needle,'if (false) CommandStateExtendedCall(')
}
if ($PoolNegativeControl) {
    $needle='PrimeCommandListBeforeResetLocked(*record, native);' # Removes both pre-notification and native-result recovery to reproduce old lazy-only policy
    if (!$production.Contains($needle)) { throw 'Pool negative anchor missing' }
    $production=$production.Replace($needle,'(void)0; // exact old lazy-only defect')
}
if ($FirstRecordingNegativeControl) {
    $needle='record->state->first_recording_observed = true;'
    if (!$production.Contains($needle)) { throw 'First-recording negative anchor missing' }
    $production=$production.Replace($needle,'record->state->first_recording_observed = false; // old no-first-recording baseline')
}
$main = Get-Content -LiteralPath (Join-Path $projectRoot 'src\nr_before_sr.cpp') -Raw
# Both entries in the current Session suffix must use the resolved native
# interface captured by before_state. Dispatch behavior has a compiled fixture;
# these integration assertions retain the resolved-interface/state contract.
$integrationExpressions = @(
    'const auto prepare_before = \[&\]\s*\{\s*return PrepareBeforeSr\(\s*before_state.command_list,\s*handle,\s*params,\s*feature\);\s*\};',
    'if\(nr_processing_runtime::Effective\(\)==nr_processing::Start::Before\)\s*\{\s*before_sr=prepare_before\(\);',
    'if\(!before_sr.fatal_recording && !nr_processing_runtime::Recorded\(\) &&\s*nr_processing_runtime::Effective\(\)==nr_processing::Start::After\)\s*\{[\s\S]*?after_sr=PrepareAfterSr\(\s*before_state.command_list,\s*handle,\s*params,\s*feature,\s*compatibility_fallback\);',
    'before_state.Restore\(\);\s*if \(!before_sr.active && !after_sr.active\)',
    'NativeCommandStateScope after_state\(command_list\);[\s\S]*?FinishAfterSr\(after_state.command_list,',
    'const HRESULT result = original\(command_list, allocator, initial_state\);\s*nr_source_runtime::OnReset\(command_list,SUCCEEDED\(result\)\);\s*OnCommandStateResetResult\(command_list, result, initial_state\);'
)
foreach ($expression in $integrationExpressions) {
    if ($main -notmatch $expression) { throw "Command-state integration invariant missing: $expression" }
}
# Keep the pointer-domain checks sensitive to the original defect: a private
# route must not receive the unresolved application/proxy command-list pointer.
foreach ($entry in @('PrepareBeforeSr', 'PrepareAfterSr')) {
    $anchor = "$entry(before_state.command_list,"
    if (!$main.Contains($anchor)) { throw "Resolved-interface inverse anchor missing: $entry" }
    $inverse = $main.Replace($anchor, "$entry(command_list,")
    $rejected = @($integrationExpressions | Where-Object { $inverse -notmatch $_ })
    if (!$rejected.Count) { throw "Resolved-interface inverse control missed: $entry" }
}
Write-Output 'Current Session resolved-interface assertions and both raw-list inverse controls passed.'
$harness = Get-Content -LiteralPath (Join-Path $projectRoot 'tests\command_state_native_harness.cpp.in') -Raw
$harness = $harness.Replace('// @PRODUCTION_COMMAND_STATE@', $production)
$pristine = Get-Content -LiteralPath (Join-Path $projectRoot 'tests\pristine_null_native.inl') -Raw
$pristine = (Get-Content -LiteralPath (Join-Path $projectRoot 'tests\command_pool_native.inl') -Raw) + "`n" + $pristine
$harness = $harness.Replace('// @PRISTINE_NULL_TEST@', $pristine)
$harness = $harness.Replace('// @NATIVE_RECORD_TEST@', (Get-Content -LiteralPath (Join-Path $projectRoot 'tests\native_command_record.inl') -Raw))
$harness = $harness.Replace('// @FIRST_RECORDING_TEST@', (Get-Content -LiteralPath (Join-Path $projectRoot 'tests\first_recording_native.inl') -Raw))
$harness = $harness.Replace('// @VRS_NULL_TEST@', (Get-Content -LiteralPath (Join-Path $projectRoot 'tests\vrs_null_native.inl') -Raw))
# Preserve existing checkpoints if a native test process terminates before flushing.
# This affects the test executable only, never the extracted production source.
$entry = 'int main(int argc, char **argv) {'
if ([regex]::Matches($harness, [regex]::Escape($entry)).Count -ne 1) { throw 'Test entry anchor missing.' }
$harness = $harness.Replace($entry, $entry + "`n    std::setvbuf(stdout, nullptr, _IONBF, 0);`n    std::setvbuf(stderr, nullptr, _IONBF, 0);`n")
$build = Join-Path $projectRoot 'build\command-state-native-validation'
New-Item -ItemType Directory -Path $build -Force | Out-Null
$generated = Join-Path $build 'command_state_native.cpp'
$binary = Join-Path $build 'command_state_native.exe'
$warpHelper = Join-Path $build 'ci_warp_runtime.h'
Copy-Item -LiteralPath (Join-Path $projectRoot 'tests\ci_warp_runtime.h') -Destination $warpHelper -Force
[ordered]@{ schema=1; warp_runtime_helper_sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $warpHelper).Hash.ToLowerInvariant() } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $build ('command_state_native.warp-helper.json')) -Encoding utf8
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
if ($BuildOnly) { Write-Output "Built only: $binary"; exit 0 }
if ($VrsNullOnly -or $VrsNullNegativeControl -or $VrsReplayNegativeControl) {
    $output=& $binary '--vrs-null' 2>&1
    $code=$LASTEXITCODE
    $output | ForEach-Object { Write-Host $_ }
    if ($VrsNullNegativeControl -or $VrsReplayNegativeControl) {
        $oracle=if ($VrsNullNegativeControl) { 'FAIL VRS_DEFAULT_ADMISSION_ORACLE' } else { 'FAIL VRS_SLOT77_REPLAY_ORACLE' }
        if ($code -ne 41 -or ($output -join "`n") -notmatch $oracle) { throw 'VRS inverse control failed for the wrong reason' }
        Write-Output "VRS inverse control detected: $oracle"
    } elseif ($code -ne 0) { throw "VRS null gate failed (exit=$code)." }
    exit 0
}
# This bounded native API gate runs in the complete entry too (no submission).
& $binary '--vrs-null'
if ($LASTEXITCODE -ne 0) { throw 'VRS null gate failed.' }
if ($FirstRecordingOnly -or $FirstRecordingNegativeControl) {
    $output=& $binary '--first-recording' 2>&1
    $code=$LASTEXITCODE
    $output | ForEach-Object { Write-Host $_ }
    Write-Host "Native gate --first-recording exit=$code"
    if ($FirstRecordingNegativeControl) {
        if ($code -ne 41 -or ($output -join "`n") -notmatch 'FIRST_RECORDING_NATIVE_ORACLE') { throw 'Wrong first-recording negative failure' }
        Write-Output 'Negative control passed: no Create-boundary baseline misses the first carrier.'
    } elseif ($code -ne 0) { throw "First-recording gate failed (exit=$code)." }
    exit 0
}
if ($NativeRecordOnly) {
    $output=& $binary '--native-record' 2>&1
    $code=$LASTEXITCODE
    $output | ForEach-Object { Write-Host $_ }
    Write-Host "Native gate --native-record exit=$code"
    if ($code -ne 0) { throw "Native record gate failed (exit=$code)." }
    exit 0
}
if ($PoolNegativeControl) {
    $output=& $binary '--pristine-null' 2>&1
    $code=$LASTEXITCODE
    $output | ForEach-Object { Write-Host $_ }
    Write-Host "Native gate --pristine-null (pool negative) exit=$code"
    if ($code -ne 41 -or ($output -join "`n") -notmatch 'POOL_FIRST_CAPTURE_ORACLE') { throw 'Wrong pool negative failure' }
    Write-Output 'Negative control passed: old lazy-only observation misses first carrier.'
    exit 0
}
# Route native stdout/stderr through the host so the acceptance transcript
# retains every assertion, including failures. Store the real exit code before
# formatting; failure predicates below are unchanged.
$output=& $binary '--pristine-null' 2>&1
$code=$LASTEXITCODE
$output | ForEach-Object { Write-Host $_ }
Write-Host "Native gate --pristine-null exit=$code"
if ($code -ne 0) { throw 'Default pristine-null native/GPU validation failed.' }
$output=& $binary '--pristine-null-negative' 2>&1
$code=$LASTEXITCODE
$output | ForEach-Object { Write-Host $_ }
Write-Host "Native gate --pristine-null-negative exit=$code"
if ($code -ne 42) { throw "Pristine-null negative control did not detect the deliberately omitted restore (exit=$code)." }
$output=& $binary '--default-enabled' 2>&1
$code=$LASTEXITCODE
$output | ForEach-Object { Write-Host $_ }
Write-Host "Native gate --default-enabled exit=$code"
if ($code -ne 0) { throw "Default-enabled command-state startup validation failed (exit=$code)." }
$output=& $binary 2>&1
$code=$LASTEXITCODE
$output | ForEach-Object { Write-Host $_ }
Write-Host "Native gate default exit=$code"
if ($code -ne 0) { throw "Native command-state validation failed (exit=$code)." }
$output=& $binary '--native-record' 2>&1
$code=$LASTEXITCODE
$output | ForEach-Object { Write-Host $_ }
Write-Host "Native gate --native-record exit=$code"
if ($code -ne 0) { throw "Native record lifecycle validation failed (exit=$code)." }
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
