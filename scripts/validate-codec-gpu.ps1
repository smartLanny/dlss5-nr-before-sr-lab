param([string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([string]::IsNullOrWhiteSpace($ToolchainRoot)) {
    throw 'Provide -ToolchainRoot or set DLSS5_LLVM_MINGW_ROOT.'
}
$projectRoot = Split-Path -Parent $PSScriptRoot
$source = Get-Content -LiteralPath (Join-Path $projectRoot 'src\nr_before_sr.cpp') -Raw
$harness = Get-Content -LiteralPath (Join-Path $projectRoot 'tests\codec_gpu_harness.cpp.in') -Raw
$shader = [regex]::Match($source, 'const char kCodecShader\[\] = R"HLSL\([\s\S]*?\)HLSL";')
$constants = [regex]::Match($source, 'struct CodecConstants \{[\s\S]*?\n\};')
$descriptors = [regex]::Match($source, 'constexpr UINT kCodecSrvCount[\s\S]*?constexpr UINT kCodecRootConstantCount = \d+;')
if (!$shader.Success -or !$constants.Success -or !$descriptors.Success) {
    throw 'Production codec extraction markers changed; update the GPU harness.'
}
$harness = $harness.Replace('// @PRODUCTION_SHADER@', $shader.Value)
$harness = $harness.Replace('// @PRODUCTION_CONSTANTS@', $constants.Value)
$harness = $harness.Replace('// @PRODUCTION_DESCRIPTOR_CONSTANTS@', $descriptors.Value)
$writer = Get-Content -LiteralPath (Join-Path $projectRoot 'src\nr_codec_descriptors.inl') -Raw
$writerTests = Get-Content -LiteralPath (Join-Path $projectRoot 'tests\codec_descriptors_gpu.inl') -Raw
$statsCount = [regex]::Match($source, 'constexpr unsigned int kDiagnosticStatsUintCount = \d+;')
if (!$statsCount.Success -or !$source.Contains('WritePostSrSnapshotDescriptors(device, slot);')) {
    throw 'Production descriptor writer is not wired into CreateSlot or stats count changed.'
}
$writerTests = $writerTests.Replace('// @PRODUCTION_DESCRIPTOR_WRITER@', $statsCount.Value + "`n" + $writer)
$harness = $harness.Replace('// @PRODUCTION_DESCRIPTOR_TESTS@', $writerTests)
$build = Join-Path $projectRoot 'build\codec-gpu-validation'
New-Item -ItemType Directory -Path $build -Force | Out-Null
$generated = Join-Path $build 'codec_gpu_validation.cpp'
$binary = Join-Path $build 'codec_gpu_validation.exe'
[IO.File]::WriteAllText($generated, $harness, [Text.UTF8Encoding]::new($false))
$compiler = Join-Path $ToolchainRoot 'bin\x86_64-w64-mingw32-clang++.exe'
& $compiler '-std=c++20' '-O2' '-static' '-Wall' '-Wextra' '-Werror' $generated `
    '-o' $binary '-ld3d12' '-ldxgi' '-ldxguid' '-ld3dcompiler'
if ($LASTEXITCODE -ne 0) { throw 'GPU codec harness compilation failed.' }
& $binary
if ($LASTEXITCODE -ne 0) { throw 'GPU codec validation failed.' }
