param(
  [Parameter(Mandatory = $true)][string]$DeliveryDir
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
New-Item -ItemType Directory -Path $DeliveryDir -Force | Out-Null

$setup = Join-Path $root 'dist\DLSS5-Manager-Setup-0.2.0-beta.2.exe'
$portable = Join-Path $root 'dist\DLSS5-Manager-0.2.0-beta.2-portable.exe'
Copy-Item -LiteralPath $setup -Destination $DeliveryDir -Force
Copy-Item -LiteralPath $portable -Destination $DeliveryDir -Force
Copy-Item -LiteralPath (Join-Path $root 'docs\INSTALL-GUIDE-0.2.0-BETA.md') -Destination (Join-Path $DeliveryDir '安装说明-装机宅版Beta.md') -Force

$items = Get-ChildItem -LiteralPath $DeliveryDir -File | Where-Object { $_.Extension -in @('.exe', '.md', '.txt') } | ForEach-Object {
  [ordered]@{
    name = $_.Name
    size = $_.Length
    sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
  }
}
[ordered]@{
  product = 'dlss5-manager-装机宅版'
  version = '0.2.0-beta.2'
  branch = 'product/xiaofeng-manager-foundation'
  baseCommit = '38db68aedad03806816f52c541d036e8bf6d847a'
  addonVersions = @('0.2.0-beta.2', '0.3.3.5', '0.4.1-r2', '0.4.4test', '0.4.5-ota', '0.4.2-dx11-native-bridge-exp1-r1')
  defaultAddonVersion = '0.3.3.5'
  note = '默认使用 0.3.3.5 稳定槽；Beta0.4 及以后优先使用标准 OTA 导入/升级，Beta0.4.5 只替换 Addon 并保留用户配置；DX11 默认使用 Beta0.4.2 native bridge 兼容测试槽，可能有问题请进群联系 @群RBQ 提交报告；旧 Beta0.4 槽保留用于 A/B 对比；0.3.3.4 的精确二进制未在本地资产中找到，未冒充命名。'
  files = @($items)
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $DeliveryDir 'RELEASE-CONTENTS.json') -Encoding UTF8

Get-ChildItem -LiteralPath $DeliveryDir -File | Where-Object { $_.Extension -in @('.exe', '.md', '.json') } | ForEach-Object {
  "$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $($_.Name)"
} | Set-Content -LiteralPath (Join-Path $DeliveryDir 'SHA256SUMS.txt') -Encoding UTF8
Write-Host "Delivery ready: $DeliveryDir"
