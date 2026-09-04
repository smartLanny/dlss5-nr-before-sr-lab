param(
  [Parameter(Mandatory=$true)][string]$NrBuildDir,
  [Parameter(Mandatory=$true)][string]$NeuralRuntime,
  [Parameter(Mandatory=$true)][string]$ReShadeAddonRuntime
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dest = Join-Path $root 'payload\nr-before-sr'
New-Item -ItemType Directory -Path $dest -Force | Out-Null
$files = @{
  'nr-before-sr.zh-CN.addon64' = Join-Path $NrBuildDir 'nr-before-sr.zh-CN.addon64'
  'nrchain_nvngx.dll' = Join-Path $NrBuildDir 'nrchain_nvngx.dll'
  'nr_before_sr.ini' = Join-Path $NrBuildDir 'nr_before_sr.ini'
  'nvngx_dlssnr.dll' = $NeuralRuntime
  'ReShade64.dll' = $ReShadeAddonRuntime
}
foreach ($entry in $files.GetEnumerator()) {
  if (-not (Test-Path -LiteralPath $entry.Value)) { throw "Missing input: $($entry.Value)" }
  Copy-Item -LiteralPath $entry.Value -Destination (Join-Path $dest $entry.Key) -Force
}
node (Join-Path $PSScriptRoot 'verify-payload.js') --write
if ($LASTEXITCODE -ne 0) { throw 'Payload verification failed.' }
Write-Host "Payload ready: $dest"
