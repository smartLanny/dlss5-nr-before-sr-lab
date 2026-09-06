param(
    [Parameter(Mandatory=$true)][string]$UpstreamRoot,
    [Parameter(Mandatory=$true)][string]$ToolchainRoot,
    [Parameter(Mandatory=$true)][string]$ReShadeSourceRoot,
    [Parameter(Mandatory=$true)][string]$DirectXMathRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Root = Split-Path -Parent $PSScriptRoot
$Upstream = (Resolve-Path -LiteralPath $UpstreamRoot).Path
$Toolchain = (Resolve-Path -LiteralPath $ToolchainRoot).Path
$ReShade = (Resolve-Path -LiteralPath $ReShadeSourceRoot).Path
$Math = (Resolve-Path -LiteralPath $DirectXMathRoot).Path
$Output = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $Output) { throw 'OutputRoot must not exist; previous candidates are never overwritten.' }
function Assert-Commit([string]$Path, [string]$Expected) {
    $Actual = (& git -C $Path rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $Actual -ne $Expected) { throw "Wrong dependency revision: $Path" }
    $Dirty = @(& git -C $Path status --porcelain --untracked-files=no)
    if ($LASTEXITCODE -ne 0 -or $Dirty.Count -ne 0) { throw "Dirty dependency: $Path" }
}
Assert-Commit $Upstream '28aed4099b0fe1c207b20b5fee5364c0773c25c2'
Assert-Commit $ReShade '4a50d1eddace85734871d91792ff214f13f66c01'
Assert-Commit $Math '28929ac6f3c1649a7641741b689ebecccb17aa8d'
$Cxx = Join-Path $Toolchain 'bin/x86_64-w64-mingw32-clang++.exe'
$Windres = Join-Path $Toolchain 'bin/x86_64-w64-mingw32-windres.exe'
foreach ($File in @($Cxx, $Windres, (Join-Path $Math 'Inc/DirectXPackedVector.h'))) {
    if (-not (Test-Path -LiteralPath $File)) { throw "Missing build input: $File" }
}
$Stage = Join-Path $Output 'source'
& python (Join-Path $PSScriptRoot 'prepare-multi-api-beta.py') --core $Root --upstream $Upstream --output $Stage
if ($LASTEXITCODE) { throw 'Full source staging failed.' }
$OldToolchain = $env:DLSS5_LLVM_MINGW_ROOT
$OldCarrier = $env:PINNED_CARRIER_ROOT
$OldSource = $env:MULTIAPI_CORE_SOURCE
$Checks = New-Object 'System.Collections.Generic.List[string]'
Push-Location $Root
try {
    $env:DLSS5_LLVM_MINGW_ROOT = $Toolchain
    $env:PINNED_CARRIER_ROOT = $Upstream
    $env:MULTIAPI_CORE_SOURCE = Join-Path $Root 'src/nr_before_sr.cpp'
    & python -m unittest discover -s tests/multi_api -v
    if ($LASTEXITCODE) { throw 'Multi-API host/source tests failed.' }
    $Checks.Add('multi_api Python source + actual Windows source-identity compile/run')
    & python -m unittest discover -s tests/native_bridge -p 'test_*.py' -v
    if ($LASTEXITCODE) { throw 'Native bridge transformations failed.' }
    $Checks.Add('native_bridge Python exact-source transformations')
    $TestBuild = Join-Path $Output 'tests'
    New-Item -ItemType Directory -Path $TestBuild | Out-Null
    $Include = @('-DNR_TEST_MULTIAPI=1', "-I$(Join-Path $Stage 'core')", "-I$Root", "-I$(Join-Path $Math 'Inc')")
    foreach ($Test in @('multi_api/format_access', 'multi_api/format_adapter', 'native_bridge/history', 'native_bridge/forward', 'native_bridge/render_scale', 'native_bridge/mirror_rect')) {
        $Exe = Join-Path $TestBuild ((Split-Path -Leaf $Test) + '.exe')
        & $Cxx '-std=c++17' '-Wall' '-Wextra' '-Werror' '-static' @Include (Join-Path $Root "tests/$Test.cpp") '-o' $Exe
        if ($LASTEXITCODE) { throw "Host compile failed: $Test" }
        & $Exe
        if ($LASTEXITCODE) { throw "Host test failed: $Test" }
        $Checks.Add($Test)
    }
    foreach ($Test in @('multi_api/format_view_warp', 'native_bridge/baseline_warp')) {
        $Exe = Join-Path $TestBuild ((Split-Path -Leaf $Test) + '.exe')
        & $Cxx '-std=c++17' '-O2' '-static' @Include (Join-Path $Root "tests/$Test.cpp") '-o' $Exe '-ld3d12' '-ldxgi' '-ld3dcompiler' '-ldxguid' '-lole32'
        if ($LASTEXITCODE) { throw "WARP compile failed: $Test" }
        & $Exe
        if ($LASTEXITCODE) { throw "WARP execution failed: $Test" }
        $Checks.Add($Test)
    }
    foreach ($Script in @('validate-picture-budget.py', 'validate-command-creation.py', 'validate-reset-notification-gap.py')) {
        & python (Join-Path $PSScriptRoot $Script) --compiler $Cxx
        if ($LASTEXITCODE) { throw "Production regression failed: $Script" }
        $Checks.Add($Script)
    }
    & (Join-Path $Stage 'core/scripts/build.ps1') -ToolchainRoot $Toolchain -ReShadeSourceRoot $ReShade
    $Checks.Add('staged bilingual core + nrchain compilation and link')
    $CarrierRoot = Join-Path $Stage 'carrier/src'
    $CarrierResource = Join-Path $Output 'carrier-version.o'
    $Carrier = Join-Path $Output 'dlss5-native-carrier-multiapi-dev1.addon64'
    & $Windres '--codepage=65001' '--input-format=rc' '--output-format=coff' (Join-Path $CarrierRoot 'version.rc') $CarrierResource
    if ($LASTEXITCODE) { throw 'Carrier version resource failed.' }
    & $Cxx '-std=c++17' '-O2' '-fms-extensions' '-shared' '-static' "-I$(Join-Path $Math 'Inc')" "-I$(Join-Path $CarrierRoot 'reshade')" (Join-Path $CarrierRoot 'dlss5-bridge.cpp') $CarrierResource '-o' $Carrier '-luser32' '-ladvapi32' '-lbcrypt' '-lversion' '-ldxgi' '-ld3d11' '-ld3d12' '-ld3dcompiler' '-ldxguid'
    if ($LASTEXITCODE) { throw 'Staged native carrier compilation failed.' }
    $Checks.Add('staged carrier compilation + Windows version resource + link')
    $Manifest = Get-Content -LiteralPath (Join-Path $Stage 'source-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach ($Entry in $Manifest.files.PSObject.Properties) {
        $Actual = (Get-FileHash -LiteralPath (Join-Path $Stage $Entry.Name) -Algorithm SHA256).Hash
        if ($Actual -ne $Entry.Value) { throw "Staged source changed during build: $($Entry.Name)" }
    }
    $CoreBuild = Join-Path $Stage 'core/build'
    $Addons = @(Get-ChildItem -LiteralPath $CoreBuild -Filter '*.addon64')
    if ($Addons.Count -ne 2) { throw 'Exactly two language artifacts are required.' }
    $Binaries = @($Addons.FullName) + @((Join-Path $CoreBuild 'nrchain_nvngx.dll'), $Carrier)
    $Hashes = @($Binaries | ForEach-Object { [ordered]@{ file=[IO.Path]::GetFileName($_); sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant() } })
    $CompilerVersion = (& $Cxx --version | Out-String).Trim()
    $Evidence = [ordered]@{
        source_commit=$Manifest.source_commit; generated_source=$true
        bridge_commit='28aed4099b0fe1c207b20b5fee5364c0773c25c2'
        reshade_commit='4a50d1eddace85734871d91792ff214f13f66c01'
        directxmath_commit='28929ac6f3c1649a7641741b689ebecccb17aa8d'
        compiler=$CompilerVersion; powershell=$PSVersionTable.PSVersion.ToString()
        source_verified=$true; compile_verified=$true; warp_verified=$true
        game_runtime_verified=$false; nvidia_feature18_verified=$false
        completed_utc=[DateTime]::UtcNow.ToString('o'); checks=@($Checks.ToArray()); binaries=$Hashes
    }
    $Json = $Evidence | ConvertTo-Json -Depth 6
    [IO.File]::WriteAllText((Join-Path $Output 'validation.json'), $Json, (New-Object Text.UTF8Encoding($false)))
    Write-Host "Exact-source candidate built and WARP-tested: $Output"
    Write-Host 'NVIDIA Feature18 and game runtime are NOT verified by this script.'
} finally {
    Pop-Location
    $env:DLSS5_LLVM_MINGW_ROOT = $OldToolchain
    $env:PINNED_CARRIER_ROOT = $OldCarrier
    $env:MULTIAPI_CORE_SOURCE = $OldSource
}
