param(
    [switch]$Clean,
    [switch]$DescriptorProfile,
    [string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT,
    [string]$ReShadeSourceRoot = $env:DLSS5_RESHADE_ROOT,
    [string]$FaceRuntimeRoot = $env:DLSS5_FACE_RUNTIME_ROOT,
    [string]$CodecCacheRoot,
    [string]$NgxSdkRoot = $env:DLSS5_NGX_SDK_ROOT,
    [string]$VcVarsPath = $env:DLSS5_VCVARS_PATH
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
$DescriptorProfileValue = if ($DescriptorProfile) { 1 } else { 0 }
$BuildMode = if ($DescriptorProfile) { 'measurement' } else { 'ordinary' }
$BuildLeaf = if ($DescriptorProfile) { 'build-profile' } else { 'build' }
# ProjectRoot is the staged source/core directory in the delivery layout.
# Keep the profile as a sibling so it can never overwrite ordinary outputs.
$Build = Join-Path $ProjectRoot $BuildLeaf
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
    "-DNR_DESCRIPTOR_PROFILE=$DescriptorProfileValue",
    '-flto',
    '-finput-charset=UTF-8',
    '-ffunction-sections', '-fdata-sections',
    "-I$(Join-Path $MinHook 'include')",
    "-I$(Join-Path $ReShade 'include')",
    "-I$(Join-Path $ReShade 'deps\imgui')"
)

function New-NrSourceDefinesResponse {
    param([string]$Path, [string]$Commit, [string]$Dirty)
    if ($Commit -notmatch '^[0-9a-f]{40}$' -or $Dirty -notmatch '^[01]$') {
        throw 'Invalid source identity for compiler response file.'
    }
    # Clang parses these escapes once. Do not send embedded C string quotes
    # through PowerShell 5 native-argument marshalling, which removes them.
    $Lines = @(
        ('-DNR_SOURCE_COMMIT=\"' + $Commit + '\"'),
        ('-DNR_SOURCE_DIRTY=' + $Dirty)
    )
    [IO.File]::WriteAllLines($Path, $Lines, (New-Object Text.UTF8Encoding($false)))
    return ('@' + $Path)
}
$SourceDefines = New-NrSourceDefinesResponse -Path (Join-Path $Objects 'source-defines.rsp') -Commit $SourceCommit -Dirty $SourceDirty

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

$FaceBackend = 0
$FaceCompile = @('-DNR_FACE_BACKEND=0')
if (-not [string]::IsNullOrWhiteSpace($FaceRuntimeRoot)) {
    $FaceRuntimeRoot = (Resolve-Path -LiteralPath $FaceRuntimeRoot).Path
    $faceHeader = Join-Path $FaceRuntimeRoot 'include/onnxruntime_c_api.h'
    $faceRuntime = Join-Path $FaceRuntimeRoot 'lib/onnxruntime.dll'
    if ((Get-FileHash -LiteralPath $faceHeader).Hash.ToLowerInvariant() -ne '820cc6c379d259045d96dcce9d30f2505fdb31ba526e294a283083d015914466' -or
        (Get-FileHash -LiteralPath $faceRuntime).Hash.ToLowerInvariant() -ne 'dec964ab1ee36cc9b0ae247d13b376627992fc57dec0454354017ab8fd84f1ea') {
        throw 'Face backend requires the pinned official ONNX Runtime 1.23.2 CPU headers and runtime.'
    }
    $FaceBackend = 1
    $FaceCompile = @('-DNR_FACE_BACKEND=1', '-I'+(Join-Path $FaceRuntimeRoot 'include'))
}
$Common += @('-DNR_FACE_BACKEND='+$FaceBackend)
foreach ($faceName in @('nr_face_detector', 'nr_face_worker')) {
    $faceSource = Join-Path $ProjectRoot ('src/'+$faceName+'.cpp')
    $faceObject = Join-Path $Objects ($faceName+'.o')
    & $CppCompiler @Common @FaceCompile '-std=c++20' '-Wall' '-Wextra' '-Werror' '-c' $faceSource '-o' $faceObject
    if ($LASTEXITCODE) {throw "Face backend compilation failed: $faceName"}
    $ObjectFiles += $faceObject
}
[ordered]@{schema='nr-face-build-v1';enabled=($FaceBackend -eq 1);runtime_version='1.23.2';
    runtime_sha256='dec964ab1ee36cc9b0ae247d13b376627992fc57dec0454354017ab8fd84f1ea';
    model_sha256='b1b3061b1ae7a43bcc1497284ade754f701db774ef3a19481edcaf27fbc425b1';
    activation='Opt-in; assets loaded only by asynchronous worker; no game quality or 5% performance qualification'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Build 'face-backend.json') -Encoding utf8

$DualSource=Join-Path $ProjectRoot 'src/nr_dual_spatial_d3d12.cpp'
$DualObject=Join-Path $Objects 'nr_dual_spatial_d3d12.o'
& $CppCompiler @Common '-std=c++20' '-Wall' '-Wextra' '-Wpedantic' '-c' $DualSource '-o' $DualObject
if($LASTEXITCODE){throw 'Second NR spatial module compilation failed.'}
$ObjectFiles += $DualObject

$CodecCompiler=Join-Path $Build 'compile-codec-artifact.exe'
& $CppCompiler '-std=c++20' '-O2' '-static' '-municode' `
    "-DNR_DESCRIPTOR_PROFILE=$DescriptorProfileValue" `
    (Join-Path $PSScriptRoot 'compile-codec-artifact.cpp') '-ld3dcompiler' '-o' $CodecCompiler
if($LASTEXITCODE -ne 0 -or -not(Test-Path -LiteralPath $CodecCompiler -PathType Leaf)){throw 'Codec artifact compiler build failed.'}
$CodecCacheArgs = @()
if ($CodecCacheRoot) { $CodecCacheArgs = @('--cache-from', (Resolve-Path -LiteralPath $CodecCacheRoot).Path) }
& python (Join-Path $PSScriptRoot 'prepare-codec-artifacts.py') --source (Join-Path $ProjectRoot 'src/nr_before_sr.cpp') --compiler $CodecCompiler --output $Build --workers 2 @CodecCacheArgs
if($LASTEXITCODE -ne 0 -or -not(Test-Path -LiteralPath (Join-Path $Build 'nr_codec_precompiled.h') -PathType Leaf)){throw 'Frozen shader artifact generation failed.'}
$Common += '-DNR_CODEC_PRECOMPILED=1'

& (Join-Path $PSScriptRoot 'build-nrchain.ps1') -ProjectRoot $ProjectRoot -OutputRoot $Build -NgxSdkRoot $NgxSdkRoot -VcVarsPath $VcVarsPath
if ($LASTEXITCODE -ne 0) { throw 'Production cold NGX bridge build failed.' }

$ResourceSource = Join-Path $ProjectRoot 'src\nr_before_sr.rc'

$AddonSource = Join-Path $ProjectRoot 'src\nr_before_sr.cpp'
$AddonBuilds = @(
    @{
        Language = 'English'
        Define = '-DNR_UI_CHINESE=0'
        ResourceDefine = '-DNR_UI_CHINESE=0'
        Object = 'nr_before_sr.en.o'
        ResourceObject = 'nr_before_sr.en.resource.o'
        LinkLeaf = 'nr_before_sr.en.link.addon64'
        Output = 'DLSS5-AI-Rendering-Upscaling-beta0.5-dline20-@野生的装机宅-Bilibili.en.addon64'
    },
    @{
        Language = 'Simplified Chinese'
        Define = '-DNR_UI_CHINESE=1'
        ResourceDefine = '-DNR_UI_CHINESE=1'
        Object = 'nr_before_sr.zh-CN.o'
        ResourceObject = 'nr_before_sr.zh-CN.resource.o'
        LinkLeaf = 'nr_before_sr.zh-CN.link.addon64'
        Output = 'DLSS5-AI渲染超分版-beta0.5-dline20-@野生的装机宅-Bilibili.addon64'
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
    & $CppCompiler @Common $SourceDefines '-std=c++20' '-Wall' '-Wextra' '-Wpedantic' $AddonBuild.Define '-c' $AddonSource '-o' $AddonObject
    if ($LASTEXITCODE -ne 0) {
        throw "C++ compilation failed ($($AddonBuild.Language)): $AddonSource"
    }

    $Output = Join-Path $Build $AddonBuild.Output
    # Keep the native tool's output leaf ASCII; PowerShell performs the final
    # Unicode rename. The UTF-8 BOM on this script is required by Windows PS5.
    $LinkOutput = Join-Path $Build $AddonBuild.LinkLeaf
    $LinkArgs = @(
        '-shared', '-static', '-flto', '-Wl,--gc-sections',
        '-Wl,--strip-all', '-Wl,--no-insert-timestamp'
    ) + $ObjectFiles + @(
        $AddonObject, $ResourceObject, '-o', $LinkOutput, '-lkernel32', '-luser32', '-ld3d12',
        '-ldxgi', '-ldxguid', '-lole32', '-luuid', '-ld3dcompiler', '-lshell32', '-lbcrypt'
    )
    & $CppCompiler @LinkArgs
    if ($LASTEXITCODE -ne 0) { throw "Link failed ($($AddonBuild.Language))." }
    Move-Item -LiteralPath $LinkOutput -Destination $Output -Force
    $AddonOutputs += $Output
}

$BridgeOutput = Join-Path $Build 'nrchain_nvngx.dll'

Copy-Item -LiteralPath (Join-Path $ProjectRoot 'config\nr_before_sr.ini') `
          -Destination (Join-Path $Build 'nr_before_sr.ini') -Force

$ArtifactRows = @()
foreach ($Output in $AddonOutputs + $BridgeOutput) {
    if (-not (Test-Path -LiteralPath $Output -PathType Leaf)) {
        throw "Expected build output missing: $Output"
    }
    $Item = Get-Item -LiteralPath $Output
    $Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Output).Hash.ToLowerInvariant()
    $ArtifactRows += [ordered]@{
        name = $Item.Name
        bytes = [int64]$Item.Length
        sha256 = $Hash
        kind = if ($Item.Name -ieq 'nrchain_nvngx.dll') { 'bridge' } else { 'addon' }
    }
    Write-Host "Built: $Output"
    Write-Host "SHA256: $Hash"
}

$AddonVariantRows = @($ArtifactRows | Where-Object { $_.kind -eq 'addon' })
$BridgeVariantRows = @($ArtifactRows | Where-Object { $_.kind -eq 'bridge' })
[ordered]@{
    schema = 'nr-build-variant-v1'
    mode = $BuildMode
    NR_DESCRIPTOR_PROFILE = $DescriptorProfileValue
    sourcecommit = $SourceCommit
    source_dirty = [int]$SourceDirty
    build_directory = $BuildLeaf
    addons = $AddonVariantRows
    bridge = $BridgeVariantRows
    artifacts = $ArtifactRows
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Build 'build-variant.json') -Encoding utf8
