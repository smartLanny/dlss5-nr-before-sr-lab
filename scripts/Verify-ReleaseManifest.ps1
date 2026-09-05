param(
    [string]$PackagePath = $PSScriptRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ExpectedOwner = '@野生的装机宅 Bilibili'
$ExpectedOfficialReleaseUrl = 'https://space.bilibili.com/941799'
$Root = (Resolve-Path -LiteralPath $PackagePath).Path
$ManifestPath = Join-Path $Root 'RELEASE_MANIFEST.json'
$SignaturePath = Join-Path $Root 'RELEASE_MANIFEST.sig'
$PublicKeyPath = Join-Path $Root 'release-public-key.json'

foreach ($Required in @($ManifestPath, $SignaturePath, $PublicKeyPath)) {
    if (-not (Test-Path -LiteralPath $Required -PathType Leaf)) {
        throw "Required verification file is missing: $Required"
    }
}

$ManifestBytes = [IO.File]::ReadAllBytes($ManifestPath)
$Manifest = [Text.Encoding]::UTF8.GetString($ManifestBytes) |
    ConvertFrom-Json
$Signature = Get-Content -LiteralPath $SignaturePath -Raw |
    ConvertFrom-Json
$PublicKey = Get-Content -LiteralPath $PublicKeyPath -Raw |
    ConvertFrom-Json

if ($Manifest.schema -ne 'dlss5-release-manifest-v1' -or
    $Signature.schema -ne 'dlss5-release-signature-v1' -or
    $PublicKey.schema -ne 'dlss5-release-public-key-v1') {
    throw 'Unsupported release signature schema.'
}
if ($Manifest.author -ne $ExpectedOwner -or
    $PublicKey.owner -ne $ExpectedOwner -or
    $Manifest.officialReleaseUrl -ne $ExpectedOfficialReleaseUrl -or
    $PublicKey.officialReleaseUrl -ne $ExpectedOfficialReleaseUrl) {
    throw 'Manifest author or official release address does not match 野生的装机宅.'
}
if ($Manifest.keyId -ne $Signature.keyId -or
    $Manifest.keyId -ne $PublicKey.keyId) {
    throw 'Manifest, signature and public key use different identities.'
}

$ManifestHash = [Convert]::ToHexString(
    [Security.Cryptography.SHA256]::HashData($ManifestBytes))
if ($ManifestHash -ne $Signature.manifestSha256) {
    throw 'The release manifest was modified after it was signed.'
}

$PublicBytes = [Convert]::FromBase64String($PublicKey.publicKeySpkiBase64)
$PublicHash = [Convert]::ToHexString(
    [Security.Cryptography.SHA256]::HashData($PublicBytes))
if ($PublicHash -ne $PublicKey.publicKeySha256) {
    throw 'The bundled public key is corrupt.'
}
$Ecdsa = [Security.Cryptography.ECDsa]::Create()
try {
    $BytesRead = 0
    $Ecdsa.ImportSubjectPublicKeyInfo($PublicBytes, [ref]$BytesRead)
    if ($BytesRead -ne $PublicBytes.Length) {
        throw 'The bundled public key contains trailing data.'
    }
    $SignatureBytes = [Convert]::FromBase64String(
        $Signature.signatureBase64)
    if (-not $Ecdsa.VerifyData(
            $ManifestBytes, $SignatureBytes,
            [Security.Cryptography.HashAlgorithmName]::SHA256)) {
        throw 'INVALID release signature.'
    }
} finally {
    $Ecdsa.Dispose()
}

$RootPrefix = $Root.TrimEnd([IO.Path]::DirectorySeparatorChar) +
    [IO.Path]::DirectorySeparatorChar
foreach ($Record in @($Manifest.files)) {
    $RelativePath = [string]$Record.path
    if ([string]::IsNullOrWhiteSpace($RelativePath) -or
        [IO.Path]::IsPathRooted($RelativePath)) {
        throw "Unsafe manifest path: $RelativePath"
    }
    $Candidate = [IO.Path]::GetFullPath((Join-Path $Root $RelativePath))
    if (-not $Candidate.StartsWith(
            $RootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Manifest path escapes the package: $RelativePath"
    }
    if (-not (Test-Path -LiteralPath $Candidate -PathType Leaf)) {
        throw "Signed file is missing: $RelativePath"
    }
    $Info = Get-Item -LiteralPath $Candidate
    if ($Info.Length -ne [long]$Record.bytes) {
        throw "Signed file size mismatch: $RelativePath"
    }
    $ActualHash = (Get-FileHash -Algorithm SHA256 `
        -LiteralPath $Candidate).Hash
    if ($ActualHash -ne [string]$Record.sha256) {
        throw "Signed file hash mismatch: $RelativePath"
    }
}

Write-Host "VALID: $($Manifest.product) $($Manifest.version)"
Write-Host "Author: $ExpectedOwner"
Write-Host "Official release address: $ExpectedOfficialReleaseUrl"
Write-Host "Signing key: $($Manifest.keyId)"
