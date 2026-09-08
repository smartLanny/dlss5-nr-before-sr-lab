param(
    [Parameter(Mandatory=$true)][string]$ToolchainRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [switch]$RunWarp
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=Split-Path -Parent $PSScriptRoot
$output=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $output){throw 'OutputRoot exists; preserve previous evidence.'}
New-Item -ItemType Directory -Path $output | Out-Null
$compiler=Join-Path $ToolchainRoot 'bin/x86_64-w64-mingw32-clang++.exe'
$tests=@('gpu_timing_lifetime','nvfp4_core_faults')
if($RunWarp){$tests+='gpu_timing_warp'}
$results=@()
& python (Join-Path $root 'tests/test_nvfp4_config.py') --compiler $compiler 2>&1 | Tee-Object -FilePath (Join-Path $output 'nvfp4-config.run.log')
if($LASTEXITCODE){throw 'Production precision configuration test failed.'}
$results+=@{name='nvfp4-config';passed=$true}
foreach($test in $tests){
    $binary=Join-Path $output "$test.exe"
    $compilerArgs=@('-std=c++20','-O2','-static','-Wall','-Wextra','-Werror',(Join-Path $root "tests/$test.cpp"),'-o',$binary)
    if($test -eq 'gpu_timing_warp'){$compilerArgs+=@('-ld3d12','-ldxgi','-ldxguid','-luuid')}
    & $compiler @compilerArgs 2>&1 | Tee-Object -FilePath (Join-Path $output "$test.build.log")
    if($LASTEXITCODE){throw "Compile failed: $test"}
    & $binary 2>&1 | Tee-Object -FilePath (Join-Path $output "$test.run.log")
    if($LASTEXITCODE){throw "Test failed: $test"}
    $results+=@{name=$test;sha256=(Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant();passed=$true}
}
@{schema='nvfp4-core-gates-v1';source_commit=(& git -C $root rev-parse HEAD).Trim();source_dirty=(@(& git -C $root status --porcelain).Count -ne 0);tests=$results;warp_verified=$RunWarp.IsPresent;nvidia_verified=$false;game_verified=$false} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'validation.json') -Encoding UTF8
