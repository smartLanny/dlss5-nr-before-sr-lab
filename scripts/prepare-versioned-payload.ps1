param(
  [Parameter(Mandatory = $true)][string]$Legacy40,
  [Parameter(Mandatory = $true)][string]$Legacy50,
  [Parameter(Mandatory = $true)][string]$Stable40,
  [Parameter(Mandatory = $true)][string]$Stable50,
  [Parameter(Mandatory = $true)][string]$Fix2Zip,
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

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($Fix2Zip)
$entry = $archive.Entries | Where-Object { $_.Name -match '(?i)[.]addon64$' } | Select-Object -First 1
if (-not $entry) { $archive.Dispose(); throw 'Beta0.4.5-FIX2 addon not found in the supplied ZIP.' }
$fix2Temp = Join-Path $payload '_fix2-addon.tmp'
$input = $entry.Open()
$output = [IO.File]::Create($fix2Temp)
$input.CopyTo($output)
$output.Dispose(); $input.Dispose(); $archive.Dispose()

function Copy-Version([string]$id, [string]$source, [string]$addonOverride) {
  $target = Join-Path $versions $id
  New-Item -ItemType Directory -Path $target -Force | Out-Null
  $addon = Get-ChildItem -LiteralPath $source -File | Where-Object { $_.Name -ieq 'nr-before-sr.zh-CN.addon64' } | Select-Object -First 1
  if (-not $addon) {
    $addon = Get-ChildItem -LiteralPath $source -File | Where-Object { $_.Name -match '(?i)[.]addon64$' } | Select-Object -First 1
  }
  if (-not $addon) { throw "No addon64 found in $source" }
  Copy-Item -LiteralPath $addon.FullName -Destination (Join-Path $target 'nr-before-sr.zh-CN.addon64') -Force
  Copy-Item -LiteralPath (Join-Path $source 'nr_before_sr.ini') -Destination (Join-Path $target 'nr_before_sr.ini') -Force
  if ($addonOverride) {
    Copy-Item -LiteralPath $addonOverride -Destination (Join-Path $target 'nr-before-sr.zh-CN.addon64') -Force
  }
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
Copy-Version '0.2.0-beta.2' $Legacy02 $null
Copy-Version '0.3.3.5' $Legacy40 $null
Copy-Version '0.4.5-ota' $Stable40 $fix2Temp
Remove-Item -LiteralPath $fix2Temp -Force

Write-Host "Compact payload prepared under $payload"
