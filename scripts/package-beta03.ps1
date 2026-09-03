param(
    [Parameter(Mandatory = $true)]
    [string]$ReleaseRoot,
    [Parameter(Mandatory = $true)]
    [string]$Rtx50Runtime,
    [Parameter(Mandatory = $true)]
    [string]$Rtx3040Runtime,
    [string]$SigningKeyPath = (Join-Path $env:LOCALAPPDATA `
        'DLSS5-NR-Pro\signing\release-signing-key.dpapi.json'),
    [string]$PublicKeyPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Version = 'beta0.3.3-dev'
$OfficialReleaseUrl = 'https://space.bilibili.com/941799'
$ManifestSigner = Join-Path $PSScriptRoot 'New-SignedReleaseManifest.ps1'
$ManifestVerifier = Join-Path $PSScriptRoot 'Verify-ReleaseManifest.ps1'
if ([string]::IsNullOrWhiteSpace($PublicKeyPath)) {
    $PublicKeyPath = Join-Path $ProjectRoot 'signing\release-public-key.json'
}
$BuildRoot = Join-Path $ProjectRoot 'build'
$ResolvedRtx50Runtime = (Resolve-Path -LiteralPath $Rtx50Runtime).Path
$ResolvedRtx3040Runtime = (Resolve-Path -LiteralPath $Rtx3040Runtime).Path
$RuntimeSignature = Get-AuthenticodeSignature -LiteralPath $ResolvedRtx50Runtime
if ($RuntimeSignature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
    throw "RTX 50 runtime is not Authenticode-valid: $ResolvedRtx50Runtime"
}
$Rtx50RuntimeHash = (Get-FileHash -Algorithm SHA256 `
    -LiteralPath $ResolvedRtx50Runtime).Hash
$ExpectedRtx3040RuntimeHash = `
    '6EB209E764F39872625DEBD6ABAF45E2BB6322F6F270F781F70C059AE30B3927'
$Rtx3040RuntimeHash = (Get-FileHash -Algorithm SHA256 `
    -LiteralPath $ResolvedRtx3040Runtime).Hash
if ($Rtx3040RuntimeHash -ne $ExpectedRtx3040RuntimeHash) {
    throw "Unexpected RTX 30/40 SF-v2 runtime hash: $Rtx3040RuntimeHash"
}
$Rtx3040Version = (Get-Item -LiteralPath $ResolvedRtx3040Runtime).VersionInfo.FileVersion
if ($Rtx3040Version -ne '310.8.SF.0') {
    throw "Unexpected RTX 30/40 runtime version: $Rtx3040Version"
}

$ReleasePath = [IO.Path]::GetFullPath($ReleaseRoot)
if (Test-Path -LiteralPath $ReleasePath) {
    throw "Release destination already exists; choose a new empty path: $ReleasePath"
}
New-Item -ItemType Directory -Path $ReleasePath | Out-Null

$ChineseAddon = Join-Path $BuildRoot `
    'DLSS5-AI渲染超分版-beta0.3.3-dev-@野生的装机宅-Bilibili.addon64'
$EnglishAddon = Join-Path $BuildRoot `
    'DLSS5-AI-Rendering-Upscaling-beta0.3.3-dev-@野生的装机宅-Bilibili.en.addon64'
$Bridge = Join-Path $BuildRoot 'nrchain_nvngx.dll'
$Config = Join-Path $BuildRoot 'nr_before_sr.ini'
$ReleaseNotes = Join-Path $ProjectRoot 'docs\BETA_0_3_3_RELEASE_NOTES.md'
$Limitations = Join-Path $ProjectRoot 'docs\KNOWN_LIMITATIONS.md'
$Rtx40Notes = Join-Path $ProjectRoot 'docs\RTX40_COMMUNITY_TEST.md'
$AgentNotice = Join-Path $ProjectRoot 'AI_AGENT_NOTICE.md'

$Packages = @(
    [pscustomobject]@{
        Name = 'DLSS5-AI渲染超分版-beta0.3.3-dev-RTX50-@野生的装机宅-Bilibili-完整包'
        Addon = $ChineseAddon
        Readme = Join-Path $ProjectRoot 'docs\INSTALL.zh-CN.md'
        Runtime = $ResolvedRtx50Runtime
        RuntimeHash = $Rtx50RuntimeHash
        Target = 'RTX 50; NVIDIA signed 310.8 runtime included'
        Compat = $false
    },
    [pscustomobject]@{
        Name = 'DLSS5-AI-Rendering-Upscaling-beta0.3.3-dev-RTX50-@野生的装机宅-Bilibili-Full'
        Addon = $EnglishAddon
        Readme = Join-Path $ProjectRoot 'docs\INSTALL.en.md'
        Runtime = $ResolvedRtx50Runtime
        RuntimeHash = $Rtx50RuntimeHash
        Target = 'RTX 50; NVIDIA signed 310.8 runtime included'
        Compat = $false
    },
    [pscustomobject]@{
        Name = 'DLSS5-AI渲染超分版-beta0.3.3-dev-RTX30-40-ShortFuse-SF-v2-@野生的装机宅-Bilibili-完整包'
        Addon = $ChineseAddon
        Readme = Join-Path $ProjectRoot 'docs\INSTALL.zh-CN.md'
        Runtime = $ResolvedRtx3040Runtime
        RuntimeHash = $Rtx3040RuntimeHash
        Target = 'RTX 30/40; ShortFuse 310.8.SF-v2 runtime included'
        Compat = $true
    },
    [pscustomobject]@{
        Name = 'DLSS5-AI-Rendering-Upscaling-beta0.3.3-dev-RTX30-40-ShortFuse-SF-v2-@野生的装机宅-Bilibili-Full'
        Addon = $EnglishAddon
        Readme = Join-Path $ProjectRoot 'docs\INSTALL.en.md'
        Runtime = $ResolvedRtx3040Runtime
        RuntimeHash = $Rtx3040RuntimeHash
        Target = 'RTX 30/40; ShortFuse 310.8.SF-v2 runtime included'
        Compat = $true
    }
)

$Required = @(
    $ChineseAddon, $EnglishAddon, $Bridge, $Config, $ReleaseNotes,
    $Limitations, $Rtx40Notes, $AgentNotice, $ManifestSigner, $ManifestVerifier,
    $SigningKeyPath, $PublicKeyPath, $ResolvedRtx50Runtime,
    $ResolvedRtx3040Runtime
) + $Packages.Readme
foreach ($Path in $Required | Select-Object -Unique) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Required package input is missing: $Path"
    }
}

$ZipPaths = @()
foreach ($Package in $Packages) {
    $PackagePath = Join-Path $ReleasePath $Package.Name
    New-Item -ItemType Directory -Path $PackagePath | Out-Null

    Copy-Item -LiteralPath $Package.Addon -Destination $PackagePath
    Copy-Item -LiteralPath $Bridge -Destination $PackagePath
    Copy-Item -LiteralPath $Config -Destination $PackagePath
    Copy-Item -LiteralPath $Package.Readme `
        -Destination (Join-Path $PackagePath 'README.md')
    Copy-Item -LiteralPath $ReleaseNotes `
        -Destination (Join-Path $PackagePath 'RELEASE_NOTES.md')
    Copy-Item -LiteralPath $Limitations `
        -Destination (Join-Path $PackagePath 'KNOWN_LIMITATIONS.md')
    Copy-Item -LiteralPath $AgentNotice `
        -Destination (Join-Path $PackagePath 'AI_AGENT_NOTICE.md')
    Copy-Item -LiteralPath $ManifestVerifier `
        -Destination (Join-Path $PackagePath 'Verify-ReleaseManifest.ps1')
    Copy-Item -LiteralPath $PublicKeyPath `
        -Destination (Join-Path $PackagePath 'release-public-key.json')
    if ($Package.Compat) {
        Copy-Item -LiteralPath $Rtx40Notes `
            -Destination (Join-Path $PackagePath 'RTX30_40_SHORTFUSE.md')
    }
    Copy-Item -LiteralPath $Package.Runtime `
        -Destination (Join-Path $PackagePath 'nvngx_dlssnr.dll')

    $ManifestLines = @(
        "DLSS 5 AI Rendering Upscaling Edition $Version",
        'Author: @野生的装机宅 Bilibili',
        "Only official release address: $OfficialReleaseUrl",
        'Authenticity: run .\Verify-ReleaseManifest.ps1 after extraction.',
        'Status: beta0.3.3 tuning/packed-color development candidate; wider native game gates remain pending.',
        "Target: $($Package.Target)",
        "Runtime SHA-256: $($Package.RuntimeHash)",
        ''
    )
    $ManifestLines | Set-Content -LiteralPath `
        (Join-Path $PackagePath 'PACKAGE_INFO.txt') -Encoding utf8NoBOM

    $HashLines = Get-ChildItem -LiteralPath $PackagePath -File |
        Where-Object Name -ne 'SHA256SUMS.txt' |
        Sort-Object Name |
        ForEach-Object {
            $Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash
            "$Hash  $($_.Name)"
        }
    $HashLines | Set-Content -LiteralPath `
        (Join-Path $PackagePath 'SHA256SUMS.txt') -Encoding utf8NoBOM

    & $ManifestSigner -PackagePath $PackagePath -Version $Version `
        -Channel 'beta' -PrivateKeyPath $SigningKeyPath `
        -PublicKeyPath $PublicKeyPath

    $ZipPath = Join-Path $ReleasePath ($Package.Name + '.zip')
    Compress-Archive -Path (Join-Path $PackagePath '*') `
        -DestinationPath $ZipPath -CompressionLevel Optimal
    $ZipPaths += $ZipPath
}

$TopLevelHashes = foreach ($ZipPath in $ZipPaths) {
    $Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $ZipPath).Hash
    "$Hash  $([IO.Path]::GetFileName($ZipPath))"
}
$TopLevelHashes | Set-Content -LiteralPath `
    (Join-Path $ReleasePath 'SHA256SUMS.txt') -Encoding utf8NoBOM
Copy-Item -LiteralPath $ManifestVerifier `
    -Destination (Join-Path $ReleasePath 'Verify-ReleaseManifest.ps1')
Copy-Item -LiteralPath $PublicKeyPath `
    -Destination (Join-Path $ReleasePath 'release-public-key.json')
& $ManifestSigner -PackagePath $ReleasePath -Version $Version `
    -Channel 'beta-index' -PrivateKeyPath $SigningKeyPath `
    -PublicKeyPath $PublicKeyPath

foreach ($ZipPath in $ZipPaths) {
    $Info = Get-Item -LiteralPath $ZipPath
    $Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $ZipPath).Hash
    Write-Host "Packaged: $($Info.FullName)"
    Write-Host "Bytes: $($Info.Length)"
    Write-Host "SHA256: $Hash"
}
