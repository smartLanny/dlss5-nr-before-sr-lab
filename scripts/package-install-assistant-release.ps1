param(
    [Parameter(Mandatory=$true)][string]$PayloadCatalogPath,
    [Parameter(Mandatory=$true)][string]$LegacyCatalogPath,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [string]$Dotnet = 'C:\Users\pc1\Documents\ChatGPT\DLSS5\dlss5-lab\toolchains\dotnet10\dotnet.exe',
    [string]$PrivateKeyPath,
    [string]$PublicKeyPath
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
$payload = Get-Content -LiteralPath $PayloadCatalogPath -Raw -Encoding UTF8 | ConvertFrom-Json
$legacy = Get-Content -LiteralPath $LegacyCatalogPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($payload.schema -ne 'dlss5-installer-payloads/v2' -or $payload.payloadSourceCommit -notmatch '^[0-9a-fA-F]{40}$' -or [string]::IsNullOrWhiteSpace($payload.build) -or $payload.edition -notin @('enhanced-recommended','compatibility-fallback') -or @($payload.variants).Count -ne 4) { throw 'Explicit final standard payload catalog is invalid; experimental packages are not auto-installable.' }
if ($legacy.schema -ne 'dlss5-installer-legacy/v1') { throw 'Explicit known legacy catalog is required.' }
foreach ($record in @($legacy.aliases) + @($legacy.knownFiles)) {
    if ($record.sha256 -notmatch '^[0-9a-fA-F]{64}$' -or [IO.Path]::GetFileName($record.path) -cne $record.path -or $record.path.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) { throw 'Invalid known legacy identity.' }
}
foreach ($alias in $legacy.aliases) { if (-not $alias.path.EndsWith('.addon64', [StringComparison]::OrdinalIgnoreCase)) { throw 'Legacy aliases must be addon64 files.' } }
$sourceCommit = (& git -C $root rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify installer source commit.' }
if (@(& git -C $root status --porcelain).Count -ne 0) { throw 'Commit and freeze installer source before release packaging.' }
$output = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $output) { throw "Output already exists: $output" }
$variants = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
# Validate every source before creating any release output. Never select a sibling release by glob.
foreach ($variant in $payload.variants) {
    if ($variant.gpuFamily -notin @('RTX50','RTX30-40') -or $variant.language -notin @('zh-CN','en') -or -not $variants.Add($variant.gpuFamily + '/' + $variant.language)) { throw 'Invalid or duplicate GPU/language variant.' }
    $roles = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($file in $variant.files) {
        if ($file.role -notin @('addon','bridge','runtime','config','feedback-launcher','feedback-collector','crash-feedback-launcher','crash-feedback-collector') -or -not $roles.Add($file.role) -or -not [IO.Path]::IsPathFullyQualified($file.sourcePath) -or $file.sha256 -notmatch '^[0-9a-fA-F]{64}$' -or [string]::IsNullOrWhiteSpace([string]$file.source) -or ([string]$file.source).Length -gt 256) { throw 'Explicit source path/hash/role/source identity required.' }
        if ((Get-FileHash -LiteralPath $file.sourcePath -Algorithm SHA256).Hash -ne $file.sha256) { throw "Final payload hash mismatch: $($file.sourcePath)" }
        if ($file.role -eq 'addon') {
            $text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($file.sourcePath))
            if (-not $text.Contains([string]$payload.build) -or -not $text.Contains([string]$payload.payloadSourceCommit)) { throw 'Addon does not contain the declared final build/source identity.' }
        }
    }
    foreach ($role in @('addon','bridge','runtime','config')) { if (-not $roles.Contains($role)) { throw "Missing $role payload." } }
}
New-Item -ItemType Directory -Path $output | Out-Null
$app = Join-Path $output 'DLSS5InstallAssistant-win-x64'
& $Dotnet publish (Join-Path $root 'tools/install-assistant/Dlss5InstallAssistant.csproj') -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:DebugType=None -p:DebugSymbols=false -o $app
if ($LASTEXITCODE -ne 0) { throw 'WPF publish failed.' }
$helper = Join-Path $root 'test-output/installer/helper-publish'
& $Dotnet publish (Join-Path $root 'tools/install-assistant.Helper/Dlss5InstallAssistant.Helper.csproj') -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:DebugType=None -p:DebugSymbols=false -o $helper
if ($LASTEXITCODE -ne 0) { throw 'Helper publish failed.' }
Copy-Item -LiteralPath (Join-Path $helper 'DLSS5InstallAssistant.Helper.exe') -Destination $app
Copy-Item -LiteralPath (Join-Path $root 'tools/install-assistant/README.zh-CN.md') -Destination (Join-Path $app '说明.txt')
Copy-Item -LiteralPath (Join-Path $root 'signing/release-public-key.json') -Destination $app
function Sign-Directory([string]$Path,[string]$Version) {
    $arguments = @{ PackagePath=$Path; Version=$Version; Channel='installer' }
    if ($PrivateKeyPath) { $arguments.PrivateKeyPath=$PrivateKeyPath }
    if ($PublicKeyPath) { $arguments.PublicKeyPath=$PublicKeyPath }
    & (Join-Path $root 'scripts/New-SignedReleaseManifest.ps1') @arguments
}
$buildEvidence = [ordered]@{ schema='dlss5-installer-build-evidence/v1'; installerSourceCommit=$sourceCommit; payloadSourceCommit=$payload.payloadSourceCommit; payloadCatalogSha256=(Get-FileHash -LiteralPath $PayloadCatalogPath -Algorithm SHA256).Hash; legacyCatalogSha256=(Get-FileHash -LiteralPath $LegacyCatalogPath -Algorithm SHA256).Hash; builtUtc=[DateTime]::UtcNow.ToString('o'); artifacts=@() }
foreach ($file in Get-ChildItem -LiteralPath $app -File) { $buildEvidence.artifacts += [ordered]@{ path=$file.Name; sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash; bytes=$file.Length } }
[IO.File]::WriteAllText((Join-Path $app 'BUILD_EVIDENCE.json'), ($buildEvidence | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))
Sign-Directory $app ('installer-' + $sourceCommit.Substring(0,8))
Compress-Archive -LiteralPath (Get-ChildItem -LiteralPath $app -File | Select-Object -ExpandProperty FullName) -DestinationPath (Join-Path $output 'DLSS5InstallAssistant-win-x64.zip') -CompressionLevel Optimal
foreach ($variant in $payload.variants) {
    $name = 'offline-package-' + $variant.gpuFamily + '-' + $variant.language
    $directory = Join-Path $output $name; New-Item -ItemType Directory -Path $directory | Out-Null
    $files = @(); $addonHash = ''
    foreach ($file in $variant.files) {
        $leaf = [IO.Path]::GetFileName($file.sourcePath)
        Copy-Item -LiteralPath $file.sourcePath -Destination (Join-Path $directory $leaf)
        $actual = (Get-FileHash -LiteralPath (Join-Path $directory $leaf) -Algorithm SHA256).Hash
        if ($actual -ne $file.sha256) { throw 'Source changed during packaging.' }
        if ($file.role -eq 'addon') { $addonHash=$actual }
        $known = @(@($legacy.knownFiles) + @($legacy.aliases) | Where-Object { $_.path -ieq $leaf } | ForEach-Object { $_.sha256.ToUpperInvariant() } | Sort-Object -Unique)
        $files += [ordered]@{ path=$leaf; sha256=$actual; bytes=(Get-Item -LiteralPath (Join-Path $directory $leaf)).Length; role=$file.role; source=[string]$file.source; target='addon-directory'; required=($file.role -in @('addon','bridge','runtime','config')); allowedExistingSha256=$known }
    }
    $manifest = [ordered]@{ schema='dlss5-install-manifest/v4'; id='nr-before-sr'; build=$payload.build; edition=$payload.edition; payloadSourceCommit=$payload.payloadSourceCommit; artifactId=('sha256:'+$addonHash); gpuFamily=$variant.gpuFamily; language=$variant.language; nativeD3D12=$true; onlineAllowed=$false; files=$files; legacyFiles=@($legacy.aliases); compatibleProfiles=@(
        [ordered]@{ id='cyberpunk2077-d3d12'; steamAppIds=@('1091500'); executableSha256=@(); verifiedNativeD3D12=$true; evidence='Native D3D12 profile; exact deployed artifact requires current-process runtime v2 verification.' },
        [ordered]@{ id='witcher3-d3d12'; steamAppIds=@('292030'); executableSha256=@(); verifiedNativeD3D12=$true; evidence='Native D3D12 executable profile; exact deployed artifact requires current-process runtime v2 verification.' }
    ) }
    [IO.File]::WriteAllText((Join-Path $directory 'manifest.json'), ($manifest | ConvertTo-Json -Depth 12), [Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath (Join-Path $root 'tools/install-assistant/README.zh-CN.md') -Destination (Join-Path $directory '说明.txt')
    Copy-Item -LiteralPath (Join-Path $root 'signing/release-public-key.json') -Destination $directory
    Sign-Directory $directory $payload.build
    Compress-Archive -LiteralPath (Get-ChildItem -LiteralPath $directory -File | Select-Object -ExpandProperty FullName) -DestinationPath (Join-Path $output ($name+'.zip')) -CompressionLevel Optimal
}
[IO.File]::WriteAllText((Join-Path $output 'BUILD_EVIDENCE.json'), ($buildEvidence | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))
Write-Host "Created signed app and four explicit final payload packages: $output"
