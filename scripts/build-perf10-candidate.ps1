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
# Current source is Perf11; never relabel it as the frozen Perf10 binary.
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build-perf11-candidate.ps1') @PSBoundParameters
