# Orchestration-only test of the actual outer build entry, not Core/GPU validation.
param([Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $OutputRoot){throw 'Use a fresh test directory; preserve previous receipts.'}
New-Item -ItemType Directory -Path $OutputRoot | Out-Null
$Root=(Resolve-Path -LiteralPath $OutputRoot).Path
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../scripts/build-unified-candidate.ps1') -Destination (Join-Path $Root 'build-unified-candidate.ps1')
@'
param($UpstreamRoot,$ToolchainRoot,$ReShadeSourceRoot,$DirectXMathRoot,$OutputRoot,$FaceRuntimeRoot,$NrchainReuseRoot,$CodecCacheRoot,[switch]$AllowMissingDebugLayer,[switch]$QualitySuccessor)
$global:UnifiedFixtureChildCalls++
if($args.Count){throw 'Fixture: outer-only arguments leaked into child'}
if(-not $QualitySuccessor){throw 'Fixture: quality branch lost'}
if($env:DLSS5_FFX_SDK_ROOT -ne $global:UnifiedFixtureSdk -or $env:DLSS5_FFX_CACHE_ROOT -ne $global:UnifiedFixtureCache){throw 'Fixture: SDK environment mismatch'}
throw 'Fixture: intentional downstream failure after observing exact inputs'
'@ | Set-Content -LiteralPath (Join-Path $Root 'build-perf12-candidate.ps1') -Encoding UTF8
$Sdk=New-Item -ItemType Directory -Path (Join-Path $Root 'sdk')
$Cache=New-Item -ItemType Directory -Path (Join-Path $Root 'cache')
'Fixture license, never a real SDK' | Set-Content -LiteralPath (Join-Path $Sdk.FullName 'LICENSE.txt')
'{}' | Set-Content -LiteralPath (Join-Path $Cache.FullName 'build.json')
$global:UnifiedFixtureSdk=$Sdk.FullName;$global:UnifiedFixtureCache=$Cache.FullName
$global:UnifiedFixtureChildCalls=0
$Entry=Join-Path $Root 'build-unified-candidate.ps1'
$Args=@{UpstreamRoot=$Root;ToolchainRoot=$Root;ReShadeSourceRoot=$Root;DirectXMathRoot=$Root;OutputRoot=(Join-Path $Root 'unused');FaceRuntimeRoot=$Root}
$oldSdk=$env:DLSS5_FFX_SDK_ROOT;$oldCache=$env:DLSS5_FFX_CACHE_ROOT
$checks=0
try {
    $env:DLSS5_FFX_SDK_ROOT=$null;$env:DLSS5_FFX_CACHE_ROOT=$null
    try { & $Entry @Args;throw 'Fixture: missing inputs were accepted' } catch {
        if($_.Exception.Message -notlike 'Temporal backend inputs are required:*'){throw}
        if($global:UnifiedFixtureChildCalls -ne 0){throw 'Missing inputs reached expensive child'};$checks++
    }
    try { & $Entry @Args -FfxSdkRoot $Root -FfxCacheRoot $Root;throw 'Fixture: invalid paths were accepted' } catch {
        if($_.Exception.Message -notlike 'Temporal SDK/cache paths must contain*'){throw}
        if($global:UnifiedFixtureChildCalls -ne 0){throw 'Invalid paths reached expensive child'};$checks++
    }
    $env:DLSS5_FFX_SDK_ROOT='caller-sdk';$env:DLSS5_FFX_CACHE_ROOT='caller-cache'
    try { & $Entry @Args -FfxSdkRoot $Sdk.FullName -FfxCacheRoot $Cache.FullName;throw 'Fixture: child failure ignored' } catch {
        if($_.Exception.Message -ne 'Fixture: intentional downstream failure after observing exact inputs'){throw}
        if($global:UnifiedFixtureChildCalls -ne 1){throw 'Child was not called exactly once'}
        if($env:DLSS5_FFX_SDK_ROOT -ne 'caller-sdk' -or $env:DLSS5_FFX_CACHE_ROOT -ne 'caller-cache'){throw 'Caller environment not restored'};$checks++
    }
    [ordered]@{passed=$true;checks=$checks;scope='outer-build orchestration only; synthetic child; no SDK/Core/GPU build performed'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Root 'validation.json') -Encoding UTF8
    Write-Output "PASS $checks actual-entry fail-fast/argument/environment cases; synthetic child only"
} finally {
    $env:DLSS5_FFX_SDK_ROOT=$oldSdk;$env:DLSS5_FFX_CACHE_ROOT=$oldCache
    Remove-Variable -Scope Global -Name UnifiedFixtureSdk,UnifiedFixtureCache,UnifiedFixtureChildCalls -ErrorAction SilentlyContinue
}
