param(
  [Parameter(Mandatory = $true)]
  [string] $ReleaseZip,
  [Parameter(Mandatory = $true)]
  [string] $OutputDir
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$chainSource = Join-Path $repoRoot 'payload\nr-before-sr\fixed\RTX40\nrchain_nvngx.dll'
$instructionsSource = Join-Path $repoRoot 'docs\OTA-Beta0.3.8-测试说明.txt'
$addonEntryName = 'DLSS5-AI渲染超分版-beta0.3.8-@野生的装机宅-Bilibili.addon64'
$stage = Join-Path $OutputDir 'stage-beta038'
$zipOutput = Join-Path $OutputDir 'DLSS5-Beta0.3.8-Chinese-OTA.zip'

foreach ($required in @($ReleaseZip, $chainSource, $instructionsSource)) {
  if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
    throw "Required file not found: $required"
  }
}

New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
New-Item -ItemType Directory -Path $stage -Force | Out-Null

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $ReleaseZip))
try {
  $entry = $archive.GetEntry($addonEntryName)
  if (-not $entry) { throw "The Chinese Beta0.3.8 addon was not found in $ReleaseZip" }
  $addonPath = Join-Path $stage $addonEntryName
  $inputStream = $entry.Open()
  $outputStream = [System.IO.File]::Create($addonPath)
  try { $inputStream.CopyTo($outputStream) }
  finally {
    $outputStream.Dispose()
    $inputStream.Dispose()
  }
}
finally {
  $archive.Dispose()
}

Copy-Item -LiteralPath $chainSource -Destination (Join-Path $stage 'nrchain_nvngx.dll') -Force
Copy-Item -LiteralPath $instructionsSource -Destination (Join-Path $stage 'Instructions.txt') -Force

$payloadNames = @($addonEntryName, 'nrchain_nvngx.dll')
$fileRows = foreach ($name in $payloadNames) {
  $file = Get-Item -LiteralPath (Join-Path $stage $name)
  [ordered]@{
    name = $name
    bytes = [int64]$file.Length
    sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
  }
}

$manifest = [ordered]@{
  schema = 'nr-branch-ota-v1'
  version = 'Beta0.3.8'
  language = 'Chinese'
  api = 'D3D12-x64'
  sourceCommit = 'f4d0657ad2ddcb82b8af3b0edc6c67ecf9109934'
  sourceDirty = $false
  includesDx11 = $false
  channel = 'maintenance-line-test'
  preserve = @('nr_before_sr.ini', 'ReShade.ini', 'ReShade64.dll', 'nvngx_dlss.dll', 'nvngx_dlssg.dll')
  files = @($fileRows)
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $stage 'ota-manifest.json') -Encoding utf8

Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zipOutput -CompressionLevel Optimal -Force

Write-Output "Created: $zipOutput"
Get-FileHash -LiteralPath $zipOutput -Algorithm SHA256 | ForEach-Object {
  Write-Output "SHA256: $($_.Hash.ToLowerInvariant())"
}
