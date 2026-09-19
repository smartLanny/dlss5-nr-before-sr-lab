param(
    [Parameter(Mandatory=$true)][string]$CoreRoot,
    [string]$OutputRoot,
    [string]$LabRoot,
    [string]$ToolchainRoot,
    [string]$ReShadeSourceRoot,
    [string]$UpstreamRoot,
    [string]$CodecCacheRoot,
    [string]$FaceRuntimeRoot,
    [switch]$AllowDescendant,
    [switch]$PrepareOnly,
    [switch]$StageOnly
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$Arguments=@((Join-Path $PSScriptRoot 'tools/prepare_d21.py'),'--core',$CoreRoot)
foreach($Pair in @(@('--output',$OutputRoot),@('--lab-root',$LabRoot),@('--toolchain',$ToolchainRoot),
 @('--reshade',$ReShadeSourceRoot),@('--upstream',$UpstreamRoot),@('--codec-cache',$CodecCacheRoot),@('--face-runtime',$FaceRuntimeRoot))) {
 if(-not [string]::IsNullOrWhiteSpace($Pair[1])){$Arguments+=@($Pair[0],$Pair[1])}
}
if($AllowDescendant){$Arguments+='--allow-descendant'}
if($PrepareOnly){$Arguments+='--prepare-only'}
if($StageOnly){$Arguments+='--stage-only'}
$Python=Get-Command python -ErrorAction SilentlyContinue
if($Python){& $Python.Source @Arguments}
else {
 $Launcher=Get-Command py -ErrorAction SilentlyContinue
 if(-not $Launcher){throw 'Python 3 is required; source has not been changed.'}
 & $Launcher.Source -3 @Arguments
}
if($LASTEXITCODE -ne 0){throw 'D21 candidate failed. Preserve D21-RESULT.json and logs; do not bypass failed source anchors.'}
