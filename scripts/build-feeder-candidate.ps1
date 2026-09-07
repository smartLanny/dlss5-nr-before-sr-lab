param(
    [Parameter(Mandatory=$true)][string]$FeederRoot,
    [Parameter(Mandatory=$true)][string]$NgxSdkRoot,
    [Parameter(Mandatory=$true)][string]$VulkanHeadersRoot,
    [Parameter(Mandatory=$true)][string]$ToolchainRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [switch]$Development
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Root = Split-Path -Parent $PSScriptRoot
$Output = [IO.Path]::GetFullPath($OutputRoot)
$Cxx = Join-Path $ToolchainRoot 'bin/x86_64-w64-mingw32-clang++.exe'
$Cc = Join-Path $ToolchainRoot 'bin/x86_64-w64-mingw32-clang.exe'
$ReadObj = Join-Path $ToolchainRoot 'bin/llvm-readobj.exe'
foreach ($Path in @($Cxx, $Cc, $ReadObj)) {
    if (-not (Test-Path -LiteralPath $Path)) { throw "Missing compiler tool: $Path" }
}
$StageArgs = @('--core', $Root, '--feeder', $FeederRoot, '--sdk', $NgxSdkRoot,
    '--vulkan', $VulkanHeadersRoot, '--output', $Output)
if ($Development) { $StageArgs += '--development' }
& python (Join-Path $PSScriptRoot 'prepare-feeder-candidate.py') @StageArgs
if ($LASTEXITCODE) { throw 'Pinned Feeder staging failed.' }
$Includes = @(
    "-I$(Join-Path $Output 'external/reshade/include')",
    "-I$(Join-Path $Output 'external/imgui')",
    "-I$(Join-Path $Output 'external/minhook/include')",
    "-I$(Join-Path $Output 'sdk/include')",
    "-I$(Join-Path $Output 'vulkan/include')"
)
$Objects = @()
foreach ($Source in @('buffer', 'hook', 'trampoline', 'hde/hde64')) {
    $Object = Join-Path $Output ((Split-Path -Leaf $Source) + '.o')
    & $Cc '-O2' @Includes '-c' (Join-Path $Output "external/minhook/src/$Source.c") '-o' $Object
    if ($LASTEXITCODE) { throw "Feeder transport C compilation failed: $Source" }
    $Objects += $Object
}
$Addon = Join-Path $Output 'dlss5-nr-feeder-multiapi-dev1.addon64'
# Export only explicit ReShade addon entry points. Auto-exporting our private
# SDK convenience functions would make the core mistake this addon for NGX.
& $Cxx '-std=c++20' '-O2' '-fms-extensions' '-shared' '-static' '-Wl,--exclude-all-symbols' @Includes (Join-Path $Output 'src/dlss5-feed.cpp') @Objects '-o' $Addon '-luser32' '-ladvapi32' '-lole32' '-lversion' '-ldxguid' '-ld3d12' '-ld3d11' '-ldxgi' '-ld3dcompiler'
if ($LASTEXITCODE) { throw 'Actual Feeder producer compile/link failed.' }
$Exports = (& $ReadObj '--coff-exports' $Addon | Out-String)
if ($LASTEXITCODE -or $Exports -match 'Name: NVSDK_NGX_' -or $Exports -notmatch 'Name: NAME') {
    throw 'Producer exports are incorrect: private NGX helpers must not be exported.'
}
$Imports = (& $ReadObj '--coff-imports' $Addon | Out-String)
if ($LASTEXITCODE -or $Imports -match '(?i)nvsdk_ngx|vcruntime|msvcp') {
    throw 'Unexpected static SDK / Microsoft C++ runtime dependency.'
}
$Manifest = Get-Content -LiteralPath (Join-Path $Output 'nr-feeder-source.json') -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($Entry in $Manifest.files.PSObject.Properties) {
    if ((Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $Output $Entry.Name)).Hash -ne $Entry.Value) {
        throw "Staged Feeder source changed during compilation: $($Entry.Name)"
    }
}
$Evidence = [ordered]@{
    source_commit=$Manifest.source_commit; development=$Manifest.development
    feeder_commit=$Manifest.feeder_commit; sdk_headers_commit=$Manifest.sdk_headers_commit
    compile_verified=$true; nvidia_runtime_verified=$false; game_runtime_verified=$false
    private_ngx_exports=$false; msvc_crt_dependency=$false
    addon=[IO.Path]::GetFileName($Addon)
    sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $Addon).Hash.ToLowerInvariant()
}
[IO.File]::WriteAllText((Join-Path $Output 'nr-feeder-build.json'), ($Evidence | ConvertTo-Json), (New-Object Text.UTF8Encoding($false)))
Write-Host "Actual optional Feeder producer compiled: $Addon"
Write-Host "SHA256: $($Evidence.sha256)"
Write-Host 'Compilation is not NVIDIA Feature1/18 or game validation.'
