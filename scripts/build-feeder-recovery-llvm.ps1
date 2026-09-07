param(
    [Parameter(Mandatory=$true)][string]$FeederStageRoot,
    [Parameter(Mandatory=$true)][string]$ToolchainRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Stage = (Resolve-Path -LiteralPath $FeederStageRoot).Path
$Toolchain = (Resolve-Path -LiteralPath $ToolchainRoot).Path
$Output = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $Output) { throw 'OutputRoot must be new; previous evidence is retained.' }
$Cxx = Join-Path $Toolchain 'bin/x86_64-w64-mingw32-clang++.exe'
$Cc = Join-Path $Toolchain 'bin/x86_64-w64-mingw32-clang.exe'
$Rc = Join-Path $Toolchain 'bin/x86_64-w64-mingw32-windres.exe'
foreach ($file in @('src/dlss5-feed.cpp', 'nr_lab_interop/nr_external_provider_producer.h',
    'external/ngx/nvsdk_ngx.h', 'external/ngx/libs/nvsdk_ngx_d.lib',
    'external/vulkan/vulkan/vulkan.h')) {
    if (!(Test-Path -LiteralPath (Join-Path $Stage $file))) { throw "Missing real build input: $file" }
}
New-Item -ItemType Directory -Path $Output | Out-Null
$Includes = @('nr_lab_interop', 'external/reshade/include', 'external/ngx',
    'external/vulkan', 'external/imgui', 'external/minhook/include') | ForEach-Object { "-I$(Join-Path $Stage $_)" }
$Objects = @()
foreach ($file in @('buffer', 'hook', 'trampoline', 'hde/hde64')) {
    $obj = Join-Path $Output ((Split-Path -Leaf $file) + '.o')
    & $Cc '-O2' @Includes '-c' (Join-Path $Stage "external/minhook/src/$file.c") '-o' $obj
    if ($LASTEXITCODE) { throw "MinHook compilation failed: $file" }
    $Objects += $obj
}
$FeederObject = Join-Path $Output 'dlss5-feed.o'
& $Cxx '-std=c++20' '-fms-extensions' '-O2' @Includes '-c' (Join-Path $Stage 'src/dlss5-feed.cpp') '-o' $FeederObject 2>&1 |
    Tee-Object -FilePath (Join-Path $Output 'compile.log')
if ($LASTEXITCODE) { throw 'Full Feeder translation unit failed to compile.' }
$Resource = Join-Path $Output 'version.o'
& $Rc '--codepage=65001' '--input-format=rc' '--output-format=coff' (Join-Path $Stage 'src/version.rc') $Resource
if ($LASTEXITCODE) { throw 'Feeder Windows resource compilation failed.' }
& $Cxx '-shared' '-static' $FeederObject @Objects $Resource (Join-Path $Stage 'external/ngx/libs/nvsdk_ngx_d.lib') `
    '-o' (Join-Path $Output 'dlss5-feed-external-development.addon64') '-lversion' '-lkernel32' '-luser32' '-ladvapi32' `
    '-lole32' '-ldxgi' '-ld3d11' '-ld3d12' '-ld3dcompiler' '-ldxguid' '-lsetupapi' '-lshlwapi' 2>&1 |
    Tee-Object -FilePath (Join-Path $Output 'link.log')
if ($LASTEXITCODE) { throw 'Full Feeder link failed; see link.log. Do not publish the object as a working add-on.' }
Write-Output 'Full Feeder compile/link completed; this is a development binary, not GPU/game acceptance.'
