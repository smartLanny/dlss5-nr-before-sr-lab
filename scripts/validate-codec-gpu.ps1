param([string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT, [switch]$NegativeControl, [switch]$LayerNegativeControl, [string]$SourceRoot, [string]$BaselineSource, [string]$ComparisonOutput, [switch]$CompileOnly, [string]$EditFieldOutput, [string]$CatmullOutput, [switch]$ColorEditField, [string]$InputFilterOutput, [switch]$CombinedLowGrid, [switch]$LightStyle, [switch]$PostLightStyle)

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
if($CatmullOutput) {
    if($EditFieldOutput -or $InputFilterOutput -or $ColorEditField -or $ComparisonOutput -or $BaselineSource -or $NegativeControl -or $LayerNegativeControl) {throw 'Run Catmull contract independently.'}
    # Test-only non-fused oracle materializes one R-sized field. No production
    # resource/flag or full-resolution EditField allocation is introduced.
    $harness=$harness.Replace('float2 SampleRestoredEdit(float2 center) {',
        'float2 SampleRestoredEdit(float2 center) { if ((Passthrough & 131072u)!=0u) return EditHorizontal.Load(int3(int2(center),0));')
    $referenceEntry=@'
[numthreads(8,8,1)] void RestoreEditReferenceCS(uint3 tid:SV_DispatchThreadID) {
    if(tid.x>=Width || tid.y>=Height) return;
    EditHorizontalOut[tid.xy]=SampleRestoredEdit(float2(tid.xy)+0.5);
}
'@
    $harness=$harness.Replace(')HLSL";', $referenceEntry+"`n"+')HLSL";')
}
if (![string]::IsNullOrWhiteSpace($BaselineSource)) {
    $baseline = Get-Content -LiteralPath $BaselineSource -Raw -Encoding UTF8
    $baselineShader = [regex]::Match($baseline, 'const char kCodecShader\[\] = R"HLSL\([\s\S]*?\)HLSL";')
    if (!$baselineShader.Success -or !$baseline.Contains('ModelLightingTapV2')) { throw 'Expected actual maintenance shader with edge-aware LightingLock.' }
    $harness = $harness.Replace('// @MAINTENANCE_SHADER@', $baselineShader.Value.Replace('kCodecShader', 'kMaintenanceShader'))
    $comparison = if ($ColorEditField -or $InputFilterOutput -or $CombinedLowGrid -or $LightStyle -or $PostLightStyle) { '' } else { @'
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
if($PostLightStyle) {
    if (!$BaselineSource -or $LightStyle -or $CombinedLowGrid -or $CatmullOutput -or $ColorEditField -or $InputFilterOutput -or $EditFieldOutput -or $ComparisonOutput -or $NegativeControl -or $LayerNegativeControl) { throw 'PostLightStyle requires only its frozen generated BaselineSource.' }
    $candidateManifestPath = Join-Path (Split-Path -Parent $SourceRoot) 'source-manifest.json'
    $baselineCore = Split-Path -Parent (Split-Path -Parent $BaselineSource)
    $baselineManifestPath = Join-Path (Split-Path -Parent $baselineCore) 'source-manifest.json'
    $candidateManifest = Get-Content -LiteralPath $candidateManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $baselineManifest = Get-Content -LiteralPath $baselineManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($baselineManifest.source_commit -ne '59ab029614df6fad6ea1e95df18a0a986be58962') { throw 'PostLightStyle baseline must be frozen dline1 source 59ab029, not a raw historical shader.' }
    # Preserve the exact D1 reference. The sole differing adapter pair changes
    # PrepareAfterSr admission, while its shader transform and area helper must
    # remain byte-identical and belong to the recorded source commits.
    & python (Join-Path $PSScriptRoot 'validate-post-transform.py') `
        --baseline-commit $baselineManifest.source_commit --candidate-commit $candidateManifest.source_commit `
        --baseline-pin $baselineManifest.integration.picture_source_pins.'scripts/patch-post-compat.py' `
        --candidate-pin $candidateManifest.integration.picture_source_pins.'scripts/patch-post-compat.py'
    if ($LASTEXITCODE) { throw 'PostLightStyle transformation provenance failed.' }
    $candidateHash = (Get-FileHash -LiteralPath (Join-Path $SourceRoot 'src/nr_before_sr.cpp') -Algorithm SHA256).Hash.ToLowerInvariant()
    $baselineHash = (Get-FileHash -LiteralPath $BaselineSource -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($candidateManifest.files.'core/src/nr_before_sr.cpp' -ne $candidateHash -or $baselineManifest.files.'core/src/nr_before_sr.cpp' -ne $baselineHash) { throw 'Generated post source does not match its actual source manifest.' }
    foreach ($text in @($source,$baseline)) {
        if (!$text.Contains('float3 storedProxy = scaled ? SampleProxyStoredV2') -or !$text.Contains('LoadPostArea(tid.xy)')) { throw 'Expected actual generated post sampling, not raw source.' }
    }
    $postLightTests = Get-Content -LiteralPath (Join-Path $projectRoot 'tests/post_light_style_gpu.inl') -Raw -Encoding UTF8
    $harness = $harness.Replace('// @COMPOSITION_CONTRACT_TESTS@', $postLightTests)
    $harness = $harness.Replace('// @COMPOSITION_CONTRACT_ENTRY@', 'if (argc == 2 && std::strcmp(argv[1], "--post-light-style") == 0) { ValidatePostLightStyle(gpu); return 0; }')
} elseif($LightStyle) {
    if (!$BaselineSource -or $CombinedLowGrid -or $CatmullOutput -or $ColorEditField -or $InputFilterOutput -or $EditFieldOutput -or $ComparisonOutput -or $NegativeControl -or $LayerNegativeControl) { throw 'LightStyle requires only its frozen BaselineSource.' }
    $lightTests = Get-Content -LiteralPath (Join-Path $projectRoot 'tests/light_style_gpu.inl') -Raw -Encoding UTF8
    $harness = $harness.Replace('// @COMPOSITION_CONTRACT_TESTS@', $lightTests)
    $harness = $harness.Replace('// @COMPOSITION_CONTRACT_ENTRY@', 'if (argc == 2 && std::strcmp(argv[1], "--light-style") == 0) { ValidateLightStyle(gpu); return 0; }')
} elseif($CombinedLowGrid) {
    if (!$BaselineSource -or $CatmullOutput -or $ColorEditField -or $InputFilterOutput -or
        $EditFieldOutput -or $ComparisonOutput -or $NegativeControl -or $LayerNegativeControl) {
        throw 'Combined low-grid contract requires only BaselineSource.'
    }
    $inputTests = Get-Content -LiteralPath (Join-Path $projectRoot 'tests/input_filter_gpu.inl') -Raw -Encoding UTF8
    $combinedTests = Get-Content -LiteralPath (Join-Path $projectRoot 'tests/combined_low_grid_gpu.inl') -Raw -Encoding UTF8
    $harness = $harness.Replace('// @COMPOSITION_CONTRACT_TESTS@', $inputTests + "`n" + $combinedTests)
    $harness = $harness.Replace('// @COMPOSITION_CONTRACT_ENTRY@', 'if (argc == 2 && std::strcmp(argv[1], "--combined-low-grid") == 0) { ValidateCombinedLowGrid(gpu); return 0; }')
} elseif($CatmullOutput) {
    $catTests=Get-Content (Join-Path $projectRoot 'tests/catmull_edit_gpu.inl') -Raw -Encoding UTF8
    $harness=$harness.Replace('// @COMPOSITION_CONTRACT_TESTS@',$catTests)
    $harness=$harness.Replace('// @COMPOSITION_CONTRACT_ENTRY@','if (argc==3 && std::strcmp(argv[1],"--catmull-contract")==0) { ExportCatmullContract(gpu,argv[2]); return 0; }')
} elseif ($ColorEditField) {
    if ($NegativeControl -or $LayerNegativeControl -or $ComparisonOutput -or $EditFieldOutput) { throw 'Run colour EditField independently.' }
    if (![string]::IsNullOrWhiteSpace($BaselineSource)) {
        $baselineHash = (Get-FileHash -LiteralPath $BaselineSource -Algorithm SHA256).Hash.ToLowerInvariant()
        $harness = '#define NR_EDIT_FIELD_BASELINE_SOURCE_SHA "' + $baselineHash + '"' + "`n" + $harness
    }
    $colorTests = Get-Content -LiteralPath (Join-Path $projectRoot 'tests/color_edit_field_gpu.inl') -Raw -Encoding UTF8
    $harness = $harness.Replace('// @COMPOSITION_CONTRACT_TESTS@', $colorTests)
    $harness = $harness.Replace('// @COMPOSITION_CONTRACT_ENTRY@', 'if (argc == 2 && std::strcmp(argv[1], "--color-edit-field") == 0) { ValidateColorEditField(gpu); return 0; }')
} elseif (![string]::IsNullOrWhiteSpace($InputFilterOutput)) {
    if ($NegativeControl -or $LayerNegativeControl -or $ComparisonOutput -or $EditFieldOutput) { throw 'Run input filter export independently.' }
    if (![string]::IsNullOrWhiteSpace($BaselineSource)) {
        $baselineHash = (Get-FileHash -LiteralPath $BaselineSource -Algorithm SHA256).Hash.ToLowerInvariant()
        $harness = '#define NR_INPUT_FILTER_BASELINE_SOURCE_SHA "' + $baselineHash + '"' + "`n" + $harness
    }
    $inputTests = Get-Content -LiteralPath (Join-Path $projectRoot 'tests/input_filter_gpu.inl') -Raw -Encoding UTF8
    $harness = $harness.Replace('// @COMPOSITION_CONTRACT_TESTS@', $inputTests)
    $harness = $harness.Replace('// @COMPOSITION_CONTRACT_ENTRY@', 'if (argc == 3 && std::strcmp(argv[1], "--input-filter-contract") == 0) { ExportInputFilterContract(gpu, argv[2]); return 0; }')
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
if($PostLightStyle) {
    & $binary '--post-light-style'
    if($LASTEXITCODE -ne 0){throw 'Post light style production GPU contract failed.'}
    return
} elseif($LightStyle) {
    & $binary '--light-style'
    if($LASTEXITCODE -ne 0){throw 'Light style production GPU contract failed.'}
    return
} elseif($CombinedLowGrid) {
    & $binary '--combined-low-grid'
    if($LASTEXITCODE -ne 0){throw 'Combined low-grid GPU contract failed.'}
    return
} elseif($CatmullOutput) {
    & $binary '--catmull-contract' ([IO.Path]::GetFullPath($CatmullOutput))
    if($LASTEXITCODE -ne 0){throw 'Catmull GPU contract failed; evidence retained.'}
    & python (Join-Path $projectRoot 'scripts/validate-catmull-edit.py') --capture $CatmullOutput
    if($LASTEXITCODE -ne 0){throw 'Independent Catmull validation failed; evidence retained.'}
    return
}
if ($ColorEditField) {
    & $binary '--color-edit-field'
    if ($LASTEXITCODE -ne 0) { throw 'Production colour EditField validation failed.' }
} elseif (![string]::IsNullOrWhiteSpace($InputFilterOutput)) {
    & $binary '--input-filter-contract' ([IO.Path]::GetFullPath($InputFilterOutput))
    if ($LASTEXITCODE -ne 0) { throw 'Production input filter export failed.' }
    & python (Join-Path $projectRoot 'scripts/validate-input-filter.py') --capture $InputFilterOutput --source (Join-Path $SourceRoot 'src/nr_before_sr.cpp')
    if ($LASTEXITCODE -ne 0) { throw 'Independent input filter contract validation failed.' }
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
