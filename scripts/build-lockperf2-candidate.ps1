# Historical command alias. Candidate identity is owned by the successor builder.
param(
    [Parameter(Mandatory=$true)][string]$UpstreamRoot,
    [Parameter(Mandatory=$true)][string]$ToolchainRoot,
    [Parameter(Mandatory=$true)][string]$ReShadeSourceRoot,
    [Parameter(Mandatory=$true)][string]$DirectXMathRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [string]$NrchainReuseRoot,
    [switch]$AllowMissingDebugLayer
)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build-perf3-candidate.ps1') @PSBoundParameters
