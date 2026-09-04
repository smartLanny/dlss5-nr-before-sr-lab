param(
    [string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT,
    [string]$ReShadeSourceRoot = $env:DLSS5_RESHADE_ROOT
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$source = Join-Path $project 'src\nr_f8_sequence.inl'
$main = Join-Path $project 'src\nr_before_sr.cpp'
$feedback = Join-Path $project 'src\nr_feedback_capture.inl'
$text = Get-Content -LiteralPath $source -Raw
$mainText = Get-Content -LiteralPath $main -Raw
$feedbackText = Get-Content -LiteralPath $feedback -Raw
foreach ($needle in @(
    'kGroupCount = 3', 'kFrameCount = 16', 'kCountdownMs = 3000',
    'sequence restarted', 'nativeSrOutput=unknown', 'config.writesToIni=0',
    'regionProtection.effective=',
    'planeData=original,proxy,nr,composite RGBA32F ROI tiles', 'QueueUserWorkItem(&Worker',
    'ApplyConfig(session->original, "F8 restore", false)')) {
    if (-not $text.Contains($needle)) { throw "F8 contract missing: $needle" }
}
foreach ($needle in @(
    '#include "nr_f8_sequence.inl"',
    'nr_f8::RequestWithOptions', 'nr_f8::Present(runtime)',
    'nr_f8::ObserveSuccess')) {
    if (-not $mainText.Contains($needle)) { throw "F8 integration missing: $needle" }
}
if (-not $feedbackText.Contains('nr_f8::ObserveParams') -or
    -not $feedbackText.Contains('nr_f8::Screenshot')) {
    throw 'F8 callback integration missing.'
}
if ([string]::IsNullOrWhiteSpace($ToolchainRoot) -or
    [string]::IsNullOrWhiteSpace($ReShadeSourceRoot)) {
    Write-Host 'F8 contract checks passed; syntax compile skipped (toolchain or ReShade root not supplied).'
    exit 0
}
$compiler = Join-Path $ToolchainRoot 'bin\x86_64-w64-mingw32-clang++.exe'
& $compiler -std=c++20 -DNR_UI_CHINESE=1 `
    "-I$(Join-Path $ReShadeSourceRoot 'include')" `
    "-I$(Join-Path $ReShadeSourceRoot 'deps\imgui')" `
    "-I$(Join-Path $project 'third_party\minhook\include')" `
    -fsyntax-only $main
if ($LASTEXITCODE -ne 0) { throw 'F8 integration syntax compile failed.' }
Write-Host 'F8 sequence contract and integration checks passed.'
