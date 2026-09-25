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
try {
    $env:DLSS5_FFX_SDK_ROOT=$FfxSdkRoot
    $env:DLSS5_FFX_CACHE_ROOT=$FfxCacheRoot
$ReconstructionTests=Join-Path $OutputRoot 'reconstruction-checks'
# Keep pre-build evidence OUTSIDE the output that the canonical builder creates.
$ReconstructionPre=$OutputRoot + '.reconstruction-checks'
$Cxx=Join-Path $ToolchainRoot 'bin/x86_64-w64-mingw32-clang++.exe'
& python (Join-Path $PSScriptRoot 'validate-reconstruction-math.py') --compiler $Cxx --output (Join-Path $ReconstructionPre 'math')
if ($LASTEXITCODE) { throw 'Reconstruction production math/policy failed.' }
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
& python (Join-Path $PSScriptRoot 'validate-acceptance.py') --compiler $Cxx --source $Source --reshade $ReShadeSourceRoot --output (Join-Path $Tests 'acceptance')
if ($LASTEXITCODE) { throw 'Actual acceptance controller failed.' }
& python (Join-Path $PSScriptRoot 'validate-native-color-view.py') --acceptance --acceptance-negative --compiler $Cxx --source $Source --reshade $ReShadeSourceRoot --output (Join-Path $Tests 'acceptance-negative')
if ($LASTEXITCODE) { throw 'Wrong-restoration negative control failed.' }
& python (Join-Path $PSScriptRoot 'validate-temporal-routes.py') --compiler $Cxx --source $Source --reshade $ReShadeSourceRoot --codec-cache (Join-Path $Core 'build') --output (Join-Path $Tests 'temporal-routes')
if ($LASTEXITCODE) { throw 'Actual temporal route entries failed.' }
& python (Join-Path $PSScriptRoot 'validate-aux-shaders.py') --compiler $Cxx --core $Core --reshade $ReShadeSourceRoot --artifacts (Join-Path $Core 'build') --output (Join-Path $Tests 'auxiliary') --run-warp
if ($LASTEXITCODE) { throw 'Actual auxiliary precompiled shader oracle failed.' }
& python (Join-Path $PSScriptRoot 'validate-native-output-capture.py') --compiler $Cxx --source $Source --link-core $Core --reshade $ReShadeSourceRoot --output (Join-Path $Tests 'native-output') --run
if ($LASTEXITCODE) { throw 'Actual native output capture failed.' }
[ordered]@{passed=$true;source_sha256=(Get-FileHash -LiteralPath $Source).Hash.ToLowerInvariant();
    acceptance=$true;restoration_negative=$true;temporal_entries=$true;auxiliary=$true;native_output=$true;
    game=$false;nvidia_inference=$false;hosted_ci=$false
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Tests 'validation.json') -Encoding utf8
& python (Join-Path $PSScriptRoot '../tests/test_reconstruction_runtime.py') --generated $Core -v
if ($LASTEXITCODE) { throw 'Final reconstruction wiring failed.' }
& python (Join-Path $PSScriptRoot 'validate-reconstruction-gpu.py') --source-root $Core --compiler $Cxx --output (Join-Path $ReconstructionTests 'warp') --artifacts (Join-Path $Core 'build') --require-manifest
if ($LASTEXITCODE) { throw 'Exact reconstruction codec WARP or inverse control failed.' }
[ordered]@{passed=$true;source_sha256=(Get-FileHash -LiteralPath $Source).Hash.ToLowerInvariant();
    source_commit=(Get-Content (Join-Path $OutputRoot 'source/source-manifest.json') -Raw|ConvertFrom-Json).source_commit;
    math_receipt=(Join-Path $ReconstructionPre 'math/validation.json');
    warp_receipt=(Join-Path $ReconstructionTests 'warp/validation.json');
    runtime_controls=$true;defaults_unchanged=$true;game=$false;nvidia_inference=$false
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $ReconstructionTests 'validation.json') -Encoding utf8


} finally {
    $env:DLSS5_FFX_SDK_ROOT=$SavedFfxSdk
    $env:DLSS5_FFX_CACHE_ROOT=$SavedFfxCache
}
