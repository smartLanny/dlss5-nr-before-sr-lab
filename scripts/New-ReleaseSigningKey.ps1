param(
    [string]$PrivateKeyPath = (Join-Path $env:LOCALAPPDATA `
        'DLSS5-NR-Pro\signing\release-signing-key.dpapi.json'),
    [string]$PublicKeyPath,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($PublicKeyPath)) {
    $PublicKeyPath = Join-Path $ProjectRoot 'signing\release-public-key.json'
}

$PrivateKeyPath = [IO.Path]::GetFullPath($PrivateKeyPath)
$PublicKeyPath = [IO.Path]::GetFullPath($PublicKeyPath)
if (-not $Force -and ((Test-Path -LiteralPath $PrivateKeyPath) -or
                      (Test-Path -LiteralPath $PublicKeyPath))) {
    throw 'A release key already exists. Refusing to replace the official identity without -Force.'
}

function Write-Utf8NoBomAtomic([string]$Path, [string]$Text) {
    $Directory = Split-Path -Parent $Path
    New-Item -ItemType Directory -Path $Directory -Force | Out-Null
    $Temporary = Join-Path $Directory ([IO.Path]::GetRandomFileName())
    try {
        [IO.File]::WriteAllText($Temporary, $Text, [Text.UTF8Encoding]::new($false))
        Move-Item -LiteralPath $Temporary -Destination $Path -Force
    } finally {
        if (Test-Path -LiteralPath $Temporary) {
            Remove-Item -LiteralPath $Temporary -Force
        }
    }
}

$Owner = '@野生的装机宅 Bilibili'
$OfficialReleaseUrl = 'https://space.bilibili.com/941799'
$Entropy = [Text.Encoding]::UTF8.GetBytes('DLSS5-NR-Pro-Release-Key-v1')
$Curve = [Security.Cryptography.ECCurve]::CreateFromFriendlyName('nistP256')
$Ecdsa = [Security.Cryptography.ECDsa]::Create($Curve)
$PrivateBytes = $null
try {
    $PrivateBytes = $Ecdsa.ExportPkcs8PrivateKey()
    $PublicBytes = $Ecdsa.ExportSubjectPublicKeyInfo()
    $PublicHash = [Security.Cryptography.SHA256]::HashData($PublicBytes)
    $Fingerprint = [Convert]::ToHexString($PublicHash)
    $ProtectedPrivate = [Security.Cryptography.ProtectedData]::Protect(
        $PrivateBytes, $Entropy,
        [Security.Cryptography.DataProtectionScope]::CurrentUser)

    $PrivateDocument = [ordered]@{
        schema = 'dlss5-release-private-key-v1'
        protection = 'Windows-DPAPI-CurrentUser'
        keyId = $Fingerprint.Substring(0, 16)
        publicKeySha256 = $Fingerprint
        protectedPkcs8Base64 = [Convert]::ToBase64String($ProtectedPrivate)
    }
    $PublicDocument = [ordered]@{
        schema = 'dlss5-release-public-key-v1'
        algorithm = 'ECDSA-P256-SHA256'
        keyId = $Fingerprint.Substring(0, 16)
        owner = $Owner
        officialReleaseUrl = $OfficialReleaseUrl
        publicKeySha256 = $Fingerprint
        publicKeySpkiBase64 = [Convert]::ToBase64String($PublicBytes)
    }

    Write-Utf8NoBomAtomic $PrivateKeyPath `
        (($PrivateDocument | ConvertTo-Json -Depth 4) + "`n")
    Write-Utf8NoBomAtomic $PublicKeyPath `
        (($PublicDocument | ConvertTo-Json -Depth 4) + "`n")

    Write-Host "Created protected release key: $PrivateKeyPath"
    Write-Host "Created public verification key: $PublicKeyPath"
    Write-Host "Public-key SHA256: $Fingerprint"
} finally {
    if ($PrivateBytes -ne $null) {
        [Array]::Clear($PrivateBytes, 0, $PrivateBytes.Length)
    }
    $Ecdsa.Dispose()
}
