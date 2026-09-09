param(
    [Parameter(Mandatory=$true)][string]$DependencyRoot,
    [string]$VisualStudioRoot = 'C:\BuildTools\VS2022',
    [string]$OutputDirectory = '',
    [switch]$RunCpuTests
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ExpansionProject = Split-Path -Parent $PSScriptRoot
$ExpansionDeps = (Resolve-Path -LiteralPath $DependencyRoot).Path
$ExpansionCuda = Join-Path $ExpansionDeps 'cuda-13.0'
$ExpansionCutlass = Join-Path $ExpansionDeps 'cutlass-4.7.1'
$ExpansionNvcc = Join-Path $ExpansionCuda 'bin\nvcc.exe'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $ExpansionProject 'build\nvfp4-expansion' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$ExpansionOutput = (Resolve-Path -LiteralPath $OutputDirectory).Path
$ExpansionSource = Join-Path $ExpansionProject 'kernels\nvfp4_expansion'
Import-Module (Join-Path $VisualStudioRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $VisualStudioRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$env:PATH = (Join-Path $ExpansionCuda 'bin') + ';' + $env:PATH
$ExpansionInputs = [ordered]@{}
foreach ($ExpansionLeaf in @('expansion_abi.h','fragment_layout.h','params_builder.cpp','pack_expansion.cu')) {
    $ExpansionInputs[$ExpansionLeaf] = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $ExpansionSource $ExpansionLeaf)).Hash.ToLowerInvariant()
}
$ExpansionInputs['nvfp4_io/original_layout.cuh'] = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $ExpansionProject 'kernels\nvfp4_io\original_layout.cuh')).Hash.ToLowerInvariant()
$ExpansionIdentity = [ordered]@{
    abi_version = 1
    sources = $ExpansionInputs
    cutlass_commit = 'cb4247394dd82148787aed73e5dc7cef33cbf862'
    cuda_nvcc = (& $ExpansionNvcc --version | Out-String).Trim()
    msvc = [Diagnostics.FileVersionInfo]::GetVersionInfo((Get-Command cl.exe).Source).FileVersion
    target = 'sm_120a'
    options = @('C++17','O3','relaxed-constexpr','extended-lambda','MSVC-MT','precise-math','256threads','tokens32-64-128')
}
$ExpansionJson = $ExpansionIdentity | ConvertTo-Json -Depth 6 -Compress
$ExpansionAlgorithm = [Security.Cryptography.SHA256]::Create()
$ExpansionId = ([BitConverter]::ToString($ExpansionAlgorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes($ExpansionJson)))).Replace('-','').ToLowerInvariant()
$ExpansionAlgorithm.Dispose()
('#define NR_EXPANSION_BUILD_ID "' + $ExpansionId + '"') | Set-Content -LiteralPath (Join-Path $ExpansionOutput 'nr_expansion_build_id.h') -Encoding ASCII
$ExpansionCommon = @('-std=c++17','-arch=sm_120a','-O3','--expt-relaxed-constexpr','--extended-lambda',
    '--diag-suppress=177,550,20012','-DWIN32_LEAN_AND_MEAN','-DNOMINMAX','-Xcompiler=/EHsc,/MT,/Zc:__cplusplus,/utf-8',
    ('-I' + (Join-Path $ExpansionCutlass 'include')),('-I' + (Join-Path $ExpansionCutlass 'tools\util\include')),('-I' + $ExpansionSource))
$ErrorActionPreference = 'Continue'
& $ExpansionNvcc @ExpansionCommon --resource-usage --cubin (Join-Path $ExpansionSource 'pack_expansion.cu') -o (Join-Path $ExpansionOutput 'nr_nvfp4_pack_expansion_sm120.cubin') 2>&1 | Tee-Object -FilePath (Join-Path $ExpansionOutput 'expansion-build.log')
$ExpansionCompileExit = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($ExpansionCompileExit -ne 0) { throw 'Pack/expansion cubin compile failed; see expansion-build.log.' }
# The descriptor helper is plain C++. It does not link CUDA or load its driver.
$ExpansionCl = @('/nologo','/std:c++17','/O2','/EHsc','/MT','/Zc:__cplusplus','/utf-8',('/I' + $ExpansionSource),('/I' + $ExpansionOutput))
& cl.exe @ExpansionCl '/LD' (Join-Path $ExpansionSource 'params_builder.cpp') ('/Fo' + (Join-Path $ExpansionOutput 'params_builder.obj')) `
    '/link' ('/OUT:' + (Join-Path $ExpansionOutput 'nr_nvfp4_expansion_params.dll')) ('/IMPLIB:' + (Join-Path $ExpansionOutput 'nr_nvfp4_expansion_params.lib'))
if ($LASTEXITCODE -ne 0) { throw 'Pure CPU pack/expansion helper compile failed.' }
Copy-Item -LiteralPath (Join-Path $ExpansionSource 'expansion_abi.h') -Destination $ExpansionOutput -Force
$ExpansionReceipt = [ordered]@{
    schema = 1
    abi_fingerprint = $ExpansionId
    source_identity = $ExpansionIdentity
    gpu_validation = 'not run by this build script'
    artifacts = @('nr_nvfp4_pack_expansion_sm120.cubin','nr_nvfp4_expansion_params.dll') | ForEach-Object {
        @{file=$_;sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $ExpansionOutput $_)).Hash.ToLowerInvariant()}
    }
}
$ExpansionReceipt | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $ExpansionOutput 'expansion-build.json') -Encoding UTF8
if ($RunCpuTests) {
    $ExpansionLayout = Join-Path $ExpansionOutput 'layout-cpu.exe'
    & cl.exe @ExpansionCl ('/I' + (Join-Path $ExpansionCuda 'include')) ('/I' + (Join-Path $ExpansionCuda 'include\cccl')) ('/I' + (Join-Path $ExpansionCutlass 'include')) `
        (Join-Path $ExpansionProject 'tests\nvfp4_expansion\layout_cpu.cpp') ('/Fo' + (Join-Path $ExpansionOutput 'layout_cpu.obj')) ('/Fe' + $ExpansionLayout)
    if ($LASTEXITCODE -ne 0) { throw 'CPU CUTLASS layout test compile failed.' }
    & $ExpansionLayout
    if ($LASTEXITCODE -ne 0) { throw 'CPU CUTLASS layout/access test failed.' }
    & python (Join-Path $ExpansionProject 'tests\nvfp4_expansion\abi_cpu.py') --build $ExpansionOutput --output (Join-Path $ExpansionOutput 'abi-cpu.json')
    if ($LASTEXITCODE -ne 0) { throw 'CPU expansion helper ABI test failed.' }
}
Write-Host "Compiled experimental pack/expansion in $ExpansionOutput; no GPU execution performed."
