param(
    [Parameter(Mandatory=$true)][string]$DependencyRoot,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$VisualStudioRoot='C:\BuildTools\VS2022'
)
$ErrorActionPreference='Stop'
$Root=Split-Path -Parent $PSScriptRoot
$Nvcc=Join-Path $DependencyRoot 'cuda-13.0/bin/nvcc.exe'
Import-Module (Join-Path $VisualStudioRoot 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $VisualStudioRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$Output=(Resolve-Path -LiteralPath $OutputDirectory).Path
$Source=Join-Path $Root 'kernels/nvfp4_io/original_io.cu'
$Cubin=Join-Path $Output 'nr_original_io.cubin'
& $Nvcc -std=c++17 -arch=sm_120a -O3 --cubin $Source -o $Cubin
if($LASTEXITCODE){throw 'Reconstructed IO compilation failed.'}
$Receipt=[ordered]@{schema=1;target='sm_120a';scope='reference-equivalent IO, no automatic renderer activation';files=@()}
foreach($File in @($Source,(Join-Path $Root 'kernels/nvfp4_io/original_layout.cuh'),$Cubin)) {
    $Receipt.files+=@{name=[IO.Path]::GetFileName($File);sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $File).Hash.ToLowerInvariant()}
}
$Receipt | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Output 'io-build.json') -Encoding UTF8
