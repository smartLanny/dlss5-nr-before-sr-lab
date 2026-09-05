param(
    [Parameter(Mandatory = $true)][string]$ToolchainRoot,
    [Parameter(Mandatory = $true)][string]$ReShadeSourceRoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
$compiler = Join-Path (Resolve-Path -LiteralPath $ToolchainRoot).Path 'bin\x86_64-w64-mingw32-clang++.exe'
$reshade = (Resolve-Path -LiteralPath $ReShadeSourceRoot).Path
$output = Join-Path $root 'build\analysis'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$common = @('--analyze', '-std=c++20', '-DUNICODE', '-D_UNICODE', '-D_WIN64',
    '-Wno-ignored-attributes', '-Wno-unknown-attributes',
    "-I$(Join-Path $root 'third_party\minhook\include')",
    "-I$(Join-Path $reshade 'include')", "-I$(Join-Path $reshade 'deps\imgui')",
    '-Xanalyzer', '-analyzer-output=plist')
$targets = @(
    @{ Name='English'; Source='nr_before_sr.cpp'; Define='-DNR_UI_CHINESE=0' },
    @{ Name='Chinese'; Source='nr_before_sr.cpp'; Define='-DNR_UI_CHINESE=1' },
    @{ Name='Bridge'; Source='nrchain_nvngx.cpp'; Define='-DNR_UI_CHINESE=0' }
)
foreach ($target in $targets) {
    $report = Join-Path $output ($target.Name + '.plist')
    & $compiler @common $target.Define (Join-Path $root ('src\' + $target.Source)) '-o' $report
    if ($LASTEXITCODE -ne 0) { throw "Analyzer failed: $($target.Name)" }
    [xml]$document = Get-Content -LiteralPath $report -Raw
    $diagnostics = $document.SelectSingleNode("//key[text()='diagnostics']/following-sibling::array[1]")
    if ($null -eq $diagnostics -or $diagnostics.ChildNodes.Count -ne 0) {
        throw "Analyzer diagnostics require review: $report"
    }
    Write-Host "$($target.Name): analyzer PASS (zero diagnostics)"
}
