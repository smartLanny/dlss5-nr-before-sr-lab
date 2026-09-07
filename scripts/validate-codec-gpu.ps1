param([string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT, [switch]$NegativeControl, [switch]$LayerNegativeControl, [string]$SourceRoot)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([string]::IsNullOrWhiteSpace($ToolchainRoot)) {
    throw 'Provide -ToolchainRoot or set DLSS5_LLVM_MINGW_ROOT.'
}
$projectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($SourceRoot)) { $SourceRoot = $projectRoot }
$source = Get-Content -LiteralPath (Join-Path $SourceRoot 'src\nr_before_sr.cpp') -Raw -Encoding UTF8
$harness = Get-Content -LiteralPath (Join-Path $projectRoot 'tests\codec_gpu_harness.cpp.in') -Raw -Encoding UTF8
$shader = [regex]::Match($source, 'const char kCodecShader\[\] = R"HLSL\([\s\S]*?\)HLSL";')
$constants = [regex]::Match($source, 'struct CodecConstants \{[\s\S]*?\n\};')
$descriptors = [regex]::Match($source, 'constexpr UINT kCodecSrvCount[\s\S]*?constexpr UINT kCodecRootConstantCount = \d+;')
if (!$shader.Success -or !$constants.Success -or !$descriptors.Success) {
    throw 'Production codec extraction markers changed; update the GPU harness.'
}
$harness = $harness.Replace('// @PRODUCTION_SHADER@', $shader.Value)
$boostTests = Get-Content -LiteralPath (Join-Path $projectRoot 'tests\high_strength_gpu.inl') -Raw -Encoding UTF8
$harness = $harness.Replace('// @V3_BOOST_TESTS@', $boostTests)
$layerTests = Get-Content -LiteralPath (Join-Path $projectRoot 'tests\layered_composite_gpu.inl') -Raw -Encoding UTF8
$harness = $harness.Replace('// @LAYER_TESTS@', $layerTests)
$harness = $harness.Replace('// @PRODUCTION_CONSTANTS@', $constants.Value)
$harness = $harness.Replace('// @PRODUCTION_DESCRIPTOR_CONSTANTS@', $descriptors.Value)
$writer = Get-Content -LiteralPath (Join-Path $SourceRoot 'src\nr_codec_descriptors.inl') -Raw -Encoding UTF8
$writerTests = Get-Content -LiteralPath (Join-Path $projectRoot 'tests\codec_descriptors_gpu.inl') -Raw -Encoding UTF8
$statsCount = [regex]::Match($source, 'constexpr unsigned int kDiagnosticStatsUintCount = \d+;')
if (!$statsCount.Success -or !$source.Contains('WritePostSrSnapshotDescriptors(device, slot);')) {
    throw 'Production descriptor writer is not wired into CreateSlot or stats count changed.'
}
# The standalone core needs neither helper. The transformed compatibility core
# uses both. Pull the actual pure format mapping into the C++ COM pixel harness;
# never include the unrelated C-interface device/query adapter or duplicate math.
$helperDefinitions = ''
foreach ($entry in @(
    @('NrMotionReadFormat', 'nr_motion_access_d3d12.inl'),
    @('NrExposureReadFormat', 'nr_exposure_access_d3d12.inl')
)) {
    $name = $entry[0]
    if ($writer.Contains($name + '(')) {
        $helperPath = Join-Path $SourceRoot ('src\interop\' + $entry[1])
        $helperText = Get-Content -LiteralPath $helperPath -Raw -Encoding UTF8
        $pattern = '(?ms)^DXGI_FORMAT ' + [regex]::Escape($name) + '\([^{}]*\)\s*\{.*?^\}'
        $definitions = [regex]::Matches($helperText, $pattern)
        if ($definitions.Count -ne 1) { throw "Non-unique production codec helper: $name" }
        $helperDefinitions += $definitions[0].Value + "`n"
    }
}
$writerTests = $writerTests.Replace('// @PRODUCTION_DESCRIPTOR_WRITER@', $helperDefinitions + $statsCount.Value + "`n" + $writer)
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
if ($LayerNegativeControl) {
    $negative = & $binary --layer-negative-control 2>&1
    if ($LASTEXITCODE -ne 1 -or ($negative -join "`n") -notmatch 'LAYER_DIRECTION_ORACLE') {
        throw 'Layer negative control must reject the precise unattenuated bright edit.'
    }
    $global:LASTEXITCODE = 0
    Write-Host 'Layer negative control rejected the expected unchanged bright edit.'
} elseif ($NegativeControl) {
    $negative = & $binary --negative-control 2>&1
    if ($LASTEXITCODE -ne 1 -or ($negative -join "`n") -notmatch 'V3_SDR_EXTRA_BOOST_COLOUR') {
        throw 'Negative control must reject the exact unprotected SDR colour result, not a crash.'
    }
    $global:LASTEXITCODE = 0
    Write-Host 'V3 negative control rejected the expected old clipped SDR result.'
} else {
    & $binary
    if ($LASTEXITCODE -ne 0) { throw 'GPU codec validation failed.' }
}
