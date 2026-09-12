param(
    [string]$ToolchainRoot=$env:DLSS5_LLVM_MINGW_ROOT,
    [Parameter(Mandatory=$true)][string]$OutputRoot
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$projectRoot=Split-Path -Parent $PSScriptRoot
$output=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $output){throw 'Preserve prior replay evidence; use a new OutputRoot.'}
$compiler=Join-Path $ToolchainRoot 'bin/x86_64-w64-mingw32-clang++.exe'
if(-not(Test-Path -LiteralPath $compiler)){throw 'Replay compiler missing.'}
New-Item -ItemType Directory -Path $output | Out-Null
$exe=Join-Path $output 'replay-gpu.exe'
& $compiler -std=c++20 -O2 -Wall -Wextra -Werror -static (Join-Path $projectRoot 'tests/replay_gpu.cpp') -o $exe -ld3d12 -ldxgi -ldxguid -ld3dcompiler -luuid
if($LASTEXITCODE){throw 'Replay GPU fixture compile failed.'}
$frame=Join-Path $output 'captured.nrf'
& $exe $frame
if($LASTEXITCODE){throw 'Actual WARP replay capture failed.'}
$previous=$env:NR_REPLAY_TEST_FRAME
try {
    $env:NR_REPLAY_TEST_FRAME=$frame
    & python (Join-Path $projectRoot 'tests/test_replay_reader.py')
    if($LASTEXITCODE){throw 'Independent replay reader rejected its C++ fixture.'}
} finally {$env:NR_REPLAY_TEST_FRAME=$previous}
$lab=Join-Path $output 'nr-model-lab.exe'
& $compiler -std=c++20 -O2 -Wall -Wextra -Werror -static -municode (Join-Path $projectRoot 'tools/nr_model_lab.cpp') -o $lab -ld3d12 -ldxgi -ldxguid -luuid
if($LASTEXITCODE){throw 'Standalone model lab compile failed.'}
$files=@('src/nr_replay_format.h','src/nr_replay_shader.h','src/nr_replay_gpu.inl','src/nr_replay_runtime.inl',
    'tests/replay_gpu.cpp','tests/test_replay_reader.py','scripts/nr_replay.py','tools/nr_model_lab.cpp')
$source=@{}
foreach($file in $files){$source[$file]=(Get-FileHash -LiteralPath (Join-Path $projectRoot $file) -Algorithm SHA256).Hash.ToLowerInvariant()}
[ordered]@{schema='nr-replay-validation-v1';success=$true;source=$source;
    fixture_sha256=(Get-FileHash -LiteralPath $frame -Algorithm SHA256).Hash.ToLowerInvariant();
    lab_sha256=(Get-FileHash -LiteralPath $lab -Algorithm SHA256).Hash.ToLowerInvariant();
    scope='Actual WARP typed-input capture and independent file reader; standalone lab compiled only, no NVIDIA or game execution by this validator.'
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output 'validation.json') -Encoding utf8
