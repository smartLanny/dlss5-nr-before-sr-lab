param(
    [Parameter(Mandatory=$true)][string]$CoreZh,
    [Parameter(Mandatory=$true)][string]$CoreEn,
    [Parameter(Mandatory=$true)][string]$Bridge,
    [Parameter(Mandatory=$true)][string]$Licenses,
    [Parameter(Mandatory=$true)][string]$SourceCommit,
    [Parameter(Mandatory=$true)][string]$CoreZhSha256,
    [Parameter(Mandatory=$true)][string]$CoreEnSha256,
    [Parameter(Mandatory=$true)][string]$BridgeSha256,
    [Parameter(Mandatory=$true)][string]$LicensesSha256,
    [Parameter(Mandatory=$true)][string]$FaceAssetsManifest,
    [Parameter(Mandatory=$true)][string]$ControlledValidationSummary,
    [Parameter(Mandatory=$true)][string]$ControlledValidationSha256,
    [Parameter(Mandatory=$true)][string]$Runtime20,
    [Parameter(Mandatory=$true)][string]$Runtime40,
    [Parameter(Mandatory=$true)][string]$KioInstructions,
    [Parameter(Mandatory=$true)][string]$WorkspaceRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [string]$Version = '0.5.2-beta.13'
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Assert-Pin([string]$Path, [string]$Expected) {
    $resolved = (Resolve-Path -LiteralPath $Path).Path
    if ($Expected -notmatch '^[0-9a-fA-F]{64}$') { throw "Missing exact SHA-256 pin: $resolved" }
    $actual = (Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $Expected.ToLowerInvariant()) { throw "SHA-256 mismatch: $resolved" }
    return [pscustomobject]@{ path=$resolved; sha256=$actual; bytes=(Get-Item -LiteralPath $resolved).Length }
}

if ($SourceCommit -notmatch '^[0-9a-fA-F]{40}$') { throw 'A full frozen source commit is required' }
if ($Version -ne '0.5.2-beta.13') { throw 'This prepared script is pinned to Beta13' }
$outputAbsolute = [IO.Path]::GetFullPath($OutputRoot)
$allowedRoot = [IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd([IO.Path]::DirectorySeparatorChar)
if (-not $outputAbsolute.StartsWith($allowedRoot + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw "OutputRoot must stay inside WorkspaceRoot: $allowedRoot" }
if (Test-Path -LiteralPath $outputAbsolute) { throw "Output must be fresh: $outputAbsolute" }
$zh = Assert-Pin $CoreZh $CoreZhSha256
$en = Assert-Pin $CoreEn $CoreEnSha256
$bridgePin = Assert-Pin $Bridge $BridgeSha256
$licensesPin = Assert-Pin $Licenses $LicensesSha256
if ($CoreZh -notlike '*.addon64' -or $CoreEn -notlike '*.addon64') { throw 'Core inputs must be addon64 files' }
if ([IO.Path]::GetFileName($Bridge) -ne 'nrchain_nvngx.dll') { throw 'Unexpected bridge name' }
$faceAssets = @(Get-Content -LiteralPath $FaceAssetsManifest -Raw -Encoding UTF8 | ConvertFrom-Json)
$validationPin = Assert-Pin $ControlledValidationSummary $ControlledValidationSha256
$controlledValidation = Get-Content -LiteralPath $validationPin.path -Raw -Encoding UTF8 | ConvertFrom-Json
if ($controlledValidation.source_commit -ne $SourceCommit.ToLowerInvariant() -or @($controlledValidation.variants).Count -ne 2) { throw 'Beta13 controlled validation summary identity/count mismatch' }
if ($controlledValidation.status -ne 'passed') { throw 'Beta13 controlled validation summary not passed' }
$requiredFaceNames = @('onnxruntime.dll','onnxruntime_providers_shared.dll','LICENSE','ThirdPartyNotices.txt','yunet-dynamic.onnx','YUNET-LICENSE','yunet-dynamic.json')
if ($faceAssets.Count -ne $requiredFaceNames.Count) { throw 'Exact seven face assets are required' }
$facePins = @()
foreach ($asset in $faceAssets) {
    if ($asset.name -notin $requiredFaceNames -or @($facePins | Where-Object { $_.name -eq $asset.name }).Count) { throw 'Unexpected or repeated face asset' }
    $pin = Assert-Pin ([string]$asset.path) ([string]$asset.sha256)
    $facePins += [pscustomobject]@{ name=[string]$asset.name; path=$pin.path; sha256=$pin.sha256; bytes=$pin.bytes }
}

$variants = @(
    [pscustomobject]@{ id='RTX20-30'; runtime=$Runtime20; sha256='6dac1b40f0c87af84a8177b18c741e84fb0c914f204c9d87d95916b665ba3af8'; bytes=309671536 },
    [pscustomobject]@{ id='RTX40-50'; runtime=$Runtime40; sha256='a2d16f9fba2b619559427821169918df56fc8e410a6ac87569953598a00b66fa'; bytes=165844992 }
)
foreach ($v in $variants) {
    $p = Assert-Pin $v.runtime $v.sha256
    if ($p.bytes -ne $v.bytes) { throw "Runtime size mismatch: $($v.id)" }
    $check = @($controlledValidation.variants | Where-Object { $_.id -eq $v.id })
    if ($check.Count -ne 1 -or $check[0].runtime_sha256 -ne $v.sha256 -or $check[0].status -ne 'passed' -or $check[0].actual_exit_code -ne 0) { throw "Beta13 controlled validation failed or missing: $($v.id)" }
    if (@($check[0].cases).Count -ne 3 -or @($check[0].cases | Where-Object { $_.status -ne 'passed' -or $_.actual_exit_code -ne 0 }).Count) { throw "Beta13 controlled cases incomplete: $($v.id)" }
}

New-Item -ItemType Directory -Path $outputAbsolute | Out-Null
$receipts = @()
foreach ($v in $variants) {
    $stage = Join-Path $outputAbsolute ('stage-' + $v.id)
    $install = Join-Path $stage 'install'
    $languages = Join-Path $stage 'languages'
    New-Item -ItemType Directory -Path $install,$languages | Out-Null
    Copy-Item -LiteralPath $zh.path -Destination (Join-Path $install 'nr-before-sr.zh-CN.addon64')
    Copy-Item -LiteralPath $bridgePin.path -Destination (Join-Path $install 'nrchain_nvngx.dll')
    Copy-Item -LiteralPath $v.runtime -Destination (Join-Path $install 'nvngx_dlssnr.dll')
    Copy-Item -LiteralPath $licensesPin.path -Destination (Join-Path $stage 'LICENSES.txt')
    $faceDirectory = Join-Path $install 'nr_face'
    New-Item -ItemType Directory -Path $faceDirectory | Out-Null
    foreach ($asset in $facePins) { Copy-Item -LiteralPath $asset.path -Destination (Join-Path $faceDirectory $asset.name) }
    if ($v.id -eq 'RTX40-50') {
        $kioPin = Assert-Pin $KioInstructions '378ea0a5e1c2aeb0591015c7f7c04a4f0d6ce0c32e94fd16b6a2ecbf081a758c'
        $destination = Join-Path $stage 'Kio-原作者使用说明.txt'
        Copy-Item -LiteralPath $kioPin.path -Destination $destination
        [void](Assert-Pin $destination $kioPin.sha256)
    }

    $englishStage = Join-Path $outputAbsolute ('english-' + $v.id)
    New-Item -ItemType Directory -Path $englishStage | Out-Null
    Copy-Item -LiteralPath $en.path -Destination (Join-Path $englishStage 'nr-before-sr.en.addon64')
    [IO.Compression.ZipFile]::CreateFromDirectory($englishStage,(Join-Path $languages 'English-Core.zip'))

    $readme = @"
DLSS5 $Version — $($v.id)

先退出游戏，将旧 Core 与 NR DLL 备份到插件扫描目录之外，再把旧 Core 移出扫描目录，复制 install 内文件到已核实的 ReShade 插件目录。默认使用中文 Core；只保留一个 nr-before-sr*.addon64 和一个 nvngx_dlssnr.dll，其他插件不动。English-Core.zip 是手动替换中文 Core 的备选，不要同时安装。
不覆盖游戏原生 SR/RR 库或已保存设置。已知限制：切换 Esc 菜单时 NR 可能短暂消失；私有 SR 与空间降噪仍属手动实验，不代表画质已获推荐。详细组件及测试范围见 manifest.json。40/50 包保留 Kio 原作者说明。

Exit the game. Back up the old Core and NR DLL outside add-on scan paths, then remove the old Core from the scan path. Copy the contents of install to the verified ReShade add-on directory. Keep only one nr-before-sr*.addon64 and one nvngx_dlssnr.dll; leave other add-ons untouched. English-Core.zip replaces the Chinese Core, never co-installs with it. Saved settings and game-owned SR/RR DLLs are untouched. Known limits: NR may briefly disappear during the Esc menu transition; private SR and spatial denoising remain manual experiments.
"@
    [IO.File]::WriteAllText((Join-Path $stage 'README.zh-CN.txt'),$readme,(New-Object System.Text.UTF8Encoding($false)))
    $variantValidation = $controlledValidation.variants | Where-Object { $_.id -eq $v.id } | Select-Object -First 1
    $validationPublic = [ordered]@{
        status='passed'; adapter_scope='local RTX 5090 D'; frames_per_case=4
        cases=@($variantValidation.cases | ForEach-Object { @{ name=$_.name; actual_exit_code=$_.actual_exit_code; status=$_.status } })
        evidence_sha256=$validationPin.sha256
        boundary=$(if ($v.id -eq 'RTX20-30') {
            'Synthetic NGX host on RTX 5090 D; not RTX20/30 hardware or game acceptance'
        } else {
            'Synthetic NGX host on RTX 5090 D; not game or whole RTX40/50-family acceptance'
        })
    }
    $manifest = [ordered]@{
        schema='dlss5-local-two-runtime-handoff/v1'; version=$Version; source_commit=$SourceCommit.ToLowerInvariant()
        gpu_family=$v.id; default_language='zh-CN'; english_core='languages/English-Core.zip (manual alternative, never co-install)'
        install_files=@(
            @{ path='install/nr-before-sr.zh-CN.addon64'; sha256=$zh.sha256; bytes=$zh.bytes },
            @{ path='install/nrchain_nvngx.dll'; sha256=$bridgePin.sha256; bytes=$bridgePin.bytes },
            @{ path='install/nvngx_dlssnr.dll'; sha256=$v.sha256; bytes=$v.bytes }
        )
        english_core_sha256=$en.sha256; licenses_sha256=$licensesPin.sha256
        face_assets=@($facePins | ForEach-Object { @{ path=('install/nr_face/' + $_.name); sha256=$_.sha256; bytes=$_.bytes } })
        runtime_contract='052_BETA4_RUNTIME_CONTRACT.json; Beta13 compatibility requires separate controlled evidence'
        beta13_controlled_validation=$validationPublic
        beta13_controlled_summary_sha256=$validationPin.sha256
        game_acceptance=$false; rtx20_30_hardware_tested=$false
    }
    [IO.File]::WriteAllText((Join-Path $stage 'manifest.json'),($manifest | ConvertTo-Json -Depth 7),(New-Object System.Text.UTF8Encoding($false)))
    $outerAddonCount = @(Get-ChildItem -LiteralPath $stage -Recurse -File -Filter '*.addon64').Count
    $outerRuntimeCount = @(Get-ChildItem -LiteralPath $stage -Recurse -File -Filter 'nvngx_dlssnr.dll').Count
    if ($outerAddonCount -ne 1 -or $outerRuntimeCount -ne 1) { throw "Invalid outer layout: $($v.id)" }
    [void](Assert-Pin (Join-Path $install 'nr-before-sr.zh-CN.addon64') $zh.sha256)
    [void](Assert-Pin (Join-Path $install 'nrchain_nvngx.dll') $bridgePin.sha256)
    [void](Assert-Pin (Join-Path $install 'nvngx_dlssnr.dll') $v.sha256)
    foreach ($asset in $facePins) { [void](Assert-Pin (Join-Path $faceDirectory $asset.name) $asset.sha256) }
    $zip = Join-Path $outputAbsolute ("DLSS5-$Version-$($v.id).zip")
    [IO.Compression.ZipFile]::CreateFromDirectory($stage,$zip)
    $outer = [IO.Compression.ZipFile]::OpenRead($zip)
    try {
        $names = @($outer.Entries | ForEach-Object FullName)
        if (@($names | Where-Object { $_ -like '*.addon64' }).Count -ne 1 -or
            @($names | Where-Object { $_ -like '*nvngx_dlssnr.dll' }).Count -ne 1 -or
            @($names | Where-Object { $_ -like '*.ini' }).Count -ne 0) { throw "ZIP layout verification failed: $($v.id)" }
    } finally { $outer.Dispose() }
    $zipSha = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    $receipts += [pscustomobject]@{ id=$v.id; zip=$zip; sha256=$zipSha; bytes=(Get-Item -LiteralPath $zip).Length; source_commit=$SourceCommit.ToLowerInvariant(); status='assembled-unpublished' }
}
$receipts | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputAbsolute 'assembly-receipt.json') -Encoding UTF8
$receipts | ConvertTo-Json -Depth 5
