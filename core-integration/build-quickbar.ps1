param(
  [switch]$Clean,
  [string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT,
  [string]$ReShadeSourceRoot = $env:DLSS5_RESHADE_ROOT
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
# This adapter has been written for the reviewed beta0.4.1 core, not arbitrary future internals.
$actual = (& git -C $root hash-object src/nr_before_sr.cpp).Trim()
if ($LASTEXITCODE -ne 0 -or $actual -ne '8c0839400dee2dd3bd7069c6e0c62c33eca1cd17') {
  throw 'Core source changed: revalidate the Quickbar C ABI adapter before building.'
}
$build = Join-Path $PSScriptRoot 'build.ps1'
$text = Get-Content -LiteralPath $build -Raw
$old = '$AddonSource = Join-Path $ProjectRoot ''src\nr_before_sr.cpp'''
$new = '$AddonSource = Join-Path $ProjectRoot ''src\nr_before_sr_quickbar.cpp'''
if (($text.Split(@($old), [StringSplitOptions]::None)).Count -ne 2) { throw 'Build script contract changed.' }
$temp = Join-Path $PSScriptRoot ('.quickbar-build-' + [guid]::NewGuid().ToString() + '.ps1')
try {
  [IO.File]::WriteAllText($temp, $text.Replace($old, $new), [Text.UTF8Encoding]::new($true))
  & $temp -Clean:$Clean -ToolchainRoot $ToolchainRoot -ReShadeSourceRoot $ReShadeSourceRoot
  if ($LASTEXITCODE -ne 0) { throw 'Core Quickbar build failed.' }
} finally { Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue }
