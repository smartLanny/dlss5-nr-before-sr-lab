param(
    [string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT,
    [switch]$MetaProbeOnly,
    [switch]$NegativeControl,
    [switch]$CpuDescriptorNegativeControl,
    [switch]$DynamicStateNegativeControl
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([string]::IsNullOrWhiteSpace($ToolchainRoot)) {
    throw 'Provide -ToolchainRoot or set DLSS5_LLVM_MINGW_ROOT.'
}
$projectRoot = Split-Path -Parent $PSScriptRoot
if (@(@($MetaProbeOnly,$NegativeControl,$CpuDescriptorNegativeControl,$DynamicStateNegativeControl) | Where-Object { $_ }).Count -gt 1) {
    throw 'Select one validation mode.'
}
$source = Get-Content -LiteralPath (Join-Path $projectRoot 'src\nr_command_state.inl') -Raw
$harness = Get-Content -LiteralPath (Join-Path $projectRoot 'tests\command_state_gpu_harness.cpp.in') -Raw
$model = [regex]::Match($source, '(?ms)^// BEGIN COMMAND_STATE_MODEL[^\r\n]*\r?\n(.*?)^// END COMMAND_STATE_MODEL')
if (!$model.Success) {
    throw 'Production command-state extraction markers changed; update the GPU harness.'
}
$production = $model.Groups[1].Value
if ($NegativeControl) {
    # Mutate only this generated test translation unit. Valid bindings still
    # execute safely; wrong constants must be caught by independent GPU oracles.
    if (!$production.Contains('argument.constants + begin')) {
        throw 'Negative-control extraction marker changed.'
    }
    $production = $production.Replace('argument.constants + begin', 'g_wrong_restore_constants + begin')
    $harness = $harness.Replace('// @NEGATIVE_CONTROL_CONSTANTS@', 'const UINT g_wrong_restore_constants[64] = {};')
}
if ($CpuDescriptorNegativeControl) {
    $marker='if (!state.null_pipeline_baseline || (!count && !depth)) return;'
    if (!$production.Contains($marker)) { throw 'CPU descriptor negative-control marker changed.' }
    $production=$production.Replace($marker,'return; // Deliberately retain recycled application CPU handles.')
}
if ($DynamicStateNegativeControl) {
    $marker='d.viewport_count, d.viewports'
    if (!$production.Contains($marker)) { throw 'Dynamic-state negative-control marker changed.' }
    # Keep a valid D3D12 binding so this fails through the pixel oracle, rather
    # than relying on an invalid topology/RTV dispatch or device removal.
    $production=$production.Replace($marker,'d.viewport_count, g_wrong_restore_viewports')
    $harness=$harness.Replace('// @NEGATIVE_CONTROL_CONSTANTS@','const D3D12_VIEWPORT g_wrong_restore_viewports[16] = {{0,0,0.25f,0.25f,0,1}};')
}
$harness = $harness.Replace('// @PRODUCTION_STATE@', $production)
$build = Join-Path $projectRoot 'build\command-state-gpu-validation'
New-Item -ItemType Directory -Path $build -Force | Out-Null
$stem = if ($NegativeControl) { 'command_state_gpu_negative_control' } elseif ($CpuDescriptorNegativeControl) { 'command_state_gpu_cpu_descriptor_negative' } elseif ($DynamicStateNegativeControl) { 'command_state_gpu_dynamic_negative' } else { 'command_state_gpu_validation' }
$generated = Join-Path $build ($stem + '.cpp')
$binary = Join-Path $build ($stem + '.exe')
[IO.File]::WriteAllText($generated, $harness, [Text.UTF8Encoding]::new($false))
$compiler = Join-Path $ToolchainRoot 'bin\x86_64-w64-mingw32-clang++.exe'
& $compiler '-std=c++20' '-O2' '-static' '-Wall' '-Wextra' '-Werror' $generated `
    '-o' $binary '-ld3d12' '-ldxgi' '-ldxguid' '-ld3dcompiler'
if ($LASTEXITCODE -ne 0) { throw 'Command-state GPU harness compilation failed.' }
if ($NegativeControl) {
    $output = & $binary 2>&1
    $exitCode = $LASTEXITCODE
    $output | ForEach-Object { Write-Host $_ }
    if ($exitCode -ne 1 -or ($output -join "`n") -notmatch 'Restored compute state produced wrong output') {
        throw 'GPU oracle failed to reject the deliberately corrupted restore.'
    }
    Write-Host 'Negative control passed: GPU oracle rejected wrong restored constants.'
    exit 0
}
if ($CpuDescriptorNegativeControl -or $DynamicStateNegativeControl) {
    $output = & $binary '--known-null-only' 2>&1
    $exitCode = $LASTEXITCODE
    $output | ForEach-Object { Write-Host $_ }
    if ($exitCode -ne 1 -or ($output -join "`n") -notmatch 'Restored graphics state produced wrong output') {
        throw 'Known-null GPU oracle failed to reject the corrupted dynamic/CPU descriptor restoration.'
    }
    Write-Host 'Negative control passed: known-null GPU oracle rejected incorrect graphics restoration.'
    exit 0
}
if ($MetaProbeOnly) { & $binary '--meta-probe-only' } else { & $binary }
if ($LASTEXITCODE -ne 0) { throw 'Command-state GPU validation failed.' }
