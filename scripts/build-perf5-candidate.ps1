# Historical entry point: source now produces Perf6; never relabel new bytes as Perf5.
param(
    [Parameter(Mandatory=$true)][string]$UpstreamRoot,
    [Parameter(Mandatory=$true)][string]$ToolchainRoot,
    [Parameter(Mandatory=$true)][string]$ReShadeSourceRoot,
    [Parameter(Mandatory=$true)][string]$DirectXMathRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [string]$NrchainReuseRoot,
    [string]$CodecCacheRoot,
    [switch]$AllowMissingDebugLayer
)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build-perf6-candidate.ps1') @PSBoundParameters
