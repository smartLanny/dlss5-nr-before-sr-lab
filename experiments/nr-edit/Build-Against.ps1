param(
    [string]$CoreRoot,
    [string]$OutputRoot,
    [string]$ToolchainRoot,
    [string]$ReShadeSourceRoot,
    [string]$UpstreamRoot,
    [string]$CodecCacheRoot,
    [string]$FaceRuntimeRoot,
    [switch]$StageOnly,
    [switch]$AllowDirtySource
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$Arguments=@((Join-Path $PSScriptRoot 'tools/build_against.py'))
foreach($Item in @(
    @('--core',$CoreRoot),@('--output',$OutputRoot),@('--toolchain',$ToolchainRoot),
    @('--reshade',$ReShadeSourceRoot),@('--upstream',$UpstreamRoot),
    @('--codec-cache',$CodecCacheRoot),@('--face-runtime',$FaceRuntimeRoot))) {
    if(-not [string]::IsNullOrWhiteSpace($Item[1])){$Arguments+=@($Item[0],$Item[1])}
}
if($StageOnly){$Arguments+='--stage-only'}
if($AllowDirtySource){$Arguments+='--allow-dirty-source'}
& python @Arguments
if($LASTEXITCODE -ne 0){throw 'Edit candidate failed. Read validation.json and logs in the new output directory. Do not bypass failing source anchors.'}
