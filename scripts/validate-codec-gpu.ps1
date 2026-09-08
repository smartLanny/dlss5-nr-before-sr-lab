param([string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT, [switch]$NegativeControl, [switch]$LayerNegativeControl, [string]$SourceRoot, [string]$BaselineSource, [string]$ComparisonOutput, [switch]$CompileOnly, [string]$EditFieldOutput, [switch]$ColorEditField)

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
if (![string]::IsNullOrWhiteSpace($BaselineSource)) {
    $baseline = Get-Content -LiteralPath $BaselineSource -Raw -Encoding UTF8
    $baselineShader = [regex]::Match($baseline, 'const char kCodecShader\[\] = R"HLSL\([\s\S]*?\)HLSL";')
    if (!$baselineShader.Success -or !$baseline.Contains('ModelLightingTapV2')) { throw 'Expected actual maintenance shader with edge-aware LightingLock.' }
    $harness = $harness.Replace('// @MAINTENANCE_SHADER@', $baselineShader.Value.Replace('kCodecShader', 'kMaintenanceShader'))
    $comparison = if ($ColorEditField) { '' } else { @'
        Gpu baseline(kMaintenanceShader);
        unsigned comparison_count = 0;
        for (bool before : {false,true}) for (UINT flags : {0u,1u,16u,32u,64u,128u,192u})
        for (float strength : {0.f,.5f,1.f,1.2f,2.f,4.f}) for (int variant : {0,1,3,6,7,8}) {
            const auto old = Run(baseline,13,13,0,1,flags,variant,before,7,5,{},.6f,{},strength);
            const auto now = Run(gpu,13,13,0,1,flags,variant,before,7,5,{},.6f,{},strength);
            Require(old.output.size()==now.output.size(), "MAINTENANCE_NEUTRAL_SIZE");
            Require(std::memcmp(old.output.data(),now.output.data(),old.output.size()*sizeof(Float4))==0,
                "MAINTENANCE_NEUTRAL_PIXELS");
            ++comparison_count;
        }
        std::printf("Actual maintenance vs 0.5 disabled protection/layers: %u byte-identical WARP outputs.\n",comparison_count);
'@ }
    $harness = $harness.Replace('// @MAINTENANCE_COMPARISON@', $comparison)
}
$boostTests = Get-Content -LiteralPath (Join-Path $projectRoot 'tests\high_strength_gpu.inl') -Raw -Encoding UTF8
$harness = $harness.Replace('// @V3_BOOST_TESTS@', $boostTests)
$layerTests = Get-Content -LiteralPath (Join-Path $projectRoot 'tests\layered_composite_gpu.inl') -Raw -Encoding UTF8
$harness = $harness.Replace('// @LAYER_TESTS@', $layerTests)
if ($ColorEditField) {
    if ($NegativeControl -or $LayerNegativeControl -or $ComparisonOutput -or $EditFieldOutput) { throw 'Run colour EditField independently.' }
    if (![string]::IsNullOrWhiteSpace($BaselineSource)) {
        $baselineHash = (Get-FileHash -LiteralPath $BaselineSource -Algorithm SHA256).Hash.ToLowerInvariant()
        $harness = '#define NR_EDIT_FIELD_BASELINE_SOURCE_SHA "' + $baselineHash + '"' + "`n" + $harness
    }
    $colorTests = Get-Content -LiteralPath (Join-Path $projectRoot 'tests/color_edit_field_gpu.inl') -Raw -Encoding UTF8
    $harness = $harness.Replace('// @COMPOSITION_CONTRACT_TESTS@', $colorTests)
    $harness = $harness.Replace('// @COMPOSITION_CONTRACT_ENTRY@', 'if (argc == 2 && std::strcmp(argv[1], "--color-edit-field") == 0) { ValidateColorEditField(gpu); return 0; }')
} elseif (![string]::IsNullOrWhiteSpace($EditFieldOutput)) {
    if ($NegativeControl -or $LayerNegativeControl -or $ComparisonOutput) { throw 'Run EditField export independently.' }
    if (![string]::IsNullOrWhiteSpace($BaselineSource)) {
        $baselineHash = (Get-FileHash -LiteralPath $BaselineSource -Algorithm SHA256).Hash.ToLowerInvariant()
        $harness = '#define NR_EDIT_FIELD_BASELINE_SOURCE_SHA "' + $baselineHash + '"' + "`n" + $harness
    }
    $editTests = Get-Content -LiteralPath (Join-Path $projectRoot 'tests/edit_field_gpu.inl') -Raw -Encoding UTF8
    $harness = $harness.Replace('// @COMPOSITION_CONTRACT_TESTS@', $editTests)
    $harness = $harness.Replace('// @COMPOSITION_CONTRACT_ENTRY@', 'if (argc == 3 && std::strcmp(argv[1], "--edit-field-contract") == 0) { ExportEditFieldContract(gpu, argv[2]); return 0; }')
} elseif (![string]::IsNullOrWhiteSpace($ComparisonOutput)) {
    if ($NegativeControl -or $LayerNegativeControl -or $BaselineSource) { throw 'Run composition export independently from negative/baseline controls.' }
    $comparisonTests = Get-Content -LiteralPath (Join-Path $projectRoot 'tests/composition_contract_gpu.inl') -Raw -Encoding UTF8
    $harness = $harness.Replace('// @COMPOSITION_CONTRACT_TESTS@', $comparisonTests)
    $harness = $harness.Replace('// @COMPOSITION_CONTRACT_ENTRY@', 'if (argc == 3 && std::strcmp(argv[1], "--composition-contract") == 0) { ExportCompositionContract(gpu, argv[2]); return 0; }')
}
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
if ($CompileOnly) {
    Write-Host 'Production codec fixture compiled only; no WARP/GPU dispatch executed.'
    return
}
if ($ColorEditField) {
    & $binary '--color-edit-field'
    if ($LASTEXITCODE -ne 0) { throw 'Production colour EditField validation failed.' }
} elseif (![string]::IsNullOrWhiteSpace($EditFieldOutput)) {
    & $binary '--edit-field-contract' ([IO.Path]::GetFullPath($EditFieldOutput))
    if ($LASTEXITCODE -ne 0) { throw 'Production EditField export failed.' }
    & python (Join-Path $projectRoot 'scripts/validate-edit-field.py') --capture $EditFieldOutput --source (Join-Path $SourceRoot 'src/nr_before_sr.cpp')
    if ($LASTEXITCODE -ne 0) { throw 'Independent EditField contract validation failed.' }
} elseif (![string]::IsNullOrWhiteSpace($ComparisonOutput)) {
    & $binary '--composition-contract' ([IO.Path]::GetFullPath($ComparisonOutput))
    if ($LASTEXITCODE -ne 0) { throw 'Production pre/post composition export failed.' }
    & python (Join-Path $projectRoot 'scripts/validate-composition-contract.py') --capture $ComparisonOutput --source (Join-Path $SourceRoot 'src/nr_before_sr.cpp')
    if ($LASTEXITCODE -ne 0) { throw 'Independent raw-plane/contract validation failed.' }
} elseif ($LayerNegativeControl) {
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
