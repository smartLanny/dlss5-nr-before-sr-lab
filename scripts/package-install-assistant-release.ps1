param(
    [string]$Dotnet = 'C:\Users\pc1\Documents\ChatGPT\DLSS5\dlss5-lab\toolchains\dotnet10\dotnet.exe',
    [string]$ComponentRoot = '..\github-staging\release\beta0.3.7-full-r3',
    [string]$OutputRoot = 'release\install-assistant-beta0.3.7-r3-final2'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$component = [IO.Path]::GetFullPath((Join-Path $root $ComponentRoot))
$output = [IO.Path]::GetFullPath((Join-Path $root $OutputRoot))
if (Test-Path -LiteralPath $output) { throw "Release destination already exists: $output" }
New-Item -ItemType Directory -Path $output | Out-Null

$assistantProject = Join-Path $root 'tools\install-assistant\Dlss5InstallAssistant.csproj'
$helperProject = Join-Path $root 'tools\install-assistant.Helper\Dlss5InstallAssistant.Helper.csproj'
& $Dotnet publish $assistantProject -c Release -r win-x64 --self-contained true
if ($LASTEXITCODE -ne 0) { throw 'assistant publish failed' }
& $Dotnet publish $helperProject -c Release -r win-x64 --self-contained true
if ($LASTEXITCODE -ne 0) { throw 'helper publish failed' }
$assistantPublish = Join-Path $root 'tools\install-assistant\bin\Release\net10.0-windows\win-x64\publish'
$helperExe = Join-Path $root 'tools\install-assistant.Helper\bin\Release\net10.0-windows\win-x64\publish\DLSS5InstallAssistant.Helper.exe'

$appDir = Join-Path $output 'DLSS5InstallAssistant-win-x64'
New-Item -ItemType Directory -Path $appDir | Out-Null
Copy-Item -LiteralPath (Get-ChildItem -LiteralPath $assistantPublish -Force | Select-Object -ExpandProperty FullName) -Destination $appDir -Recurse -Force
Copy-Item -LiteralPath $helperExe -Destination (Join-Path $appDir 'DLSS5InstallAssistant.Helper.exe') -Force
$appZip = Join-Path $output 'DLSS5InstallAssistant-win-x64.zip'
Compress-Archive -Path (Join-Path $appDir '*') -DestinationPath $appZip -CompressionLevel Optimal

$readme = @'
# DLSS5 安装助手 beta0.3.7-r3

1. 先安装并启动受支持版本的 ReShade，确认游戏目录中的 ReShade.ini 含有有效 AddOnPath；助手只读取这个路径，不下载或代装 ReShade/loader。
2. 完全退出游戏，运行 DLSS5InstallAssistant.exe，扫描 Steam 或手动选择 EXE。
3. 只对兼容表中的 Cyberpunk 2077（Steam 1091500）和 The Witcher 3（Steam 292030）显示可应用计划；其它游戏、手选但未绑定 EXE SHA256 的目标只显示待确认。
4. 导入同包目录中的 manifest.json。安装前请阅读逐项目标计划；未知同名 DLL/addon、缺少 AddOnPath、路径穿越和 junction 都会拒绝。
5. “取消安装”会回滚已写入的文件；卸载和恢复会保留安装后被用户修改或删除的文件。启动游戏仍使用普通权限。
6. Helldivers 2（553850）永久禁止自动部署；运行状态只有在匹配当前 PID、EXE、启动时间、构建号、15 秒新鲜度和有效 NR/transfer 计数时才显示已生效。

作者：@野生的装机宅 Bilibili
'@

$variants = @(
    @{ Name='RTX50-中文'; Prefix='RTX50-DLSS5-AI渲染超分版-beta0.3.7-'; Addon='DLSS5-AI渲染超分版-beta0.3.7-@野生的装机宅-Bilibili.addon64'; Readme='说明.txt'; Runtime='nvngx_dlssnr.dll' },
    @{ Name='RTX50-English'; Prefix='RTX50-DLSS5-AI-Rendering-Upscaling-beta0.3.7-'; Addon='DLSS5-AI-Rendering-Upscaling-beta0.3.7-@野生的装机宅-Bilibili.en.addon64'; Readme='Instructions.txt'; Runtime='nvngx_dlssnr.dll' },
    @{ Name='RTX30-40-中文'; Prefix='RTX30-40-DLSS5-AI渲染超分版-beta0.3.7-'; Addon='DLSS5-AI渲染超分版-beta0.3.7-@野生的装机宅-Bilibili.addon64'; Readme='说明.txt'; Runtime='nvngx_dlssnr.dll' },
    @{ Name='RTX30-40-English'; Prefix='RTX30-40-DLSS5-AI-Rendering-Upscaling-beta0.3.7-'; Addon='DLSS5-AI-Rendering-Upscaling-beta0.3.7-@野生的装机宅-Bilibili.en.addon64'; Readme='Instructions.txt'; Runtime='nvngx_dlssnr.dll' }
)
foreach ($variant in $variants) {
    $sourceDir = Get-ChildItem -LiteralPath $component -Directory | Where-Object Name -Like ($variant.Prefix + '*') | Select-Object -First 1
    if ($null -eq $sourceDir) { throw "Component directory not found: $($variant.Prefix)" }
    $packageDir = Join-Path $output ('offline-package-' + $variant.Name)
    New-Item -ItemType Directory -Path $packageDir | Out-Null
    foreach ($name in @($variant.Addon, 'nrchain_nvngx.dll', 'nr_before_sr.ini', 'nvngx_dlssnr.dll')) {
        Copy-Item -LiteralPath (Join-Path $sourceDir.FullName $name) -Destination $packageDir
    }
    Set-Content -LiteralPath (Join-Path $packageDir 'README.txt') -Value $readme -Encoding UTF8
    $files = @(
        @{ path=$variant.Addon; role='addon'; source='beta0.3.7-full-r3/verified-local'; target='reshade-addon' },
        @{ path='nrchain_nvngx.dll'; role='bridge'; source='beta0.3.7-full-r3/verified-local'; target='reshade-addon' },
        @{ path='nr_before_sr.ini'; role='config'; source='beta0.3.7-full-r3/verified-local'; target='reshade-addon' },
        @{ path='nvngx_dlssnr.dll'; role='native-runtime'; source='beta0.3.7-full-r3/verified-local'; target='game-root' }
    )
    $manifestFiles = foreach ($file in $files) {
        $path = Join-Path $packageDir $file.path
        [ordered]@{ path=$file.path; sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToUpperInvariant(); role=$file.role; source=$file.source; target=$file.target; required=$true; allowedExistingSha256=@() }
    }
    $manifest = [ordered]@{
        schema='dlss5-install-manifest/v2'; id='nr-before-sr'; build='beta0.3.7'; sourceBuild='beta0.3.7-full-r3'; nativeD3D12=$true; onlineAllowed=$false
        reshade=[ordered]@{ required=$true; addOnPathSource='ReShade.ini'; knownLoaderSha256=@(); installHint='请先安装并配置受支持的 ReShade，助手不会下载或代装 loader。' }
        verifiedNativeD3D12=[ordered]@{ verified=$true; method='beta0.3.7-full-r3-native-d3d12-validation'; api='D3D12'; evidenceDocument='docs/BETA_0_3_3_R4_VALIDATION.md' }
        compatibleProfiles=@(
            [ordered]@{ steamAppIds=@('1091500'); executableSha256=@(); verifiedNativeD3D12=$true; evidence='Cyberpunk 2077 native D3D12 NR+transfer+capture' },
            [ordered]@{ steamAppIds=@('292030'); executableSha256=@(); verifiedNativeD3D12=$true; evidence='The Witcher 3 native D3D12 NR+transfer+capture' }
        )
        files=$manifestFiles
    }
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $packageDir 'manifest.json') -Encoding UTF8
    $zip = Join-Path $output ('offline-package-' + $variant.Name + '.zip')
    Compress-Archive -Path (Join-Path $packageDir '*') -DestinationPath $zip -CompressionLevel Optimal
}

$sumLines = foreach ($file in Get-ChildItem -LiteralPath $output -File -Recurse | Sort-Object FullName) {
    "$(Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName | Select-Object -ExpandProperty Hash)  $($file.FullName.Substring($output.Length + 1))"
}
Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Value $sumLines -Encoding UTF8
Set-Content -LiteralPath (Join-Path $output 'README.txt') -Value $readme -Encoding UTF8
Write-Host "Published assistant and four offline packages under $output"
