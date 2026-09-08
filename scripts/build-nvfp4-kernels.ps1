param(
    [Parameter(Mandatory=$true)][string]$DependencyRoot,
    [string]$VisualStudioRoot = 'C:\BuildTools\VS2022',
    [string]$OutputDirectory = '',
    [switch]$Bootstrap
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Project = Split-Path -Parent $PSScriptRoot
if ($Bootstrap) {
    & python (Join-Path $Project 'tools\nvfp4_kernel\bootstrap.py') --deps $DependencyRoot
    if ($LASTEXITCODE -ne 0) { throw 'Dependency bootstrap failed.' }
}
$Deps = (Resolve-Path -LiteralPath $DependencyRoot).Path
$Cuda = Join-Path $Deps 'cuda-13.0'
$Cutlass = Join-Path $Deps 'cutlass-4.7.1'
$Nvcc = Join-Path $Cuda 'bin\nvcc.exe'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $Project 'build\nvfp4' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$Output = (Resolve-Path -LiteralPath $OutputDirectory).Path

# Import the supported MSVC toolchain environment without changing global PATH.
Import-Module (Join-Path $VisualStudioRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $VisualStudioRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$env:PATH = (Join-Path $Cuda 'bin') + ';' + $env:PATH
$Common = @('-std=c++17','-arch=sm_120a','-O3','--expt-relaxed-constexpr','--extended-lambda',
    '--diag-suppress=177,550,20012','-DNDEBUG','-DWIN32_LEAN_AND_MEAN','-DNOMINMAX',
    '-Xcompiler=/EHsc,/MT,/Zc:__cplusplus,/utf-8',
    ('-I' + (Join-Path $Cutlass 'include')),
    ('-I' + (Join-Path $Cutlass 'tools\util\include')),
    ('-I' + (Join-Path $Project 'kernels\nvfp4')),
    ('-I' + $Output))
$Kernel = Join-Path $Project 'kernels\nvfp4\contract.cu'
$Helper = Join-Path $Project 'kernels\nvfp4\params_builder.cu'
$SourceInputs = [ordered]@{}
foreach ($Name in @('nvfp4_kernel_abi.h','nvfp4_fused_abi.h','contract_kernel.cuh','contract.cu','params_builder.cu',
                    'fused_store.cuh','fused_contract_kernel.cuh','fused_contract.cu','fused_params_builder.cu')) {
    $SourceInputs[$Name] = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $Project ('kernels\nvfp4\' + $Name))).Hash.ToLowerInvariant()
}
foreach ($Name in @('original_layout.cuh','original_io.cu')) {
    $SourceInputs['nvfp4_io/' + $Name] = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $Project ('kernels\nvfp4_io\' + $Name))).Hash.ToLowerInvariant()
}
$CompilerVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo((Get-Command cl.exe).Source).FileVersion
$Identity = [ordered]@{
    abi_version = 1
    sources = $SourceInputs
    cutlass_commit = 'cb4247394dd82148787aed73e5dc7cef33cbf862'
    cuda_nvcc = (& $Nvcc --version | Out-String).Trim()
    msvc = $CompilerVersion
    target = 'sm_120a'
    options = @('C++17','O3','relaxed-constexpr','extended-lambda','NDEBUG','MSVC-MT','Zc:__cplusplus')
}
$IdentityJson = $Identity | ConvertTo-Json -Depth 5 -Compress
$HashAlgorithm = [Security.Cryptography.SHA256]::Create()
$BuildId = ([BitConverter]::ToString($HashAlgorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes($IdentityJson)))).Replace('-','').ToLowerInvariant()
$HashAlgorithm.Dispose()
('#define NR_NVFP4_BUILD_ID "' + $BuildId + '"') | Set-Content -LiteralPath (Join-Path $Output 'nr_nvfp4_build_id.h') -Encoding ASCII
$OldReceipt = $null
if (Test-Path -LiteralPath (Join-Path $Output 'kernel-build.json')) {
    $OldReceipt = Get-Content -Raw -LiteralPath (Join-Path $Output 'kernel-build.json') | ConvertFrom-Json
}
function Test-KernelCache([string]$Leaf, [string[]]$Sources) {
    if (-not $OldReceipt -or -not (Test-Path -LiteralPath (Join-Path $Output $Leaf))) { return $false }
    if (-not $OldReceipt.PSObject.Properties['source_identity']) { return $false }
    if ($OldReceipt.source_identity.cuda_nvcc -ne $Identity.cuda_nvcc -or
        $OldReceipt.source_identity.msvc -ne $Identity.msvc -or
        $OldReceipt.source_identity.cutlass_commit -ne $Identity.cutlass_commit -or
        ($OldReceipt.source_identity.options -join ',') -ne ($Identity.options -join ',')) { return $false }
    foreach ($Source in $Sources) {
        $Property = $OldReceipt.source_identity.sources.PSObject.Properties[$Source]
        if (-not $Property -or $Property.Value -ne $SourceInputs[$Source]) { return $false }
    }
    $Artifact = @($OldReceipt.artifacts | Where-Object { $_.file -eq $Leaf })
    if ($Artifact.Count -ne 1) { return $false }
    return (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $Output $Leaf)).Hash.ToLowerInvariant() -eq $Artifact[0].sha256
}
if (-not (Test-KernelCache 'nr_nvfp4_contract_sm120.cubin' @('contract_kernel.cuh','contract.cu'))) {
    $ErrorActionPreference = 'Continue' # Windows PowerShell treats native stderr warnings as errors.
    & $Nvcc @Common --cubin $Kernel -o (Join-Path $Output 'nr_nvfp4_contract_sm120.cubin') 2>&1 | Tee-Object -FilePath (Join-Path $Output 'contract-build.log')
    $ErrorActionPreference = 'Stop'
    if ($LASTEXITCODE -ne 0) { throw 'SM120 cubin compilation failed; see contract-build.log.' }
} else { Write-Host 'Reusing verified contraction cubin with identical compile inputs.' }
if (-not (Test-KernelCache 'nr_nvfp4_contract_fused_sm120.cubin' @('contract_kernel.cuh','fused_contract_kernel.cuh','fused_contract.cu','fused_store.cuh','nvfp4_io/original_layout.cuh'))) {
    $ErrorActionPreference = 'Continue'
    & $Nvcc @Common --cubin (Join-Path $Project 'kernels\nvfp4\fused_contract.cu') -o (Join-Path $Output 'nr_nvfp4_contract_fused_sm120.cubin') 2>&1 | Tee-Object -FilePath (Join-Path $Output 'fused-contract-build.log')
    $ErrorActionPreference = 'Stop'
    if ($LASTEXITCODE -ne 0) { throw 'Fused SM120 cubin compilation failed; see fused-contract-build.log.' }
} else { Write-Host 'Reusing verified fused cubin with identical compile inputs.' }
$ErrorActionPreference = 'Continue'
& $Nvcc @Common --shared $Helper (Join-Path $Project 'kernels\nvfp4\fused_params_builder.cu') -o (Join-Path $Output 'nr_nvfp4_params.dll') ('-L' + (Join-Path $Cuda 'lib\x64')) -lcuda 2>&1 | Tee-Object -FilePath (Join-Path $Output 'builder-build.log')
$ErrorActionPreference = 'Stop'
if ($LASTEXITCODE -ne 0) { throw 'Parameter helper compilation failed; see builder-build.log.' }
Copy-Item -LiteralPath (Join-Path $Project 'kernels\nvfp4\nvfp4_kernel_abi.h') -Destination $Output -Force
Copy-Item -LiteralPath (Join-Path $Project 'kernels\nvfp4\nvfp4_fused_abi.h') -Destination $Output -Force
$Receipt = [ordered]@{
    schema = 1
    abi_fingerprint = $BuildId
    source_identity = $Identity
    cutlass_commit = 'cb4247394dd82148787aed73e5dc7cef33cbf862'
    cuda = (& $Nvcc --version | Out-String).Trim()
    target = 'sm_120a'
    artifacts = @('nr_nvfp4_contract_sm120.cubin','nr_nvfp4_contract_fused_sm120.cubin','nr_nvfp4_params.dll') | ForEach-Object {
        @{ file = $_; sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $Output $_)).Hash.ToLowerInvariant() }
    }
}
$Receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Output 'kernel-build.json') -Encoding UTF8
Write-Host "NVFP4 kernel and helper built in $Output"
