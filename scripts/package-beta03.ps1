param(
    [Parameter(Mandatory = $true)]
    [string]$ReleaseRoot,
    [string]$Rtx50Runtime,
    [string]$Rtx3040Runtime,
    [switch]$UpdateOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Version = 'beta0.3.8'
$BuildRoot = Join-Path $ProjectRoot 'build'
if (-not $UpdateOnly) {
    if ([string]::IsNullOrWhiteSpace($Rtx50Runtime) -or
        [string]::IsNullOrWhiteSpace($Rtx3040Runtime)) {
        throw 'Full packages require -Rtx50Runtime and -Rtx3040Runtime; use -UpdateOnly for existing installations.'
    }
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
}

$ReleasePath = [IO.Path]::GetFullPath($ReleaseRoot)
if (Test-Path -LiteralPath $ReleasePath) {
    throw "Release destination already exists; choose a new empty path: $ReleasePath"
}
New-Item -ItemType Directory -Path $ReleasePath | Out-Null

$ChineseAddon = Join-Path $BuildRoot `
    'DLSS5-AI渲染超分版-beta0.3.8-@野生的装机宅-Bilibili.addon64'
$EnglishAddon = Join-Path $BuildRoot `
    'DLSS5-AI-Rendering-Upscaling-beta0.3.8-@野生的装机宅-Bilibili.en.addon64'
$Bridge = Join-Path $BuildRoot 'nrchain_nvngx.dll'
$Config = Join-Path $BuildRoot 'nr_before_sr.ini'
$FeedbackScript = Join-Path $ProjectRoot 'scripts\collect-feedback.ps1'
$FeedbackLauncher = Join-Path $ProjectRoot 'scripts\Collect-Feedback.cmd'

$Packages = if ($UpdateOnly) {
    @(
        [pscustomobject]@{
            Name = "DLSS5-$Version-中文轻量更新包"
            Addon = $ChineseAddon
            Readme = Join-Path $ProjectRoot 'docs\UPDATE.zh-CN.txt'
            ReadmeName = '说明.txt'
        },
        [pscustomobject]@{
            Name = "DLSS5-$Version-English-Update"
            Addon = $EnglishAddon
            Readme = Join-Path $ProjectRoot 'docs\UPDATE.en.txt'
            ReadmeName = 'Instructions.txt'
        }
    )
} else { @(
    [pscustomobject]@{
        Name = "RTX50-DLSS5-AI渲染超分版-$Version-@野生的装机宅-Bilibili-完整包"
        Addon = $ChineseAddon
        Readme = Join-Path $ProjectRoot 'docs\INSTALL.zh-CN.txt'
        ReadmeName = '说明.txt'
        Runtime = $ResolvedRtx50Runtime
    },
    [pscustomobject]@{
        Name = "RTX50-DLSS5-AI-Rendering-Upscaling-$Version-@野生的装机宅-Bilibili-Full"
        Addon = $EnglishAddon
        Readme = Join-Path $ProjectRoot 'docs\INSTALL.en.txt'
        ReadmeName = 'Instructions.txt'
        Runtime = $ResolvedRtx50Runtime
    },
    [pscustomobject]@{
        Name = "RTX30-40-DLSS5-AI渲染超分版-$Version-ShortFuse-SF-v2-@野生的装机宅-Bilibili-完整包"
        Addon = $ChineseAddon
        Readme = Join-Path $ProjectRoot 'docs\INSTALL.zh-CN.txt'
        ReadmeName = '说明.txt'
        Runtime = $ResolvedRtx3040Runtime
    },
    [pscustomobject]@{
        Name = "RTX30-40-DLSS5-AI-Rendering-Upscaling-$Version-ShortFuse-SF-v2-@野生的装机宅-Bilibili-Full"
        Addon = $EnglishAddon
        Readme = Join-Path $ProjectRoot 'docs\INSTALL.en.txt'
        ReadmeName = 'Instructions.txt'
        Runtime = $ResolvedRtx3040Runtime
    }
) }

$Required = @(
    $ChineseAddon, $EnglishAddon, $FeedbackScript, $FeedbackLauncher
) + $Packages.Readme
if (-not $UpdateOnly) {
    $Required += @($Bridge, $Config, $ResolvedRtx50Runtime, $ResolvedRtx3040Runtime)
}
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
    if (-not $UpdateOnly) {
        Copy-Item -LiteralPath $Bridge -Destination $PackagePath
        Copy-Item -LiteralPath $Config -Destination $PackagePath
        Copy-Item -LiteralPath $Package.Runtime `
            -Destination (Join-Path $PackagePath 'nvngx_dlssnr.dll')
    }
    Copy-Item -LiteralPath $FeedbackScript -Destination $PackagePath
    Copy-Item -LiteralPath $FeedbackLauncher -Destination $PackagePath
    # UTF-8 BOM and Windows newlines keep the shipped plain-text instructions
    # readable in older Notepad versions as well as current Windows.
    $Instructions = [IO.File]::ReadAllText($Package.Readme)
    $Instructions = ($Instructions -replace "`r?`n", "`r`n")
    [IO.File]::WriteAllText((Join-Path $PackagePath $Package.ReadmeName),
        $Instructions, [Text.UTF8Encoding]::new($true))

    $ZipPath = Join-Path $ReleasePath ($Package.Name + '.zip')
    Compress-Archive -Path (Join-Path $PackagePath '*') `
        -DestinationPath $ZipPath -CompressionLevel Optimal
    $ZipPaths += $ZipPath
}

foreach ($ZipPath in $ZipPaths) {
    $Info = Get-Item -LiteralPath $ZipPath
    $Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $ZipPath).Hash
    Write-Host "Packaged: $($Info.FullName)"
    Write-Host "Bytes: $($Info.Length)"
    Write-Host "SHA256: $Hash"
}
