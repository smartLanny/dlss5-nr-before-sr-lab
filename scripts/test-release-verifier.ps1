$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Split-Path -Parent $PSScriptRoot
$testRoot = [IO.Path]::GetFullPath((Join-Path $root 'test-output\installer\verifier-tests'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
$caseRoot = Join-Path $testRoot ('untrusted-key-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $caseRoot | Out-Null

$key = [Security.Cryptography.ECDsa]::Create(
    [Security.Cryptography.ECCurve+NamedCurves]::nistP256)
try {
    $payloadPath = Join-Path $caseRoot 'payload.bin'
    [IO.File]::WriteAllBytes($payloadPath, [Text.Encoding]::UTF8.GetBytes('untrusted self-signed payload'))
    $publicBytes = $key.ExportSubjectPublicKeyInfo()
    $publicHash = [Convert]::ToHexString(
        [Security.Cryptography.SHA256]::HashData($publicBytes))
    $keyId = $publicHash.Substring(0, 16)
    $owner = '@野生的装机宅 Bilibili'
    $url = 'https://space.bilibili.com/941799'

    $publicDocument = [ordered]@{
        schema='dlss5-release-public-key-v1'
        algorithm='ECDSA-P256-SHA256'
        keyId=$keyId
        owner=$owner
        officialReleaseUrl=$url
        publicKeySha256=$publicHash
        publicKeySpkiBase64=[Convert]::ToBase64String($publicBytes)
    }
    [IO.File]::WriteAllText((Join-Path $caseRoot 'release-public-key.json'),
        ($publicDocument | ConvertTo-Json), [Text.UTF8Encoding]::new($false))

    $payloadHash = (Get-FileHash -LiteralPath $payloadPath -Algorithm SHA256).Hash
    $manifest = [ordered]@{
        schema='dlss5-release-manifest-v1'
        product='untrusted-test'
        version='test'
        channel='test'
        author=$owner
        officialReleaseUrl=$url
        keyId=$keyId
        generatedUtc=[DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
        files=@([ordered]@{path='payload.bin';bytes=(Get-Item -LiteralPath $payloadPath).Length;sha256=$payloadHash})
    }
    $manifestPath = Join-Path $caseRoot 'RELEASE_MANIFEST.json'
    [IO.File]::WriteAllText($manifestPath, (($manifest | ConvertTo-Json -Depth 6) + "`n"),
        [Text.UTF8Encoding]::new($false))
    $manifestBytes = [IO.File]::ReadAllBytes($manifestPath)
    $signatureDocument = [ordered]@{
        schema='dlss5-release-signature-v1'
        algorithm='ECDSA-P256-SHA256'
        keyId=$keyId
        manifest='RELEASE_MANIFEST.json'
        manifestSha256=[Convert]::ToHexString(
            [Security.Cryptography.SHA256]::HashData($manifestBytes))
        signatureBase64=[Convert]::ToBase64String($key.SignData(
            $manifestBytes, [Security.Cryptography.HashAlgorithmName]::SHA256))
    }
    [IO.File]::WriteAllText((Join-Path $caseRoot 'RELEASE_MANIFEST.sig'),
        ($signatureDocument | ConvertTo-Json), [Text.UTF8Encoding]::new($false))

    $rejected = $false
    try {
        & (Join-Path $PSScriptRoot 'Verify-ReleaseManifest.ps1') -PackagePath $caseRoot
    } catch {
        if ($_.Exception.Message -notlike '*fixed official DLSS5 release verification key*') {
            throw
        }
        $rejected = $true
    }
    if (-not $rejected) {
        throw 'The release verifier accepted an arbitrary self-signed public key.'
    }
    Write-Host 'PASS fixed release verifier rejects an otherwise self-consistent arbitrary signing key.'
} finally {
    $key.Dispose()
    $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    $resolvedCase = [IO.Path]::GetFullPath($caseRoot)
    if (-not $resolvedCase.StartsWith($resolvedTestRoot, [StringComparison]::OrdinalIgnoreCase) -or
        -not [IO.Path]::GetFileName($resolvedCase).StartsWith('untrusted-key-', [StringComparison]::Ordinal)) {
        throw 'Verifier test cleanup path escaped its bounded test directory.'
    }
    Remove-Item -LiteralPath $resolvedCase -Recurse -Force
}
