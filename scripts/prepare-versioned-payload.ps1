param(
  [Parameter(Mandatory = $true)][string]$Legacy40,
  [Parameter(Mandatory = $true)][string]$Legacy50,
  [Parameter(Mandatory = $true)][string]$Stable40,
  [Parameter(Mandatory = $true)][string]$Stable50,
  [Parameter(Mandatory = $true)][string]$CompatAddon,
  [Parameter(Mandatory = $true)][string]$CompatBridge,
  [Parameter(Mandatory = $true)][string]$CompatCarrier,
  [Parameter(Mandatory = $true)][string]$CompatConfig,
  [Parameter(Mandatory = $true)][string]$ReShade64,
  [Parameter(Mandatory = $true)][string]$Legacy02
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$payload = Join-Path $root 'payload\nr-before-sr'
$versions = Join-Path $payload 'versions'
$fixed = Join-Path $payload 'fixed'
New-Item -ItemType Directory -Path $versions -Force | Out-Null
New-Item -ItemType Directory -Path $fixed -Force | Out-Null

function Copy-Version([string]$id, [string]$source) {
  $target = Join-Path $versions $id
  New-Item -ItemType Directory -Path $target -Force | Out-Null
  $addon = Get-ChildItem -LiteralPath $source -File | Where-Object { $_.Name -ieq 'nr-before-sr.zh-CN.addon64' } | Select-Object -First 1
  if (-not $addon) {
    $addon = Get-ChildItem -LiteralPath $source -File | Where-Object { $_.Name -match '(?i)[.]addon64$' } | Select-Object -First 1
  }
  if (-not $addon) { throw "No addon64 found in $source" }
  Copy-Item -LiteralPath $addon.FullName -Destination (Join-Path $target 'nr-before-sr.zh-CN.addon64') -Force
  Copy-Item -LiteralPath (Join-Path $source 'nr_before_sr.ini') -Destination (Join-Path $target 'nr_before_sr.ini') -Force
}

function Copy-Exact([string]$source, [string]$target) {
  if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing exact compatibility input: $source" }
  Copy-Item -LiteralPath $source -Destination $target -Force
}

function Copy-CompatibilityVersion() {
  $target = Join-Path $versions '0.4.5-ota'
  $oldDx11 = Join-Path $versions '0.4.2-dx11-native-bridge-exp1-r1'
  if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
  if (Test-Path -LiteralPath $oldDx11) { Remove-Item -LiteralPath $oldDx11 -Recurse -Force }
  New-Item -ItemType Directory -Path $target | Out-Null
  Copy-Exact $CompatAddon (Join-Path $target 'nr-before-sr.zh-CN.addon64')
  Copy-Exact $CompatBridge (Join-Path $target 'nrchain_nvngx.dll')
  Copy-Exact $CompatCarrier (Join-Path $target 'dlss5-native-carrier-045-dx11-compat.addon64')
  Copy-Exact $CompatConfig (Join-Path $target 'nr_before_sr.ini')
}

function Copy-Fixed([string]$family, [string]$source) {
  $target = Join-Path $fixed $family
  New-Item -ItemType Directory -Path $target -Force | Out-Null
  Copy-Item -LiteralPath $ReShade64 -Destination (Join-Path $target 'ReShade64.dll') -Force
  foreach ($name in @('nrchain_nvngx.dll', 'nvngx_dlssnr.dll')) {
    Copy-Item -LiteralPath (Join-Path $source $name) -Destination (Join-Path $target $name) -Force
  }
}

Copy-Fixed 'RTX40' $Stable40
Copy-Fixed 'RTX50' $Stable50
Copy-Version '0.2.0-beta.2' $Legacy02
Copy-Version '0.3.3.5' $Legacy40
Copy-CompatibilityVersion

node (Join-Path $PSScriptRoot 'verify-payload.js') --write
if ($LASTEXITCODE -ne 0) { throw 'Versioned payload verification failed.' }
Write-Host "Matched 0.4.5 DX11 compatibility payload prepared under $payload"
