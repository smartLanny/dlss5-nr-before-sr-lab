param(
    [Parameter(Mandatory=$true)][string]$BuildRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Repo = Split-Path -Parent $PSScriptRoot
$Build = (Resolve-Path -LiteralPath $BuildRoot).Path
$Stage = Join-Path $Build 'source'
$CoreBuild = Join-Path $Stage 'core/build'
$Output = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $Output) { throw 'OutputRoot already exists; no package is overwritten.' }
$Manifest = Get-Content -LiteralPath (Join-Path $Stage 'source-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$Validation = Get-Content -LiteralPath (Join-Path $Build 'validation.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$Commit = [string]$Manifest.source_commit
$Version = [string]$Manifest.integration.version
if ($Commit -notmatch '^[0-9a-f]{40}$' -or $Validation.source_commit -ne $Commit -or $Manifest.integration.source_commit -ne $Commit) {
    throw 'Build evidence and exact staged source identity disagree.'
}
if ([string]::IsNullOrWhiteSpace($Version)) { throw 'Candidate version is missing.' }
$RepoCommit = (& git -C $Repo rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $RepoCommit -ne $Commit) { throw 'Package scripts and documentation must use the built source commit.' }
$DocChanges = @(& git -C $Repo status --porcelain -- docs/interop/POST_COMPAT_DEV4.md docs/interop/RELEASE_DEV4_PLAYER_NOTES.md THIRD_PARTY_NOTICES.txt third_party/minhook/LICENSE.txt tests/post_scaled_work_gpu.cpp.in)
if ($LASTEXITCODE -ne 0 -or $DocChanges.Count -ne 0) { throw 'Package documentation/licenses or WARP template changed since the built commit.' }
if (-not $Validation.source_verified -or -not $Validation.compile_verified -or -not $Validation.warp_verified) {
    throw 'Complete source/compile/WARP evidence is required before packaging.'
}
if (@($Validation.checks | Where-Object { $_ -like 'post scaled-work WARP:*' }).Count -ne 1) {
    throw 'Actual post-scaled-work WARP evidence is missing; baseline WARP alone is not sufficient.'
}
function Safe-Child([string]$Parent, [string]$Relative) {
    if ([string]::IsNullOrWhiteSpace($Relative) -or [IO.Path]::IsPathRooted($Relative) -or $Relative -match '(^|[/\\])\.\.([/\\]|$)') {
        throw "Unsafe manifest path: $Relative"
    }
    $Path = [IO.Path]::GetFullPath((Join-Path $Parent $Relative))
    if (-not $Path.StartsWith($Parent.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Manifest path escaped its source root: $Relative"
    }
    return $Path
}
function Assert-Hash([string]$Path, [string]$Expected) {
    if ($Expected -notmatch '^[0-9a-fA-F]{64}$' -or (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $Expected) {
        throw "SHA256 mismatch: $Path"
    }
}
foreach ($Entry in $Manifest.files.PSObject.Properties) {
    Assert-Hash (Safe-Child $Stage $Entry.Name) ([string]$Entry.Value)
}
Assert-Hash (Join-Path $CoreBuild 'nr_before_sr.ini') ([string]$Manifest.files.'core/config/nr_before_sr.ini')
$CarrierLeaf = [string]$Manifest.integration.carrier_filename
if ([IO.Path]::GetFileName($CarrierLeaf) -ne $CarrierLeaf -or $CarrierLeaf -notmatch '^dlss5-native-carrier-[a-z0-9-]+\.addon64$') {
    throw 'Invalid carrier filename in manifest.'
}
$Addons = @(Get-ChildItem -LiteralPath $CoreBuild -Filter '*.addon64' | Sort-Object Name)
if ($Addons.Count -ne 2) { throw 'Exactly two separate language cores are required.' }
$ExpectedBinaries = @($Addons.FullName) + @((Join-Path $CoreBuild 'nrchain_nvngx.dll'), (Join-Path $Build $CarrierLeaf))
if (@($Validation.binaries).Count -ne $ExpectedBinaries.Count) { throw 'Unexpected binary evidence set.' }
foreach ($Binary in $ExpectedBinaries) {
    $Name = [IO.Path]::GetFileName($Binary)
    $Evidence = @($Validation.binaries | Where-Object { $_.file -eq $Name })
    if ($Evidence.Count -ne 1) { throw "Missing/duplicated binary evidence: $Name" }
    Assert-Hash $Binary ([string]$Evidence[0].sha256)
}
$PostLog = Join-Path $Build 'tests/post-scaled-work/stdout.txt'
if (-not (Test-Path -LiteralPath $PostLog) -or (Get-Content -LiteralPath $PostLog -Raw -Encoding UTF8) -notmatch 'PASS post-scaled-work WARP: 48 scenarios') {
    throw 'Successful 48-scenario post WARP log is missing.'
}
$PostResultPath = Join-Path $Build 'tests/post-scaled-work/post-scaled-work-result.json'
$PostResult = Get-Content -LiteralPath $PostResultPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($PostResult.exit_code -ne 0 -or $PostResult.warp_verified -ne $true -or
    $PostResult.nvidia_feature18_verified -ne $false -or $PostResult.game_runtime_verified -ne $false) {
    throw 'Post WARP result must report a successful test-only run with explicit game/Feature18 limits.'
}
Assert-Hash (Join-Path $Stage 'core/src/nr_before_sr.cpp') ([string]$PostResult.staged_core_sha256)
Assert-Hash (Join-Path $Build 'tests/post-scaled-work/post_scaled_work_gpu.cpp') ([string]$PostResult.generated_harness_sha256)
Assert-Hash (Join-Path $Repo 'tests/post_scaled_work_gpu.cpp.in') ([string]$PostResult.template_sha256)
Assert-Hash $PostLog ([string]$PostResult.stdout_sha256)
$Parent = Split-Path -Parent $Output
[IO.Directory]::CreateDirectory($Parent) | Out-Null
$Temporary = Join-Path $Parent ('.nr-candidate-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($Temporary) | Out-Null
try {
    $Packages = New-Object 'System.Collections.Generic.List[object]'
    foreach ($Addon in $Addons) {
        $Package = Join-Path $Temporary $Addon.BaseName
        New-Item -ItemType Directory -Path (Join-Path $Package 'validation') -Force | Out-Null
        Copy-Item -LiteralPath $Addon.FullName -Destination $Package
        Copy-Item -LiteralPath (Join-Path $CoreBuild 'nrchain_nvngx.dll') -Destination $Package
        Copy-Item -LiteralPath (Join-Path $Build $CarrierLeaf) -Destination $Package
        Copy-Item -LiteralPath (Join-Path $Stage 'source-manifest.json') -Destination $Package
        Copy-Item -LiteralPath (Join-Path $Build 'validation.json') -Destination $Package
        Copy-Item -LiteralPath $PostLog -Destination (Join-Path $Package 'validation/post-scaled-work-stdout.txt')
        Copy-Item -LiteralPath $PostResultPath -Destination (Join-Path $Package 'validation/post-scaled-work-result.json')
        Copy-Item -LiteralPath (Join-Path $Build 'tests/post-scaled-work/stderr.txt') -Destination (Join-Path $Package 'validation/post-scaled-work-stderr.txt')
        Copy-Item -LiteralPath (Join-Path $Stage 'carrier/LICENSE') -Destination (Join-Path $Package 'LICENSE-carrier.txt')
        Copy-Item -LiteralPath (Join-Path $Repo 'third_party/minhook/LICENSE.txt') -Destination (Join-Path $Package 'LICENSE-minhook.txt')
        Copy-Item -LiteralPath (Join-Path $Repo 'THIRD_PARTY_NOTICES.txt') -Destination $Package
        Copy-Item -LiteralPath (Join-Path $Repo 'docs/interop/POST_COMPAT_DEV4.md') -Destination (Join-Path $Package 'README.md')
        Copy-Item -LiteralPath (Join-Path $Repo 'docs/interop/RELEASE_DEV4_PLAYER_NOTES.md') -Destination (Join-Path $Package 'UPDATE-NOTES.md')
        $Install = @"
COMPATIBILITY CANDIDATE - NOT A STABLE RELEASE
Version: $Version
Exact source: $Commit
Install only ONE language core and its matching $CarrierLeaf companion.
Do not stack generic Feeder/RenoDX NR owners or an older lab carrier.
Keep your existing nr_before_sr.ini. This OTA does not contain a replacement INI.
This package does not install ReShade or NVIDIA/NGX/model/game binaries.
F8 still creates one named TXT for BUG feedback; use F7 separately for pictures.
The distribution ZIP is not the F8 feedback format.
WARP uses a deterministic test-only neural residual, not NVIDIA Feature18.
Read validation.json for actual verification. This is not proof of game compatibility.
No game files, tags or Releases were modified by the packager.
"@
        [IO.File]::WriteAllText((Join-Path $Package 'INSTALL-CANDIDATE.txt'), $Install, (New-Object Text.UTF8Encoding($false)))
        $Hashes = @(Get-ChildItem -LiteralPath $Package -File -Recurse | Sort-Object FullName | ForEach-Object {
            [ordered]@{file=$_.FullName.Substring($Package.Length + 1).Replace('\','/'); sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
        })
        [IO.File]::WriteAllText((Join-Path $Package 'SHA256.json'), ($Hashes | ConvertTo-Json -Depth 4), (New-Object Text.UTF8Encoding($false)))
        $ZipLeaf = $Addon.BaseName + '-OTA.zip'
        $Zip = Join-Path $Temporary $ZipLeaf
        Compress-Archive -Path (Join-Path $Package '*') -DestinationPath $Zip
        $Packages.Add([ordered]@{file=$ZipLeaf; sha256=(Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash.ToLowerInvariant()})
    }
    $Index = [ordered]@{source_commit=$Commit; version=$Version; stable_release=$false; nvidia_feature18_verified=$Validation.nvidia_feature18_verified; game_runtime_verified=$Validation.game_runtime_verified; packages=@($Packages.ToArray())}
    [IO.File]::WriteAllText((Join-Path $Temporary 'packages.json'), ($Index | ConvertTo-Json -Depth 5), (New-Object Text.UTF8Encoding($false)))
    [IO.Directory]::Move($Temporary, $Output)
    Write-Host "Packaged exact-source language candidates without touching games or existing INI: $Output"
} finally {
    if (Test-Path -LiteralPath $Temporary) {
        $ResolvedTemporary = (Resolve-Path -LiteralPath $Temporary).Path
        if ([IO.Path]::GetDirectoryName($ResolvedTemporary) -ne $Parent -or
            [IO.Path]::GetFileName($ResolvedTemporary) -notmatch '^\.nr-candidate-[0-9a-f]{32}$') {
            throw 'Refusing cleanup outside the exact temporary package directory.'
        }
        Remove-Item -LiteralPath $ResolvedTemporary -Recurse -Force
    }
}
