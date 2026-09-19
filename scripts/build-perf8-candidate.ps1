# Historical entry forwards; do not label current source as Perf8.
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
Set-StrictMode -Version Latest
& (Join-Path $PSScriptRoot 'build-perf9-candidate.ps1') @PSBoundParameters
