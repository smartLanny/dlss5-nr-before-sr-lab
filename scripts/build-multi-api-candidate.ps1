param(
    [Parameter(Mandatory=$true)][string]$UpstreamRoot,
    [Parameter(Mandatory=$true)][string]$ToolchainRoot,
    [Parameter(Mandatory=$true)][string]$ReShadeSourceRoot,
    [Parameter(Mandatory=$true)][string]$DirectXMathRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [string]$NrchainReuseRoot,
    [string]$CodecCacheRoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Root = Split-Path -Parent $PSScriptRoot
$Upstream = (Resolve-Path -LiteralPath $UpstreamRoot).Path
$Toolchain = (Resolve-Path -LiteralPath $ToolchainRoot).Path
$ReShade = (Resolve-Path -LiteralPath $ReShadeSourceRoot).Path
$Math = (Resolve-Path -LiteralPath $DirectXMathRoot).Path
$Output = [IO.Path]::GetFullPath($OutputRoot)
$CodecCache = if ($CodecCacheRoot) { (Resolve-Path -LiteralPath $CodecCacheRoot).Path } else { '' }
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
$OldPostSource = $env:POST_COMPAT_CORE_SOURCE
$OldCompletionSource = $env:CARRIER_COMPLETION_SOURCE
$OldCompletionPatched = $env:CARRIER_COMPLETION_ALREADY_PATCHED
$OldCxx = $env:CXX
$Checks = New-Object 'System.Collections.Generic.List[string]'
Push-Location $Root
try {
    $env:DLSS5_LLVM_MINGW_ROOT = $Toolchain
    $env:PINNED_CARRIER_ROOT = $Upstream
    $env:MULTIAPI_CORE_SOURCE = Join-Path $Root 'src/nr_before_sr.cpp'
    $env:POST_COMPAT_CORE_SOURCE = Join-Path $Root 'src/nr_before_sr.cpp'
    $env:CARRIER_COMPLETION_SOURCE = $Stage
    $env:CARRIER_COMPLETION_ALREADY_PATCHED = '1'
    $env:CXX = $Cxx
    & python -m unittest discover -s tests/multi_api -v
    if ($LASTEXITCODE) { throw 'Multi-API host/source tests failed.' }
    $Checks.Add('multi_api Python source + actual Windows source-identity compile/run')
    & python -m unittest discover -s tests/native_bridge -p 'test_*.py' -v
    if ($LASTEXITCODE) { throw 'Native bridge transformations failed.' }
    $Checks.Add('native_bridge Python exact-source transformations')
    & python -m unittest tests.test_ui_refresh tests.test_report_clipboard tests.test_f8_parser tests.test_rr_route_recovery -v
    if ($LASTEXITCODE) { throw 'UI, clipboard or F7 parser regression failed.' }
    $Checks.Add('settings UI, clipboard ownership/retry, 512 ROI parser, actual RR recovery admission/dispatch/Win32 gate')
    $Checks.Add('carrier completion: staged production state machine, WARP fence, real access-violation propagation')
    $TestBuild = Join-Path $Output 'tests'
    New-Item -ItemType Directory -Path $TestBuild | Out-Null
    & (Join-Path $PSScriptRoot 'validate-runtime-load-windows.ps1') -ToolchainRoot $Toolchain `
        -SourceRoot (Join-Path $Stage 'core') -FixtureRoot $Root `
        -OutputRoot (Join-Path $TestBuild 'runtime-load-windows')
    $Checks.Add('runtime load Windows fixture: file-absent vs dependency-missing under restricted LoadLibraryExW')
    $Include = @('-DNR_TEST_MULTIAPI=1', "-I$(Join-Path $Stage 'core')", "-I$Root", "-I$(Join-Path $Math 'Inc')")
    foreach ($Test in @('multi_api/format_access', 'multi_api/format_adapter', 'multi_api/post_policy', 'multi_api/rr_post_runtime', 'multi_api/post_settings', 'multi_api/motion_access', 'multi_api/exposure_access', 'native_bridge/history', 'native_bridge/forward', 'native_bridge/render_scale', 'native_bridge/mirror_rect', 'native_bridge/ngx_parameter_abi')) {
        $Exe = Join-Path $TestBuild ((Split-Path -Leaf $Test) + '.exe')
        & $Cxx '-std=c++17' '-Wall' '-Wextra' '-Werror' '-static' @Include (Join-Path $Root "tests/$Test.cpp") '-o' $Exe
        if ($LASTEXITCODE) { throw "Host compile failed: $Test" }
        & $Exe
        if ($LASTEXITCODE) { throw "Host test failed: $Test" }
        $Checks.Add($Test)
    }
    foreach ($PolicyTest in @('feedback_text_test', 'single_text_policy_test')) {
        $PolicyExe = Join-Path $TestBuild ($PolicyTest + '.exe')
        & $Cxx '-std=c++20' '-Wall' '-Wextra' '-Werror' '-static' "-I$(Join-Path $Stage 'core/src')" (Join-Path $Root "tests/$PolicyTest.cpp") '-o' $PolicyExe
        if ($LASTEXITCODE) { throw "Feedback policy compile failed: $PolicyTest" }
        & $PolicyExe
        if ($LASTEXITCODE) { throw "Feedback policy regression failed: $PolicyTest" }
        $Checks.Add("staged feedback policy: $PolicyTest")
    }
    $OldAbi = Join-Path $TestBuild 'ngx_parameter_abi-old.exe'
    & $Cxx '-std=c++17' '-O2' '-static' '-DNR_TEST_OLD_VTABLE=1' @Include (Join-Path $Root 'tests/native_bridge/ngx_parameter_abi.cpp') '-o' $OldAbi
    if ($LASTEXITCODE) { throw 'Historical ABI negative control failed to compile.' }
    & $OldAbi
    if ($LASTEXITCODE -ne 1) { throw 'Historical ABI mismatch was not detected by executable slot oracle.' }
    $Checks.Add('NGX ABI historical-layout negative control rejected at first wrong slot')
    $ShaderCompiler = Join-Path $TestBuild 'post_shader_compile.exe'
    & $Cxx '-std=c++17' '-static' '-municode' (Join-Path $Root 'tests/multi_api/post_shader_compile.cpp') '-o' $ShaderCompiler '-ld3dcompiler'
    if ($LASTEXITCODE) { throw 'Production shader compiler harness failed.' }
    & python (Join-Path $PSScriptRoot 'validate-post-shaders.py') --source (Join-Path $Stage 'core/src/nr_before_sr.cpp') --compiler $ShaderCompiler
    if ($LASTEXITCODE) { throw 'Actual generated production HLSL failed.' }
    $Checks.Add('all six actual staged production HLSL compute entries')
    foreach ($Test in @('multi_api/format_view_warp', 'native_bridge/baseline_warp')) {
        $Exe = Join-Path $TestBuild ((Split-Path -Leaf $Test) + '.exe')
        & $Cxx '-std=c++17' '-O2' '-static' @Include (Join-Path $Root "tests/$Test.cpp") '-o' $Exe '-ld3d12' '-ldxgi' '-ld3dcompiler' '-ldxguid' '-lole32'
        if ($LASTEXITCODE) { throw "WARP compile failed: $Test" }
        & $Exe
        if ($LASTEXITCODE) { throw "WARP execution failed: $Test" }
        $Checks.Add($Test)
    }
    & (Join-Path $PSScriptRoot 'validate-post-scaled-work-gpu.ps1') -SourceRoot (Join-Path $Stage 'core') -ToolchainRoot $Toolchain -OutputRoot (Join-Path $TestBuild 'post-scaled-work')
    $Checks.Add('post scaled-work WARP: 48 actual EncodeCS/CombineCS scenarios plus production CombineV2CS LightingLock flat/strong-luma/equal-luma-colour contracts; FP16 model planes; test-only neural input')
    & python (Join-Path $PSScriptRoot 'validate-colour-defaults.py') --compiler $Cxx --root (Join-Path $Stage 'core')
    if ($LASTEXITCODE) { throw 'Actual staged colour defaults/model reset failed.' }
    $Checks.Add('actual staged colour defaults and model-only reset: 165 checks')
    & python (Join-Path $PSScriptRoot 'validate-rr-retirement.py') --compiler $Cxx --source (Join-Path $Stage 'core/src/nr_before_sr.cpp')
    if ($LASTEXITCODE) { throw 'Actual staged RR release retirement failed.' }
    & python (Join-Path $PSScriptRoot 'validate-rr-retirement.py') --compiler $Cxx --source (Join-Path $Stage 'core/src/nr_before_sr.cpp') --negative-control
    if ($LASTEXITCODE) { throw 'RR historical leak control failed.' }
    $Checks.Add('actual staged RR Release hooks: 54 scenarios and historical leak negative control')
    foreach ($Script in @('validate-picture-budget.py', 'validate-color-policy.py', 'validate-command-creation.py', 'validate-reset-notification-gap.py', 'validate-core-feedback.py', 'validate-feedback-hotkeys.py', 'validate-f8-controller.py', 'validate-recommended-picture.py')) {
        & python (Join-Path $PSScriptRoot $Script) --compiler $Cxx
        if ($LASTEXITCODE) { throw "Production regression failed: $Script" }
        $Checks.Add($Script)
    }
    & python (Join-Path $PSScriptRoot 'validate-f8-controller.py') --gpu --compiler $Cxx
    if ($LASTEXITCODE) { throw '512 ROI production F7 GPU/stream validation failed.' }
    $Checks.Add('F7 512 ROI production shader/ring/fence/stream/ZIP WARP: 3 ROI frames plus overview')
    & (Join-Path $PSScriptRoot 'validate-command-state-native.ps1') -ToolchainRoot $Toolchain
    $Checks.Add('native command state, observed blend history, 64KiB worker and stack gate')
    & (Join-Path $PSScriptRoot 'validate-command-state-gpu.ps1') -ToolchainRoot $Toolchain
    & (Join-Path $PSScriptRoot 'validate-command-state-gpu.ps1') -ToolchainRoot $Toolchain -BlendStateNegativeControl
    $Checks.Add('command-state WARP including actual blend restore and wrong-blend negative control')
    foreach ($ResearchGate in @('validate-beta05-boost.py', 'validate-layered-composite.py', 'validate-050-stability.py', 'validate-bug-sweep.py')) {
        & python (Join-Path $PSScriptRoot $ResearchGate) --compiler $Cxx
        if ($LASTEXITCODE) { throw "0.5 research regression failed: $ResearchGate" }
        $Checks.Add("0.5 research: $ResearchGate")
    }
    & python (Join-Path $PSScriptRoot 'validate-050-stability.py') --compiler $Cxx --negative-control
    if ($LASTEXITCODE) { throw '0.5 frozen picture negative control failed.' }
    $Checks.Add('0.5 frozen picture serialization negative control')
    & (Join-Path $PSScriptRoot 'validate-codec-gpu.ps1') -ToolchainRoot $Toolchain -SourceRoot (Join-Path $Stage 'core')
    & (Join-Path $PSScriptRoot 'validate-codec-gpu.ps1') -ToolchainRoot $Toolchain -SourceRoot (Join-Path $Stage 'core') -NegativeControl
    & (Join-Path $PSScriptRoot 'validate-codec-gpu.ps1') -ToolchainRoot $Toolchain -SourceRoot (Join-Path $Stage 'core') -LayerNegativeControl
    $Checks.Add('0.5 actual generated HLSL WARP high-strength/layered/maintenance lighting, 60-root ABI and independent negative controls')
    $StaticArgs = @{}
    if ($NrchainReuseRoot) { $StaticArgs['CoreOnly'] = $true }
    & (Join-Path $PSScriptRoot 'validate-static-analysis.ps1') -ToolchainRoot $Toolchain -ReShadeSourceRoot $ReShade -SourceRoot (Join-Path $Stage 'core') @StaticArgs
    if ($NrchainReuseRoot) {
        $Checks.Add('actual staged bilingual core LLVM static analysis: zero diagnostics; exact-source nrchain verified from signed SDK build receipt')
    } else {
        $Checks.Add('actual staged bilingual core and nrchain LLVM static analysis: zero diagnostics')
    }
    $CodecArgs = @{}
    if ($CodecCache) { $CodecArgs['CodecCacheRoot'] = $CodecCache }
    # Existing codec builder verifies exact source/options, compiler bytes and
    # every cached DXBC hash. This does not skip any runtime/WARP/static gate.
    & (Join-Path $Stage 'core/scripts/build.ps1') -ToolchainRoot $Toolchain -ReShadeSourceRoot $ReShade -NrchainReuseRoot $NrchainReuseRoot @CodecArgs
    if ($NrchainReuseRoot) {
        $Checks.Add('staged bilingual core compilation/link + exact-source verified nrchain reuse')
    } else {
        $Checks.Add('staged bilingual core + nrchain compilation and link')
    }
    $CarrierRoot = Join-Path $Stage 'carrier/src'
    $CarrierResource = Join-Path $Output 'carrier-version.o'
    $Manifest = Get-Content -LiteralPath (Join-Path $Stage 'source-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $CarrierLeaf = $Manifest.integration.carrier_filename
    if ([IO.Path]::GetFileName($CarrierLeaf) -ne $CarrierLeaf -or $CarrierLeaf -notmatch '^dlss5-native-carrier-[a-z0-9-]+\.addon64$') { throw 'Invalid staged carrier filename.' }
    $Carrier = Join-Path $Output $CarrierLeaf
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
        nrchain_reused=[bool]$NrchainReuseRoot
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
    $env:POST_COMPAT_CORE_SOURCE = $OldPostSource
    $env:CARRIER_COMPLETION_SOURCE = $OldCompletionSource
    $env:CARRIER_COMPLETION_ALREADY_PATCHED = $OldCompletionPatched
    $env:CXX = $OldCxx
}
