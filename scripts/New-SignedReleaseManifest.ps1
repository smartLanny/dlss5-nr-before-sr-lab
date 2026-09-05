param(
    [Parameter(Mandatory = $true)]
    [string]$PackagePath,
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [string]$Channel = 'beta',
    [string]$PrivateKeyPath = (Join-Path $env:LOCALAPPDATA `
        'DLSS5-NR-Pro\signing\release-signing-key.dpapi.json'),
    [string]$PublicKeyPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($PublicKeyPath)) {
    $PublicKeyPath = Join-Path $ProjectRoot 'signing\release-public-key.json'
}
$Root = (Resolve-Path -LiteralPath $PackagePath).Path
$PrivateKeyPath = (Resolve-Path -LiteralPath $PrivateKeyPath).Path
$PublicKeyPath = (Resolve-Path -LiteralPath $PublicKeyPath).Path
$ManifestPath = Join-Path $Root 'RELEASE_MANIFEST.json'
$SignaturePath = Join-Path $Root 'RELEASE_MANIFEST.sig'
$Excluded = @('RELEASE_MANIFEST.json', 'RELEASE_MANIFEST.sig')
$Owner = '@野生的装机宅 Bilibili'
$OfficialReleaseUrl = 'https://space.bilibili.com/941799'

function Write-Utf8NoBom([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}

$PublicDocument = Get-Content -LiteralPath $PublicKeyPath -Raw |
    ConvertFrom-Json
$PrivateDocument = Get-Content -LiteralPath $PrivateKeyPath -Raw |
    ConvertFrom-Json
if ($PublicDocument.schema -ne 'dlss5-release-public-key-v1' -or
    $PrivateDocument.schema -ne 'dlss5-release-private-key-v1') {
    throw 'Unsupported release-key schema.'
}
if ($PublicDocument.officialReleaseUrl -ne $OfficialReleaseUrl -or
    $PublicDocument.owner -ne $Owner) {
    throw 'The public key is not bound to the expected author and official release address.'
}
if ($PublicDocument.publicKeySha256 -ne $PrivateDocument.publicKeySha256) {
    throw 'Public and private release-key identities do not match.'
}

$FileRecords = @(
    Get-ChildItem -LiteralPath $Root -File |
        Where-Object { $Excluded -notcontains $_.Name } |
        Sort-Object Name |
        ForEach-Object {
            [ordered]@{
                path = $_.Name
                bytes = $_.Length
                sha256 = (Get-FileHash -Algorithm SHA256 `
                    -LiteralPath $_.FullName).Hash
            }
        }
)
if ($FileRecords.Count -eq 0) {
    throw "No files are available to sign in $Root"
}

$Manifest = [ordered]@{
    schema = 'dlss5-release-manifest-v1'
    product = 'DLSS 5 AI Rendering Perfect Chain'
    version = $Version
    channel = $Channel
    author = $Owner
    officialReleaseUrl = $OfficialReleaseUrl
    keyId = $PublicDocument.keyId
    generatedUtc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    files = $FileRecords
}
$ManifestText = ($Manifest | ConvertTo-Json -Depth 8) + "`n"
Write-Utf8NoBom $ManifestPath $ManifestText
$ManifestBytes = [IO.File]::ReadAllBytes($ManifestPath)

$Entropy = [Text.Encoding]::UTF8.GetBytes('DLSS5-NR-Pro-Release-Key-v1')
$ProtectedBytes = [Convert]::FromBase64String(
    $PrivateDocument.protectedPkcs8Base64)
$PrivateBytes = [Security.Cryptography.ProtectedData]::Unprotect(
    $ProtectedBytes, $Entropy,
    [Security.Cryptography.DataProtectionScope]::CurrentUser)
$Ecdsa = [Security.Cryptography.ECDsa]::Create()
try {
    $BytesRead = 0
    $Ecdsa.ImportPkcs8PrivateKey($PrivateBytes, [ref]$BytesRead)
    if ($BytesRead -ne $PrivateBytes.Length) {
        throw 'The protected release key contains trailing data.'
    }
    $DerivedPublic = $Ecdsa.ExportSubjectPublicKeyInfo()
    $DerivedHash = [Convert]::ToHexString(
        [Security.Cryptography.SHA256]::HashData($DerivedPublic))
    if ($DerivedHash -ne $PublicDocument.publicKeySha256) {
        throw 'The unprotected signing key does not match the published verification key.'
    }

    $Signature = $Ecdsa.SignData(
        $ManifestBytes, [Security.Cryptography.HashAlgorithmName]::SHA256)
    $SignatureDocument = [ordered]@{
        schema = 'dlss5-release-signature-v1'
        algorithm = 'ECDSA-P256-SHA256'
        keyId = $PublicDocument.keyId
        manifest = 'RELEASE_MANIFEST.json'
        manifestSha256 = [Convert]::ToHexString(
            [Security.Cryptography.SHA256]::HashData($ManifestBytes))
        signatureBase64 = [Convert]::ToBase64String($Signature)
    }
    Write-Utf8NoBom $SignaturePath `
        (($SignatureDocument | ConvertTo-Json -Depth 4) + "`n")
    Write-Host "Signed release manifest: $ManifestPath"
    Write-Host "Signing key: $($PublicDocument.keyId)"
} finally {
    [Array]::Clear($PrivateBytes, 0, $PrivateBytes.Length)
    $Ecdsa.Dispose()
}
