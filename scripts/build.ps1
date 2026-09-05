param(
    [switch]$Clean,
    [string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT,
    [string]$ReShadeSourceRoot = $env:DLSS5_RESHADE_ROOT
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($ToolchainRoot)) {
    throw 'Provide -ToolchainRoot or set DLSS5_LLVM_MINGW_ROOT.'
}
if ([string]::IsNullOrWhiteSpace($ReShadeSourceRoot)) {
    throw 'Provide -ReShadeSourceRoot or set DLSS5_RESHADE_ROOT.'
}

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Toolchain = (Resolve-Path -LiteralPath $ToolchainRoot).Path
$ReShade = (Resolve-Path -LiteralPath $ReShadeSourceRoot).Path
$CCompiler = Join-Path $Toolchain 'bin\x86_64-w64-mingw32-clang.exe'
$CppCompiler = Join-Path $Toolchain 'bin\x86_64-w64-mingw32-clang++.exe'
$WindRes = Join-Path $Toolchain 'bin\x86_64-w64-mingw32-windres.exe'
$MinHook = Join-Path $ProjectRoot 'third_party\minhook'
$Build = Join-Path $ProjectRoot 'build'
$Objects = Join-Path $Build 'obj'

foreach ($Required in @(
    $CCompiler,
    $CppCompiler,
    $WindRes,
    (Join-Path $MinHook 'include\MinHook.h'),
    (Join-Path $ReShade 'include\reshade.hpp'),
    (Join-Path $ReShade 'deps\imgui\imgui.h')
)) {
    if (-not (Test-Path -LiteralPath $Required)) {
        throw "Required build input missing: $Required"
    }
}

if ($Clean -and (Test-Path -LiteralPath $Build)) {
    $resolvedBuild = (Resolve-Path -LiteralPath $Build).Path
    if (-not $resolvedBuild.StartsWith($ProjectRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean a build directory outside the repository: $resolvedBuild"
    }
    Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
}
New-Item -ItemType Directory -Path $Objects -Force | Out-Null

$SourceCommit = (& git -C $ProjectRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $SourceCommit -notmatch '^[0-9a-f]{40}$') {
    throw 'Cannot establish the source commit for this build.'
}
$TrackedChanges = @(& git -C $ProjectRoot status --porcelain --untracked-files=no)
if ($LASTEXITCODE -ne 0) { throw 'Cannot establish tracked source dirtiness.' }
$SourceDirty = if ($TrackedChanges.Count -eq 0) { '0' } else { '1' }

$Common = @(
    '-O2', '-DNDEBUG', '-DUNICODE', '-D_UNICODE', '-D_WIN64',
    '-flto',
    '-finput-charset=UTF-8',
    '-ffunction-sections', '-fdata-sections',
    "-I$(Join-Path $MinHook 'include')",
    "-I$(Join-Path $ReShade 'include')",
    "-I$(Join-Path $ReShade 'deps\imgui')"
)
$SourceDefines = @(('-DNR_SOURCE_COMMIT="' + $SourceCommit + '"'), ('-DNR_SOURCE_DIRTY=' + $SourceDirty))

$CFiles = @(
    'src\buffer.c',
    'src\hook.c',
    'src\trampoline.c',
    'src\hde\hde64.c'
)
$ObjectFiles = @()
foreach ($Relative in $CFiles) {
    $Source = Join-Path $MinHook $Relative
    $Name = ([IO.Path]::GetFileNameWithoutExtension($Source)) + '.o'
    $Object = Join-Path $Objects $Name
    & $CCompiler @Common '-std=c17' '-c' $Source '-o' $Object
    if ($LASTEXITCODE -ne 0) { throw "C compilation failed: $Source" }
    $ObjectFiles += $Object
}

$BridgeSource = Join-Path $ProjectRoot 'src\nrchain_nvngx.cpp'
$BridgeObject = Join-Path $Objects 'nrchain_nvngx.o'
& $CppCompiler @Common '-std=c++20' '-Wall' '-Wextra' '-Wpedantic' '-c' $BridgeSource '-o' $BridgeObject
if ($LASTEXITCODE -ne 0) { throw "C++ compilation failed: $BridgeSource" }

$ResourceSource = Join-Path $ProjectRoot 'src\nr_before_sr.rc'

$AddonSource = Join-Path $ProjectRoot 'src\nr_before_sr.cpp'
$AddonBuilds = @(
    @{
        Language = 'English'
        Define = '-DNR_UI_CHINESE=0'
        ResourceDefine = '-DNR_UI_CHINESE=0'
        Object = 'nr_before_sr.en.o'
        ResourceObject = 'nr_before_sr.en.resource.o'
        Output = 'DLSS5-AI-Rendering-Upscaling-beta0.4.3r2-@野生的装机宅-Bilibili.en.addon64'
    },
    @{
        Language = 'Simplified Chinese'
        Define = '-DNR_UI_CHINESE=1'
        ResourceDefine = '-DNR_UI_CHINESE=1'
        Object = 'nr_before_sr.zh-CN.o'
        ResourceObject = 'nr_before_sr.zh-CN.resource.o'
        Output = 'DLSS5-AI渲染超分版-beta0.4.3r2-@野生的装机宅-Bilibili.addon64'
    }
)
$AddonOutputs = @()
foreach ($AddonBuild in $AddonBuilds) {
    $ResourceObject = Join-Path $Objects $AddonBuild.ResourceObject
    & $WindRes '--codepage=65001' '--input-format=rc' '--output-format=coff' `
        $AddonBuild.ResourceDefine $ResourceSource $ResourceObject
    if ($LASTEXITCODE -ne 0) {
        throw "Resource compilation failed ($($AddonBuild.Language)): $ResourceSource"
    }
    $AddonObject = Join-Path $Objects $AddonBuild.Object
    & $CppCompiler @Common @SourceDefines '-std=c++20' '-Wall' '-Wextra' '-Wpedantic' $AddonBuild.Define '-c' $AddonSource '-o' $AddonObject
    if ($LASTEXITCODE -ne 0) {
        throw "C++ compilation failed ($($AddonBuild.Language)): $AddonSource"
    }

    $Output = Join-Path $Build $AddonBuild.Output
    $LinkArgs = @(
        '-shared', '-static', '-flto', '-Wl,--gc-sections',
        '-Wl,--strip-all', '-Wl,--no-insert-timestamp'
    ) + $ObjectFiles + @(
        $AddonObject, $ResourceObject, '-o', $Output, '-lkernel32', '-luser32', '-ld3d12',
        '-ldxgi', '-ldxguid', '-lole32', '-luuid', '-ld3dcompiler', '-lshell32'
    )
    & $CppCompiler @LinkArgs
    if ($LASTEXITCODE -ne 0) { throw "Link failed ($($AddonBuild.Language))." }
    $AddonOutputs += $Output
}

$BridgeOutput = Join-Path $Build 'nrchain_nvngx.dll'
$BridgeLinkArgs = @(
    '-shared', '-static', '-flto', '-Wl,--gc-sections', '-Wl,--strip-all',
    '-Wl,--no-insert-timestamp',
    $BridgeObject, '-o', $BridgeOutput, '-lkernel32', '-ld3d12', '-luuid'
)
& $CppCompiler @BridgeLinkArgs
if ($LASTEXITCODE -ne 0) { throw 'Bridge link failed.' }

Copy-Item -LiteralPath (Join-Path $ProjectRoot 'config\nr_before_sr.ini') `
          -Destination (Join-Path $Build 'nr_before_sr.ini') -Force

foreach ($Output in $AddonOutputs + $BridgeOutput) {
    $Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Output).Hash
    Write-Host "Built: $Output"
    Write-Host "SHA256: $Hash"
}
