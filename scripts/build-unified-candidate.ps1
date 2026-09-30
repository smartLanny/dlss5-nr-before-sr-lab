# One successor entry; the cumulative performance and canonical gates remain mandatory.
param(
    [Parameter(Mandatory=$true)][string]$UpstreamRoot,
    [Parameter(Mandatory=$true)][string]$ToolchainRoot,
    [Parameter(Mandatory=$true)][string]$ReShadeSourceRoot,
    [Parameter(Mandatory=$true)][string]$DirectXMathRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [Parameter(Mandatory=$true)][string]$FaceRuntimeRoot,
    [string]$NrchainReuseRoot,
    [string]$CodecCacheRoot,
    [string]$FfxSdkRoot = $env:DLSS5_FFX_SDK_ROOT,
    [string]$FfxCacheRoot = $env:DLSS5_FFX_CACHE_ROOT,
    [switch]$AllowMissingDebugLayer
)
$ErrorActionPreference='Stop'
# Fail before expensive validation when the mandatory temporal inputs are absent.
# The production module still verifies the exact SDK commit, clean tree, compiler,
# alignment receipt and all eight cached object hashes; these are path checks only.
if ([string]::IsNullOrWhiteSpace($FfxSdkRoot) -or [string]::IsNullOrWhiteSpace($FfxCacheRoot)) {
    throw 'Temporal backend inputs are required: supply -FfxSdkRoot and -FfxCacheRoot (or DLSS5_FFX_SDK_ROOT / DLSS5_FFX_CACHE_ROOT). Do not disable the backend to bypass validation.'
}
if (-not (Test-Path -LiteralPath (Join-Path $FfxSdkRoot 'LICENSE.txt') -PathType Leaf) -or
    -not (Test-Path -LiteralPath (Join-Path $FfxCacheRoot 'build.json') -PathType Leaf)) {
    throw 'Temporal SDK/cache paths must contain LICENSE.txt and the verified build.json receipt.'
}
$FfxSdkRoot=(Resolve-Path -LiteralPath $FfxSdkRoot).Path
$FfxCacheRoot=(Resolve-Path -LiteralPath $FfxCacheRoot).Path
$CandidateParameters=@{}
foreach($Name in $PSBoundParameters.Keys) {
    if($Name -notin @('FfxSdkRoot','FfxCacheRoot')) {$CandidateParameters[$Name]=$PSBoundParameters[$Name]}
}
$SavedFfxSdk=$env:DLSS5_FFX_SDK_ROOT
$SavedFfxCache=$env:DLSS5_FFX_CACHE_ROOT
& python (Join-Path $PSScriptRoot 'validate-diagnostic-metadata.py') --compiler (Join-Path $ToolchainRoot 'bin/x86_64-w64-mingw32-clang++.exe') --output ($OutputRoot + '.diagnostic-metadata-checks')
if ($LASTEXITCODE) { throw 'Diagnostic metadata numeric preservation and privacy gate failed.' }
try {
    $env:DLSS5_FFX_SDK_ROOT=$FfxSdkRoot
    $env:DLSS5_FFX_CACHE_ROOT=$FfxCacheRoot
$ReconstructionTests=Join-Path $OutputRoot 'reconstruction-checks'
# Keep pre-build evidence OUTSIDE the output that the canonical builder creates.
$ReconstructionPre=$OutputRoot + '.reconstruction-checks'
$Cxx=Join-Path $ToolchainRoot 'bin/x86_64-w64-mingw32-clang++.exe'
$GuideStorageProbe=$OutputRoot + '.guide-storage-contract.exe'
& $Cxx '-std=c++20' '-O0' '-Wall' '-Wextra' '-Werror' '-static' (Join-Path $PSScriptRoot '../tests/nr_guide_storage_contract_test.cpp') '-o' $GuideStorageProbe
if ($LASTEXITCODE) { throw 'NR guide storage contract CPU regression failed to compile.' }
& $GuideStorageProbe
if ($LASTEXITCODE) { throw 'NR guide storage/history Reset CPU regression failed.' }
$ResidencyPre=$OutputRoot + '.residency-checks'
& python (Join-Path $PSScriptRoot 'validate-residency-policy.py') --compiler $Cxx --output (Join-Path $ResidencyPre 'slots')
if ($LASTEXITCODE) { throw 'Actual slot selection and fence negative controls failed.' }
& python (Join-Path $PSScriptRoot 'validate-residency-drain.py') --compiler $Cxx --output (Join-Path $ResidencyPre 'drain')
if ($LASTEXITCODE) { throw 'Global compaction and inherited retirement proofs failed.' }
& python (Join-Path $PSScriptRoot 'validate-pipeline-cache.py') --compiler $Cxx --output (Join-Path $ResidencyPre 'pipeline-cache')
if ($LASTEXITCODE) { throw 'Pipeline cache provenance/files/WARP output checks failed.' }
$AmdSourceProbe=$OutputRoot + '.amd-source-continuity.exe'
& $Cxx '-std=c++20' '-O0' '-Wall' '-Wextra' '-Werror' '-static' (Join-Path $PSScriptRoot '../tests/amd_of_source_continuity_test.cpp') '-o' $AmdSourceProbe
if ($LASTEXITCODE) { throw 'AMD exact owned-source pairing test failed to compile.' }
& $AmdSourceProbe
if ($LASTEXITCODE) { throw 'AMD source-gap admission or conflicting-reference rejection failed.' }
& python (Join-Path $PSScriptRoot 'validate-private-call-list.py') --root (Join-Path $PSScriptRoot '..') --compiler $Cxx --output ($OutputRoot + '.private-call-list-checks')
if ($LASTEXITCODE) { throw 'Private NGX original-call interface regression failed.' }
& python (Join-Path $PSScriptRoot '../tests/test_product_identity.py') -v
if ($LASTEXITCODE) { throw 'Final product identity regression failed.' }
& python (Join-Path $PSScriptRoot 'validate-guide-domain.py') --compiler $Cxx --output ($OutputRoot + '.guide-domain-checks')
if ($LASTEXITCODE) { throw 'Guide domain, invalid scale or negative-control regression failed.' }
& python (Join-Path $PSScriptRoot 'validate-reconstruction-math.py') --compiler $Cxx --output (Join-Path $ReconstructionPre 'math')
if ($LASTEXITCODE) { throw 'Reconstruction production math/policy failed.' }
& python (Join-Path $PSScriptRoot 'validate-reconstruction-ui.py') --compiler $Cxx --output (Join-Path $ReconstructionPre 'ui')
if ($LASTEXITCODE) { throw 'Reconstruction bilingual UI controls failed.' }
& python (Join-Path $PSScriptRoot '../tests/test_reconstruction_runtime.py') -v
if ($LASTEXITCODE) { throw 'Reconstruction source orchestration failed.' }
& (Join-Path $PSScriptRoot 'build-perf12-candidate.ps1') @CandidateParameters -QualitySuccessor
if ($LASTEXITCODE) { throw 'Cumulative candidate validation failed.' }
# Complete source, bilingual binaries and inherited gates first. These tests
# then consume that exact generated Core, rather than private overlay probes.
$Cxx=Join-Path $ToolchainRoot 'bin/x86_64-w64-mingw32-clang++.exe'
$Core=Join-Path $OutputRoot 'source/core'
$Source=Join-Path $Core 'src/nr_before_sr.cpp'
$Tests=Join-Path $OutputRoot 'unified8-checks'
$TemporalGate=Join-Path $Tests 'temporal-budget-status'
& python (Join-Path $PSScriptRoot 'validate-temporal-budget.py') --compiler $Cxx --source (Join-Path $Core 'src/temporal') --status-source (Join-Path $Core 'src') --sdk $FfxSdkRoot --output $TemporalGate
if ($LASTEXITCODE) { throw "Staged temporal budget CPU, WARP allocation queries, or admission log limiter CPU failed; see $TemporalGate/validation.json." }
$TemporalQualityGate=Join-Path $Tests 'temporal-quality-sequence'
& python (Join-Path $PSScriptRoot 'validate-temporal-quality-sequence.py') --core $Core --compiler $Cxx --sdk $FfxSdkRoot --sdk-cache $FfxCacheRoot --output $TemporalQualityGate
if ($LASTEXITCODE) { throw "Generated temporal production shader sequence WARP failed; see $TemporalQualityGate/result.json." }
$PrivateQualityGate=Join-Path $Tests 'private-sr-quality-warp'
& python (Join-Path $PSScriptRoot '../experiments/nr-edit/tools/validate_private_sr_quality.py') --core $Core --compiler $Cxx --output $PrivateQualityGate --quality-dark
if ($LASTEXITCODE) { throw "Generated private SR controlled spatial/WARP quality attribution failed; see $PrivateQualityGate/validation.json." }
& python (Join-Path $PSScriptRoot 'validate-guide-domain.py') --compiler $Cxx --source $Source --output (Join-Path $Tests 'guide-domain')
if ($LASTEXITCODE) { throw 'Generated Core guide-domain regression failed.' }
& python (Join-Path $PSScriptRoot 'validate-acceptance.py') --compiler $Cxx --source $Source --reshade $ReShadeSourceRoot --codec-cache (Join-Path $Core 'build') --output (Join-Path $Tests 'acceptance')
if ($LASTEXITCODE) { throw 'Actual acceptance controller failed.' }
$CommitPressureGate=Join-Path $Tests 'commit-pressure-entry'
& python (Join-Path $PSScriptRoot 'validate-native-color-view.py') --acceptance --acceptance-commit-pressure --compiler $Cxx --source $Source --reshade $ReShadeSourceRoot --codec-cache (Join-Path $Core 'build') --output $CommitPressureGate
if ($LASTEXITCODE) { throw "Actual SR/RR entry commit-pressure safe bypass failed; see $CommitPressureGate/result.json." }
$CommitPressureReceipt=Get-Content -LiteralPath (Join-Path $CommitPressureGate 'result.json') -Raw | ConvertFrom-Json
if (-not $CommitPressureReceipt.commit_pressure_negative_control -or
    -not $CommitPressureReceipt.production_translation_unit -or
    $CommitPressureReceipt.run_exit -ne 0 -or
    $CommitPressureReceipt.cases.Count -ne 4 -or
    (($CommitPressureReceipt.cases | ForEach-Object { $_.case }) -join ',') -ne 'pressure-bounds,pressure-rr,pressure-sr,pressure-secondary' -or
    (@($CommitPressureReceipt.cases | Where-Object { $_.run_exit -ne 0 })).Count -ne 0) {
    throw 'Actual SR/RR entry commit-pressure receipt is incomplete or failed.'
}
& python (Join-Path $PSScriptRoot 'validate-native-color-view.py') --acceptance --acceptance-negative --compiler $Cxx --source $Source --reshade $ReShadeSourceRoot --codec-cache (Join-Path $Core 'build') --output (Join-Path $Tests 'acceptance-negative')
if ($LASTEXITCODE) { throw 'Wrong-restoration negative control failed.' }
& python (Join-Path $PSScriptRoot 'validate-temporal-routes.py') --compiler $Cxx --source $Source --reshade $ReShadeSourceRoot --codec-cache (Join-Path $Core 'build') --output (Join-Path $Tests 'temporal-routes')
if ($LASTEXITCODE) { throw 'Actual temporal route entries failed.' }
& python (Join-Path $PSScriptRoot 'validate-aux-shaders.py') --compiler $Cxx --core $Core --reshade $ReShadeSourceRoot --artifacts (Join-Path $Core 'build') --output (Join-Path $Tests 'auxiliary') --run-warp
if ($LASTEXITCODE) { throw 'Actual auxiliary precompiled shader oracle failed.' }
& python (Join-Path $PSScriptRoot 'validate-native-output-capture.py') --compiler $Cxx --source $Source --link-core $Core --reshade $ReShadeSourceRoot --output (Join-Path $Tests 'native-output') --run
if ($LASTEXITCODE) { throw 'Actual native output capture failed.' }
[ordered]@{passed=$true;source_sha256=(Get-FileHash -LiteralPath $Source).Hash.ToLowerInvariant();
    acceptance=$true;commit_pressure_entry=$true;restoration_negative=$true;temporal_entries=$true;auxiliary=$true;native_output=$true;
    commit_pressure_entry_receipt=(Join-Path $CommitPressureGate 'result.json');
    temporal_budget_status_receipt=(Join-Path $TemporalGate 'validation.json');
    temporal_quality_sequence_receipt=(Join-Path $TemporalQualityGate 'result.json');
    private_sr_quality_warp_receipt=(Join-Path $PrivateQualityGate 'validation.json');
    game=$false;nvidia_inference=$false;hosted_ci=$false
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Tests 'validation.json') -Encoding utf8
& python (Join-Path $PSScriptRoot '../tests/test_reconstruction_runtime.py') --generated $Core -v
if ($LASTEXITCODE) { throw 'Final reconstruction wiring failed.' }
& python (Join-Path $PSScriptRoot 'validate-reconstruction-gpu.py') --source-root $Core --compiler $Cxx --output (Join-Path $ReconstructionTests 'warp') --artifacts (Join-Path $Core 'build') --require-manifest
if ($LASTEXITCODE) { throw 'Exact reconstruction codec WARP or inverse control failed.' }
[ordered]@{passed=$true;source_sha256=(Get-FileHash -LiteralPath $Source).Hash.ToLowerInvariant();
    source_commit=(Get-Content (Join-Path $OutputRoot 'source/source-manifest.json') -Raw|ConvertFrom-Json).source_commit;
    math_receipt=(Join-Path $ReconstructionPre 'math/validation.json');
    ui_receipt=(Join-Path $ReconstructionPre 'ui/validation.json');
    warp_receipt=(Join-Path $ReconstructionTests 'warp/validation.json');
    runtime_controls=$true;defaults_unchanged=$true;user_facing_controls=$true;game=$false;nvidia_inference=$false
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $ReconstructionTests 'validation.json') -Encoding utf8


} finally {
    $env:DLSS5_FFX_SDK_ROOT=$SavedFfxSdk
    $env:DLSS5_FFX_CACHE_ROOT=$SavedFfxCache
}
