param(
    [Parameter(Mandatory=$true)][string]$BuildRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [string]$FaceRuntimeRoot,
    [string]$FaceModel,
    [string]$FaceModelLicense
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
# Integration remains provenance; the final colour overlay owns product identity.
$Product = $Manifest.integration
foreach ($Key in @('colour_lab', 'colour_edge')) {
    $Property = $Manifest.PSObject.Properties[$Key]
    if ($null -ne $Property) {
        if ($Property.Value.source_commit -ne $Commit) { throw 'Final product source identity disagrees with staged source.' }
        $Product = $Property.Value
    }
}
$Version = [string]$Product.version
$Unified = $Version -in @('beta0.5-dline21-unified1','beta0.5-dline21-unified2','beta0.5-dline21-unified3','beta0.5-dline21-unified3-rr1','beta0.5-dline21-unified4','beta0.5-dline21-unified5','beta0.5-dline21-unified6','beta0.5-dline21-unified7')
$DisplayProperty = $Product.PSObject.Properties['display_version']
$DisplayVersion = if ($null -ne $DisplayProperty) { [string]$DisplayProperty.Value } else { $Version }
if ([string]::IsNullOrWhiteSpace($DisplayVersion) -or $DisplayVersion.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) {
    throw 'Invalid public candidate name.'
}
if ($Commit -notmatch '^[0-9a-f]{40}$' -or $Validation.source_commit -ne $Commit -or $Manifest.integration.source_commit -ne $Commit) {
    throw 'Build evidence and exact staged source identity disagree.'
}
if ([string]::IsNullOrWhiteSpace($Version)) { throw 'Candidate version is missing.' }
if ($Unified -and ($Validation.PSObject.Properties['quality_successor'] -eq $null -or $Validation.quality_successor -ne $true)) {
    throw 'Unified product requires its final quality-successor build evidence.'
}
$RepoCommit = (& git -C $Repo rev-parse HEAD).Trim()
if ($LASTEXITCODE) { throw 'Cannot resolve packaging source identity.' }
& git -C $Repo merge-base --is-ancestor $Commit $RepoCommit
if ($LASTEXITCODE) { throw 'Built source must be an ancestor of the packaging source.' }
$Changes = @(& git -C $Repo diff --name-only $Commit $RepoCommit)
if ($LASTEXITCODE) { throw 'Cannot verify source changes since build.' }
foreach ($Changed in $Changes) {
    if ($Changed -notmatch '^(docs/|\.github/|AGENTS\.md$|README\.md$|\.gitignore$|scripts/package-multi-api-candidate\.ps1$|scripts/validate-(beta04-structure|strength-cap)\.py$|tests/descriptor_provenance_warp\.inl$|tests/multi_api/test_candidate_packaging\.py$|tests/test_lockperf2_workflow\.py$)') {
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
$PostScenarios = if ($Version -eq 'beta0.5-dline21-unified7') { 60 } else { 48 }
if (-not (Test-Path -LiteralPath $PostLog) -or (Get-Content -LiteralPath $PostLog -Raw -Encoding UTF8) -notmatch ("PASS post-scaled-work WARP: " + $PostScenarios + " scenarios")) {
    throw "Successful $PostScenarios-scenario post WARP log is missing."
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
$EnglishAddons = @($Addons | Where-Object { $_.Name.EndsWith('.en.addon64', [StringComparison]::OrdinalIgnoreCase) })
if ($ChineseAddons.Count -ne 1 -or $EnglishAddons.Count -ne 1) { throw 'One verified Core per language is required.' }
$FaceEnabled = $Validation.PSObject.Properties['full_face_backend'] -and $Validation.full_face_backend -eq $true
if ($Unified -and -not $FaceEnabled) { throw 'Unified package requires the complete face backend.' }
$NgxLicense = Join-Path $CoreBuild 'NVIDIA-NGX-LICENSE.txt'
if ($Unified -and -not (Test-Path -LiteralPath $NgxLicense)) { throw 'Unified package requires the nrchain SDK license.' }
if (Test-Path -LiteralPath $NgxLicense) {
    $NgxReceipt = Get-Content -LiteralPath (Join-Path $CoreBuild 'nrchain-build.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    Assert-Hash $NgxLicense ([string]$NgxReceipt.input_sha256.'LICENSE.txt')
    Assert-Hash (Join-Path $CoreBuild 'nrchain_nvngx.dll') ([string]$NgxReceipt.binary_sha256)
}
$FaceAssets = [ordered]@{}
$AmdLicense = $null
if ($Version -in @('beta0.5-dline21-unified6','beta0.5-dline21-unified7')) {
    $TemporalReceipt = Get-Content -LiteralPath (Join-Path $CoreBuild 'obj/temporal/module.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if (-not $TemporalReceipt.passed) { throw 'Temporal backend compilation is not verified.' }
    $AmdLicense = Join-Path $CoreBuild 'obj/temporal/AMD-FidelityFX-LICENSE.txt'
    Assert-Hash $AmdLicense ([string]$TemporalReceipt.license_sha256)
}
$FaceHashes = @{}
$FaceReceiptHash = $null
if ($FaceEnabled) {
    if (-not $FaceRuntimeRoot -or -not $FaceModel -or -not $FaceModelLicense) { throw 'Enabled face backend requires all pinned assets and licenses.' }
    $ReceiptPath = Join-Path $CoreBuild 'face-backend.json'
    $Receipt = Get-Content -LiteralPath $ReceiptPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($Receipt.schema -ne 'nr-face-build-v1' -or $Receipt.enabled -ne $true -or $Receipt.runtime_version -ne '1.23.2' -or
        $Receipt.runtime_sha256 -ne 'dec964ab1ee36cc9b0ae247d13b376627992fc57dec0454354017ab8fd84f1ea' -or
        $Receipt.model_sha256 -ne 'b1b3061b1ae7a43bcc1497284ade754f701db774ef3a19481edcaf27fbc425b1') { throw 'Face build receipt differs from validated full backend.' }
    $FaceReceiptHash = (Get-FileHash -LiteralPath $ReceiptPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $FaceRoot = (Resolve-Path -LiteralPath $FaceRuntimeRoot).Path
    $Pins = [ordered]@{
        'lib/onnxruntime.dll'='dec964ab1ee36cc9b0ae247d13b376627992fc57dec0454354017ab8fd84f1ea'
        'lib/onnxruntime_providers_shared.dll'='a2b3a50956aa75a9879c8472bc7df4f7a8072bcd2db19a1b7d988e7688f293ef'
        'LICENSE'='c250d6278f0b47a6439fb7592b08b58a55eb9f535aa49a1db63211c3f982b674'
        'ThirdPartyNotices.txt'='e00f828e0a33de591a355ae6606d2625f5758da7d2c844db7821c9dd3e3647b6'
    }
    foreach ($Entry in $Pins.GetEnumerator()) {
        $Asset = Safe-Child $FaceRoot $Entry.Key
        Assert-Hash $Asset $Entry.Value
        $Name = 'nr_face/' + [IO.Path]::GetFileName($Asset)
        $FaceAssets[$Name] = $Asset
        $FaceHashes[$Name] = $Entry.Value
    }
    $Model = (Resolve-Path -LiteralPath $FaceModel).Path
    $ModelLicense = (Resolve-Path -LiteralPath $FaceModelLicense).Path
    Assert-Hash $Model ([string]$Receipt.model_sha256)
    Assert-Hash $ModelLicense 'c83b8120c50ccbd4c4f96edf53141bdd566ebb8f8e9227e415326aa1b1aba958'
    $ModelReceiptPath = [IO.Path]::ChangeExtension($Model, '.json')
    $ModelReceipt = Get-Content -LiteralPath $ModelReceiptPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($ModelReceipt.schema -ne 'nr-face-model-shape-v1' -or $ModelReceipt.sha256 -ne $Receipt.model_sha256 -or
        $ModelReceipt.source_sha256 -ne '8f2383e4dd3cfbb4553ea8718107fc0423210dc964f9f4280604804ed2552fa4' -or
        $ModelReceipt.onnx_version -ne '1.19.1' -or $ModelReceipt.nodes_weights_unchanged -ne $true -or
        $ModelReceipt.bytes -ne (Get-Item -LiteralPath $Model).Length) { throw 'Prepared face model provenance mismatch.' }
    $FaceAssets['nr_face/yunet-dynamic.onnx'] = $Model
    $FaceAssets['nr_face/YUNET-LICENSE'] = $ModelLicense
    $FaceAssets['nr_face/yunet-dynamic.json'] = $ModelReceiptPath
    $FaceHashes['nr_face/yunet-dynamic.onnx'] = [string]$Receipt.model_sha256
    $FaceHashes['nr_face/YUNET-LICENSE'] = 'c83b8120c50ccbd4c4f96edf53141bdd566ebb8f8e9227e415326aa1b1aba958'
    $FaceHashes['nr_face/yunet-dynamic.json'] = (Get-FileHash -LiteralPath $ModelReceiptPath -Algorithm SHA256).Hash.ToLowerInvariant()
}
$Parent = Split-Path -Parent $Output
[IO.Directory]::CreateDirectory($Parent) | Out-Null
$Temporary = Join-Path $Parent ('.nr-candidate-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($Temporary) | Out-Null
try {
    $Packages = New-Object 'System.Collections.Generic.List[object]'
    foreach ($Addon in $Addons) {
        $Language = if ($Addon.Name.EndsWith('.en.addon64', [StringComparison]::OrdinalIgnoreCase)) { 'en' } else { 'zh-CN' }
        $Package = Join-Path $Temporary $Addon.BaseName
        New-Item -ItemType Directory -Path $Package | Out-Null
        Copy-Item -LiteralPath $Addon.FullName -Destination $Package
        Copy-Item -LiteralPath (Join-Path $CoreBuild 'nrchain_nvngx.dll') -Destination $Package
        Copy-Item -LiteralPath (Join-Path $Build $CarrierLeaf) -Destination $Package
        if (Test-Path -LiteralPath $NgxLicense) { Copy-Item -LiteralPath $NgxLicense -Destination $Package }
        if ($AmdLicense) { Copy-Item -LiteralPath $AmdLicense -Destination $Package }
        foreach ($Leaf in @($Addon.Name, 'nrchain_nvngx.dll', $CarrierLeaf)) {
            $Evidence = @($Validation.binaries | Where-Object { $_.file -eq $Leaf })
            Assert-Hash (Join-Path $Package $Leaf) ([string]$Evidence[0].sha256)
        }
        foreach ($Asset in $FaceAssets.GetEnumerator()) {
            $Destination = Join-Path $Package $Asset.Key
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Destination)) | Out-Null
            Copy-Item -LiteralPath $Asset.Value -Destination $Destination
            Assert-Hash $Destination $FaceHashes[$Asset.Key]
        }
        $Licenses = "Carrier license`r`n==============`r`n" + [IO.File]::ReadAllText((Join-Path $Stage 'carrier/LICENSE')) +
            "`r`nMinHook license`r`n===============`r`n" + [IO.File]::ReadAllText((Join-Path $Repo 'third_party/minhook/LICENSE.txt')) +
            "`r`nThird-party notices`r`n===================`r`n" + [IO.File]::ReadAllText((Join-Path $Repo 'THIRD_PARTY_NOTICES.txt'))
        [IO.File]::WriteAllText((Join-Path $Package 'LICENSES.txt'), $Licenses, (New-Object Text.UTF8Encoding($false)))
        if ($Language -eq 'zh-CN') {
            $Install = @"
$DisplayVersion — 中文研究候选，尚未游戏验收。
退出游戏并备份旧核心，只保留一份语言核心。保留已有 INI 与模型设置。
D3D12：使用本包核心和 nrchain_nvngx.dll；不安装 DX11 carrier。
DX11 已有 DLSS 兼容路线：另需 ${CarrierLeaf}；不宣称支持所有 DX11 游戏或 Vulkan。
若包含 nr_face，请保持该目录与核心相邻；它包含可选人脸功能的 CPU 运行库、模型和许可证，不会自动开启功能。
默认 F6 对比、F8 导出文字反馈；可在开发者页改键。Windows/WARP 通过不等于游戏通过；使用同场景同配置验收。
回退时退出游戏，移出本批核心/桥接/人脸目录，恢复备份。包不含 INI、ReShade 或 NVIDIA 私有运行库/模型。
身份、依赖和哈希见 build-info.json、SHA256.json。
"@
        } else {
            $Install = @"
$DisplayVersion — English research candidate; NOT game verified.
Exit the game and back up the prior Core. Keep exactly one language Core and preserve existing INI/model settings.
D3D12: use the Core and nrchain_nvngx.dll; do not install the DX11 carrier.
DX11 with an existing compatible DLSS input path additionally needs ${CarrierLeaf}; this is not universal DX11 or Vulkan support.
If present, keep nr_face beside the Core. It contains the optional face feature's CPU runtime, model and licenses; packaging does not enable it.
Default keys: F6 compares and F8 exports a text report; remap them on Developer. Windows/WARP validation is not game validation; keep scene and settings identical.
For rollback, exit the game, remove this candidate's Core/bridge/face directory and restore backups. No INI, ReShade or NVIDIA private runtime/model is included.
See build-info.json and SHA256.json for exact identity, dependencies and hashes.
"@
        }
        $Readme = if ($Language -eq 'en') { 'READ-FIRST.txt' } else { '安装说明.txt' }
        [IO.File]::WriteAllText((Join-Path $Package $Readme), $Install, (New-Object Text.UTF8Encoding($false)))
        if ($Version -in @('beta0.5-dline21-unified3','beta0.5-dline21-unified3-rr1','beta0.5-dline21-unified4','beta0.5-dline21-unified5','beta0.5-dline21-unified6','beta0.5-dline21-unified7')) {
            $GuideSource = if ($Language -eq 'en') { 'UNIFIED3_UI_EN_20260920.md' } else { 'UNIFIED3_UI_20260920.md' }
            if ($Version -eq 'beta0.5-dline21-unified4') {
                $GuideSource = if ($Language -eq 'en') { 'UNIFIED4_UI_EN_20260921.md' } else { 'UNIFIED4_UI_20260921.md' }
            }
            if ($Version -eq 'beta0.5-dline21-unified5') {
                $GuideSource = if ($Language -eq 'en') { 'UNIFIED5_UI_EN_20260921.md' } else { 'UNIFIED5_UI_20260921.md' }
            }
            if ($Version -eq 'beta0.5-dline21-unified6') {
                $GuideSource = if ($Language -eq 'en') { 'UNIFIED6_UI_EN_20260921.md' } else { 'UNIFIED6_UI_20260921.md' }
            }
            if ($Version -eq 'beta0.5-dline21-unified7') {
                $GuideSource = if ($Language -eq 'en') { 'UNIFIED7_UI_20260921.en.md' } else { 'UNIFIED7_UI_20260921.md' }
            }
            $GuideName = if ($Language -eq 'en') { 'MENU-GUIDE.md' } else { '菜单与测试.md' }
            Copy-Item -LiteralPath (Join-Path $Repo ('docs/interop/' + $GuideSource)) -Destination (Join-Path $Package $GuideName)
        }
        if ($Version -eq 'beta0.5-dline21-unified3-rr1') {
            Copy-Item -LiteralPath (Join-Path $Repo 'docs/interop/RR_CURRENT_PROJECTION_20260920.md') -Destination (Join-Path $Package 'RR-CANDIDATE.md')
        }
        if ($Version -eq 'beta0.5-dline21-unified4') {
            Copy-Item -LiteralPath (Join-Path $Repo 'docs/interop/UNIFIED4_CONTINUATION_20260921.md') -Destination (Join-Path $Package 'IMPLEMENTATION-STATUS.md')
        }
        $BuildInfo = [ordered]@{display_version=$DisplayVersion; version=$Version; source_commit=$Commit; packaging_commit=$RepoCommit;
            language=$Language; full_face_backend=[bool]$FaceEnabled; face_backend_receipt_sha256=$FaceReceiptHash; core_pe_version=('{0}.{1}.{2}.{3}' -f $Addon.VersionInfo.FileMajorPart, $Addon.VersionInfo.FileMinorPart, $Addon.VersionInfo.FileBuildPart, $Addon.VersionInfo.FilePrivatePart);
            core_file_version=$Addon.VersionInfo.FileVersion; carrier_upstream_file_version=(Get-Item -LiteralPath (Join-Path $Build $CarrierLeaf)).VersionInfo.FileVersion; bridge_upstream_commit=$Manifest.integration.bridge_commit;
            source_manifest_sha256=(Get-FileHash -LiteralPath (Join-Path $Stage 'source-manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant();
            validation_sha256=(Get-FileHash -LiteralPath (Join-Path $Build 'validation.json') -Algorithm SHA256).Hash.ToLowerInvariant();
            local_windows_verified=$true; local_warp_verified=$true; game_runtime_verified=$false; stable_release=$false}
        [IO.File]::WriteAllText((Join-Path $Package 'build-info.json'), ($BuildInfo | ConvertTo-Json -Depth 4), (New-Object Text.UTF8Encoding($false)))
        $Hashes = @(Get-ChildItem -LiteralPath $Package -File -Recurse | Sort-Object FullName | ForEach-Object {
            [ordered]@{file=$_.FullName.Substring($Package.Length + 1).Replace('\','/'); sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
        })
        [IO.File]::WriteAllText((Join-Path $Package 'SHA256.json'), ($Hashes | ConvertTo-Json -Depth 4), (New-Object Text.UTF8Encoding($false)))
        $ZipLeaf = 'DLSS5-' + $DisplayVersion + '-' + $Language + '-OTA.zip'
        $Zip = Join-Path $Temporary $ZipLeaf
        Compress-Archive -Path (Join-Path $Package '*') -DestinationPath $Zip
        $Packages.Add([ordered]@{language=$Language; file=$ZipLeaf; sha256=(Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash.ToLowerInvariant()})
    }
    $Index = [ordered]@{source_commit=$Commit; packaging_commit=$RepoCommit; version=$Version; display_version=$DisplayVersion; languages=@('zh-CN','en'); full_face_backend=[bool]$FaceEnabled; stable_release=$false; nvidia_feature18_verified=$Validation.nvidia_feature18_verified; game_runtime_verified=$Validation.game_runtime_verified; packages=@($Packages.ToArray())}
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
