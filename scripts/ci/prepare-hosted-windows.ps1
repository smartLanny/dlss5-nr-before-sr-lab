# Public, pinned build inputs for the ordinary hosted Windows runner.
# This prepares dependencies only. The canonical builder owns every gate.
param(
    [Parameter(Mandatory=$true)][string]$DependencyRoot,
    [Parameter(Mandatory=$true)][string]$ReceiptPath
)
$ErrorActionPreference = 'Stop'
# Windows PowerShell 5.1 must not depend on the retired Internet Explorer parser,
# and rendering per-chunk progress substantially slows large hosted downloads.
$ProgressPreference = 'SilentlyContinue'
Set-StrictMode -Version Latest
$Repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Dependencies = [IO.Path]::GetFullPath($DependencyRoot)
if (Test-Path -LiteralPath $Dependencies) { throw 'Use a fresh dependency directory; cached inputs are not trusted.' }
New-Item -ItemType Directory -Path $Dependencies | Out-Null
$Receipt = [ordered]@{schema='hosted-windows-inputs-v1'; prepared=$false; git=[ordered]@{}; archives=[ordered]@{}}

function Checkout-Pinned([string]$Name, [string]$Url, [string]$Commit, [string[]]$SparsePaths = @(), [string]$AutoCrlf = 'false') {
    $Path = Join-Path $Dependencies $Name
    & git init --quiet $Path
    if ($LASTEXITCODE) { throw "Dependency initialization failed: $Name" }
    & git -C $Path config core.autocrlf $AutoCrlf
    if ($LASTEXITCODE) { throw "Dependency newline configuration failed: $Name" }
    & git -C $Path remote add origin $Url
    if ($LASTEXITCODE) { throw "Dependency remote failed: $Name" }
    if ($SparsePaths.Count) {
        & git -C $Path sparse-checkout init --cone
        if ($LASTEXITCODE) { throw "Dependency sparse initialization failed: $Name" }
        & git -C $Path sparse-checkout set @SparsePaths
        if ($LASTEXITCODE) { throw "Dependency sparse paths failed: $Name" }
    }
    & git -C $Path fetch --filter=blob:none --depth 1 origin $Commit
    if ($LASTEXITCODE) { throw "Pinned dependency fetch failed: $Name" }
    & git -C $Path checkout --quiet --detach FETCH_HEAD
    if ($LASTEXITCODE) { throw "Pinned dependency checkout failed: $Name" }
    $Actual = (& git -C $Path rev-parse HEAD).Trim()
    if ($LASTEXITCODE -or $Actual -ne $Commit) { throw "Dependency commit mismatch: $Name" }
    $Receipt.git[$Name] = [ordered]@{url=$Url; commit=$Actual; core_autocrlf=$AutoCrlf}
    return $Path
}

function Download-Pinned([string]$Url, [string]$Path, [string]$Sha256) {
    Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $Path
    $Actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($Actual -ne $Sha256.ToLowerInvariant()) { throw "Dependency digest mismatch: $Url" }
    $Receipt.archives[[IO.Path]::GetFileName($Path)] = [ordered]@{url=$Url; sha256=$Actual}
}

$Carrier = Checkout-Pinned 'carrier' 'https://github.com/NIGos/dlss5-bridge.git' '28aed4099b0fe1c207b20b5fee5364c0773c25c2'
$ReShade = Checkout-Pinned 'reshade' 'https://github.com/crosire/reshade.git' '4a50d1eddace85734871d91792ff214f13f66c01'
& git -C $ReShade -c core.autocrlf=false submodule update --init --recursive --depth 1
if ($LASTEXITCODE) { throw 'Pinned ReShade submodules failed' }
$DirectXMath = Checkout-Pinned 'directxmath' 'https://github.com/microsoft/DirectXMath.git' '28929ac6f3c1649a7641741b689ebecccb17aa8d'
# Only official SDK headers, x64 static libraries and root license are needed.
# Runtime/model directories are neither checked out nor uploaded. Existing NGX
# production hashes bind to Git-for-Windows CRLF text bytes; preserve that exact
# checkout convention, while binary .lib bytes remain untouched.
$Ngx = Checkout-Pinned 'ngx-sdk' 'https://github.com/NVIDIA/DLSS.git' 'a291cc7d2cc642a51566f3dfd5376f635cd1b284' -SparsePaths @('include', 'lib/Windows_x86_64/x64') -AutoCrlf 'true'
$Ffx = Checkout-Pinned 'fidelityfx-sdk' 'https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git' 'c6efa6bf7f2027b3ec94f28578bb5965eabb9e55'
& git -C $Ffx lfs pull
if ($LASTEXITCODE) { throw 'Pinned FidelityFX payloads failed' }

$LlvmZip = Join-Path $Dependencies 'llvm-mingw.zip'
Download-Pinned 'https://github.com/mstorsjo/llvm-mingw/releases/download/20260826/llvm-mingw-20260826-ucrt-x86_64.zip' $LlvmZip 'ae601f4e0f72bbdf441ad2df8bb16f037e2e9251559ea6b37b4057aef39c06c3'
Expand-Archive -LiteralPath $LlvmZip -DestinationPath (Join-Path $Dependencies 'toolchain')
$Toolchain = Join-Path $Dependencies 'toolchain/llvm-mingw-20260826-ucrt-x86_64'
$Compiler = Join-Path $Toolchain 'bin/x86_64-w64-mingw32-clang++.exe'
if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) { throw 'Pinned LLVM-MinGW compiler is missing' }
Remove-Item -LiteralPath $LlvmZip

$OrtZip = Join-Path $Dependencies 'onnxruntime.zip'
Download-Pinned 'https://github.com/microsoft/onnxruntime/releases/download/v1.23.2/onnxruntime-win-x64-1.23.2.zip' $OrtZip '0b38df9af21834e41e73d602d90db5cb06dbd1ca618948b8f1d66d607ac9f3cd'
Expand-Archive -LiteralPath $OrtZip -DestinationPath (Join-Path $Dependencies 'face-runtime')
$Face = Join-Path $Dependencies 'face-runtime/onnxruntime-win-x64-1.23.2'
Remove-Item -LiteralPath $OrtZip
$Models = Join-Path $Dependencies 'face-model'
New-Item -ItemType Directory -Path $Models | Out-Null
Download-Pinned 'https://media.githubusercontent.com/media/opencv/opencv_zoo/47534e27c9851bb1128ccc0102f1145e27f23f98/models/face_detection_yunet/face_detection_yunet_2023mar.onnx' (Join-Path $Models 'yunet-original.onnx') '8f2383e4dd3cfbb4553ea8718107fc0423210dc964f9f4280604804ed2552fa4'
Download-Pinned 'https://raw.githubusercontent.com/opencv/opencv_zoo/47534e27c9851bb1128ccc0102f1145e27f23f98/models/face_detection_yunet/LICENSE' (Join-Path $Models 'YUNET-LICENSE') 'c83b8120c50ccbd4c4f96edf53141bdd566ebb8f8e9227e415326aa1b1aba958'
& python -m venv (Join-Path $Models 'venv')
if ($LASTEXITCODE) { throw 'Face serializer environment failed' }
$ModelPython = Join-Path $Models 'venv/Scripts/python.exe'
& $ModelPython -m pip install --disable-pip-version-check 'onnx==1.19.1'
if ($LASTEXITCODE) { throw 'Pinned face serializer installation failed' }
& $ModelPython (Join-Path $Repo 'scripts/prepare-face-model.py') (Join-Path $Models 'yunet-original.onnx') (Join-Path $Models 'yunet-dynamic.onnx')
if ($LASTEXITCODE) { throw 'Pinned face model preparation failed' }
if ((Get-FileHash -LiteralPath (Join-Path $Models 'yunet-dynamic.onnx') -Algorithm SHA256).Hash.ToLowerInvariant() -ne 'b1b3061b1ae7a43bcc1497284ade754f701db774ef3a19481edcaf27fbc425b1') { throw 'Prepared face model hash mismatch' }

$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$VisualStudio = (& $VsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if ($LASTEXITCODE -or -not $VisualStudio) { throw 'The standard Windows image lacks the required Visual C++ tools' }
$VcVars = Join-Path $VisualStudio 'VC/Auxiliary/Build/vcvarsall.bat'
if (-not (Test-Path -LiteralPath $VcVars -PathType Leaf)) { throw 'Visual C++ environment script missing' }
$FfxCache = Join-Path $Dependencies 'ffx-opticalflow-build'
& python (Join-Path $Repo 'experiments/final_residual/build_amd_probe.py') --sdk $Ffx --compiler $Compiler --output $FfxCache --sdk-only
if ($LASTEXITCODE) { throw 'Pinned AMD shader/backend compilation failed' }

$Environment = [ordered]@{
    TOOLCHAIN_ROOT=$Toolchain; CXX=$Compiler; DLSS5_LLVM_MINGW_ROOT=$Toolchain
    NR_LLVM_BIN=(Join-Path $Toolchain 'bin'); UPSTREAM_ROOT=$Carrier
    RESHADE_SOURCE_ROOT=$ReShade; DIRECTXMATH_ROOT=$DirectXMath
    FACE_RUNTIME_ROOT=$Face; FACE_MODEL_ROOT=$Models
    DLSS5_FACE_TEST_ASSETS=$Models; DLSS5_FACE_TEST_RUNTIME=$Face
    DLSS5_NGX_SDK_ROOT=$Ngx; DLSS5_VCVARS_PATH=$VcVars
    DLSS5_FFX_SDK_ROOT=$Ffx; DLSS5_FFX_CACHE_ROOT=$FfxCache
}
foreach ($Entry in $Environment.GetEnumerator()) {
    [Environment]::SetEnvironmentVariable($Entry.Key, $Entry.Value, 'Process')
    if ($env:GITHUB_ENV) { [IO.File]::AppendAllText($env:GITHUB_ENV, "$($Entry.Key)=$($Entry.Value)`n", (New-Object Text.UTF8Encoding($false))) }
}
$Receipt.prepared = $true
$Receipt['compiler_sha256'] = (Get-FileHash -LiteralPath $Compiler -Algorithm SHA256).Hash.ToLowerInvariant()
$Receipt['ffx_cache_receipt_sha256'] = (Get-FileHash -LiteralPath (Join-Path $FfxCache 'build.json') -Algorithm SHA256).Hash.ToLowerInvariant()
$Receipt['image_os'] = $env:ImageOS
$Receipt['image_version'] = $env:ImageVersion
$Receipt['visual_studio'] = $VisualStudio
$Receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $ReceiptPath -Encoding UTF8
Write-Host 'Pinned full-face, official NGX, carrier and FidelityFX inputs prepared; canonical gates have not run yet.'
