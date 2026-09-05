$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$files = @(
    'tools/install-assistant/MainWindow.xaml',
    'tools/install-assistant/package/README.txt',
    'tools/install-assistant/README.zh-CN.md',
    'docs/INSTALL_ASSISTANT_VALIDATION.md'
)
foreach ($relative in $files) {
    $text = Get-Content -LiteralPath (Join-Path $root $relative) -Raw
    if ($text -notmatch [regex]::Escape('beta0.4.2') -or $text -notmatch [regex]::Escape('beta0.3.8')) {
        throw "$relative does not name both current standard editions."
    }
}
foreach ($relative in @('tools/install-assistant/MainWindow.xaml', 'tools/install-assistant/package/README.txt')) {
    $text = Get-Content -LiteralPath (Join-Path $root $relative) -Raw
    if ($text -match '0\.4\.1-r3' -or $text -match '0\.3\.7-r3') {
        throw "$relative still recommends an obsolete r3 package."
    }
}
$validation = Get-Content -LiteralPath (Join-Path $root 'docs/INSTALL_ASSISTANT_VALIDATION.md') -Raw
foreach ($identity in @(
    'f4d0657ad2ddcb82b8af3b0edc6c67ecf9109934',
    '15909ef10914fcdf151ee193460451667fbf31dc',
    '0BA9CE2F792BBDA09C2E967315F9AC58272A8FB70D246C1BED5B4AA950C1B0BC',
    'CCD7F1D8CD95A8559E5A629D3206BF586777173AA2309722202EC1A0A0B2D1E2'
)) {
    if ($validation -notmatch [regex]::Escape($identity)) { throw "Validation identity missing: $identity" }
}
Write-Host 'PASS current installer recommendation labels and frozen payload identities.'
