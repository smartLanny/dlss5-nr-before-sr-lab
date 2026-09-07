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
$DisplayProperty = $Manifest.integration.PSObject.Properties['display_version']
$DisplayVersion = if ($null -ne $DisplayProperty) { [string]$DisplayProperty.Value } else { $Version }
if ([string]::IsNullOrWhiteSpace($DisplayVersion) -or $DisplayVersion.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) {
    throw 'Invalid public candidate name.'
}
if ($Commit -notmatch '^[0-9a-f]{40}$' -or $Validation.source_commit -ne $Commit -or $Manifest.integration.source_commit -ne $Commit) {
    throw 'Build evidence and exact staged source identity disagree.'
}
if ([string]::IsNullOrWhiteSpace($Version)) { throw 'Candidate version is missing.' }
$RepoCommit = (& git -C $Repo rev-parse HEAD).Trim()
if ($LASTEXITCODE) { throw 'Cannot resolve packaging source identity.' }
& git -C $Repo merge-base --is-ancestor $Commit $RepoCommit
if ($LASTEXITCODE) { throw 'Built source must be an ancestor of the packaging source.' }
$Changes = @(& git -C $Repo diff --name-only $Commit $RepoCommit)
if ($LASTEXITCODE) { throw 'Cannot verify source changes since build.' }
foreach ($Changed in $Changes) {
    if ($Changed -notmatch '^(docs/|\.github/|AGENTS\.md$|README\.md$|\.gitignore$|scripts/package-multi-api-candidate\.ps1$|tests/multi_api/test_candidate_packaging\.py$)') {
        throw "Binary/build inputs changed since validated source; rebuild required: $Changed"
    }
}
$Dirty = @(& git -C $Repo status --porcelain --untracked-files=no)
if ($LASTEXITCODE -or $Dirty.Count) { throw 'Packaging source must be committed and clean.' }
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
$ChineseAddons = @($Addons | Where-Object { -not $_.Name.EndsWith('.en.addon64', [StringComparison]::OrdinalIgnoreCase) })
if ($ChineseAddons.Count -ne 1) { throw 'Exactly one verified Chinese core must be selected for delivery.' }
$Parent = Split-Path -Parent $Output
[IO.Directory]::CreateDirectory($Parent) | Out-Null
$Temporary = Join-Path $Parent ('.nr-candidate-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($Temporary) | Out-Null
try {
    $Packages = New-Object 'System.Collections.Generic.List[object]'
    foreach ($Addon in $ChineseAddons) {
        $Package = Join-Path $Temporary $Addon.BaseName
        New-Item -ItemType Directory -Path $Package | Out-Null
        Copy-Item -LiteralPath $Addon.FullName -Destination $Package
        Copy-Item -LiteralPath (Join-Path $CoreBuild 'nrchain_nvngx.dll') -Destination $Package
        Copy-Item -LiteralPath (Join-Path $Build $CarrierLeaf) -Destination $Package
        $Licenses = "Carrier license`r`n==============`r`n" + [IO.File]::ReadAllText((Join-Path $Stage 'carrier/LICENSE')) +
            "`r`nMinHook license`r`n===============`r`n" + [IO.File]::ReadAllText((Join-Path $Repo 'third_party/minhook/LICENSE.txt')) +
            "`r`nThird-party notices`r`n===================`r`n" + [IO.File]::ReadAllText((Join-Path $Repo 'THIRD_PARTY_NOTICES.txt'))
        [IO.File]::WriteAllText((Join-Path $Package 'LICENSES.txt'), $Licenses, (New-Object Text.UTF8Encoding($false)))
        $Install = @"
$DisplayVersion

退出游戏，备份现用核心与配套DLL。将本包中文核心、nrchain_nvngx.dll和$CarrierLeaf一起放到原ReShade addon目录，替换旧0.4.5组件。移走旧同类核心/carrier，保留nr_before_sr.ini及其他无关MOD。
兼容范围：已有原生/模组DLSS输入的DX11 carrier路线，以及D3D12 SR前/后置和符合条件的RR后置。不是所有无DLSS的DX11游戏都支持；不含Feeder、原生Vulkan/Legacy支持。
F6对比开关，F8生成单TXT反馈；视觉问题可另按F7。新组合仍需逐游戏测试，Windows/WARP通过不等于所有游戏通过。
回退：退出游戏，移走本包三个组件，放回备份。包不含替换INI、ReShade、NVIDIA运行库/模型或游戏文件。
实际组件身份和哈希见build-info.json、SHA256.json；完整验证保留在源码仓库docs/interop/MAIN_CONSOLIDATION.md。
"@
        [IO.File]::WriteAllText((Join-Path $Package '安装说明.txt'), $Install, (New-Object Text.UTF8Encoding($false)))
        $BuildInfo = [ordered]@{display_version=$DisplayVersion; version=$Version; source_commit=$Commit; packaging_commit=$RepoCommit;
            language='zh-CN'; core_pe_version='0.4.5.104'; bridge_upstream_commit=$Manifest.integration.bridge_commit;
            source_manifest_sha256=(Get-FileHash -LiteralPath (Join-Path $Stage 'source-manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant();
            validation_sha256=(Get-FileHash -LiteralPath (Join-Path $Build 'validation.json') -Algorithm SHA256).Hash.ToLowerInvariant();
            local_windows_verified=$true; local_warp_verified=$true; game_runtime_verified=$false; stable_release=$false}
        [IO.File]::WriteAllText((Join-Path $Package 'build-info.json'), ($BuildInfo | ConvertTo-Json -Depth 4), (New-Object Text.UTF8Encoding($false)))
        $Hashes = @(Get-ChildItem -LiteralPath $Package -File -Recurse | Sort-Object FullName | ForEach-Object {
            [ordered]@{file=$_.FullName.Substring($Package.Length + 1).Replace('\','/'); sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
        })
        [IO.File]::WriteAllText((Join-Path $Package 'SHA256.json'), ($Hashes | ConvertTo-Json -Depth 4), (New-Object Text.UTF8Encoding($false)))
        $ZipLeaf = $Addon.BaseName.Replace($Version, $DisplayVersion) + '-OTA.zip'
        $Zip = Join-Path $Temporary $ZipLeaf
        Compress-Archive -Path (Join-Path $Package '*') -DestinationPath $Zip
        $Packages.Add([ordered]@{file=$ZipLeaf; sha256=(Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash.ToLowerInvariant()})
    }
    $Index = [ordered]@{source_commit=$Commit; packaging_commit=$RepoCommit; version=$Version; display_version=$DisplayVersion; language='zh-CN'; stable_release=$false; nvidia_feature18_verified=$Validation.nvidia_feature18_verified; game_runtime_verified=$Validation.game_runtime_verified; packages=@($Packages.ToArray())}
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
