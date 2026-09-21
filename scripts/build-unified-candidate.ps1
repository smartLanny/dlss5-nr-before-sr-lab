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
    [switch]$AllowMissingDebugLayer
)
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'build-perf12-candidate.ps1') @PSBoundParameters -QualitySuccessor
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
