param(
  [Parameter(Mandatory=$true)][string]$AddonFile,
  [Parameter(Mandatory=$true)][string]$BridgeFile,
  [Parameter(Mandatory=$true)][string]$ConfigFile,
  [Parameter(Mandatory=$true)][string]$NeuralRuntime,
  [Parameter(Mandatory=$true)][string]$ReShadeAddonRuntime,
  [string]$QuickbarFile = ''
)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$dest=Join-Path $root 'payload\nr-before-sr'
$files=@{
 'nr-before-sr.zh-CN.addon64'=$AddonFile
 'nrchain_nvngx.dll'=$BridgeFile
 'nr_before_sr.ini'=$ConfigFile
 'nvngx_dlssnr.dll'=$NeuralRuntime
 'ReShade64.dll'=$ReShadeAddonRuntime
}
if ($QuickbarFile) {
 $files['zjz-quickbar.addon64']=$QuickbarFile
 # Sanity check only, not publisher authentication or proof of ABI behavior.
 $symbols=[Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Resolve-Path $AddonFile)))
 foreach($name in @('NRUI_GetStateV1','NRUI_SetValueV1')) {
  if(-not $symbols.Contains($name)){throw 'The selected core is not the companion control-API build.'}
 }
}
foreach($src in $files.Values){if(-not(Test-Path -LiteralPath $src -PathType Leaf)){throw "Missing file: $src"}}
New-Item -ItemType Directory -Path $dest -Force | Out-Null
if(-not $QuickbarFile -and (Test-Path (Join-Path $dest 'zjz-quickbar.addon64'))){throw 'Stale Quickbar payload exists. Use a clean payload directory.'}
foreach($entry in $files.GetEnumerator()){Copy-Item -LiteralPath $entry.Value -Destination (Join-Path $dest $entry.Key) -Force}
if($QuickbarFile){[IO.File]::WriteAllText((Join-Path $dest 'nr_quickbar.ini'),"[Quickbar]`r`nKeyToggle=121`r`nPinned=0`r`nOpacityPercent=86`r`nScalePercent=100`r`n",[Text.UTF8Encoding]::new($false))}
node (Join-Path $PSScriptRoot 'verify-payload.js') --write
if($LASTEXITCODE -ne 0){throw 'Payload verification failed.'}
