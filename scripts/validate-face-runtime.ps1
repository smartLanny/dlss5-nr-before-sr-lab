param(
    [Parameter(Mandatory=$true)][string]$ToolchainRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [string]$FaceRuntimeRoot=$env:DLSS5_FACE_RUNTIME_ROOT,
    [string]$FaceModelPath=$env:DLSS5_FACE_MODEL_PATH
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=Split-Path -Parent $PSScriptRoot
$output=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $output){throw 'Preserve earlier face validation evidence.'}
New-Item -ItemType Directory -Path $output | Out-Null
$compiler=Join-Path $ToolchainRoot 'bin/x86_64-w64-mingw32-clang++.exe'
$gpu=Join-Path $output 'face-tracker.exe'
& $compiler -std=c++20 -O2 -Wall -Wextra -Werror -static (Join-Path $root 'tests/face_tracker_gpu.cpp') -ld3d12 -ldxgi -ldxguid -ld3dcompiler -luuid -o $gpu
if($LASTEXITCODE){throw 'Face tracker compilation failed.'}
& $gpu
if($LASTEXITCODE){throw 'Actual WARP face tracker validation failed.'}
$cpu=$false
if($FaceRuntimeRoot) {
    if(-not $FaceModelPath){throw 'A pinned prepared model is required for the CPU worker gate.'}
    $worker=Join-Path $output 'face-worker.exe'
    $include='-I'+(Join-Path $FaceRuntimeRoot 'include')
    & $compiler -std=c++20 -O2 -Wall -Wextra -Werror -static -municode -DNR_FACE_BACKEND=1 $include `
        (Join-Path $root 'src/nr_face_detector.cpp') (Join-Path $root 'src/nr_face_worker.cpp') `
        (Join-Path $root 'tests/face_worker.cpp') -lbcrypt -o $worker
    if($LASTEXITCODE){throw 'Face CPU worker compilation failed.'}
    & $worker (Join-Path $FaceRuntimeRoot 'lib/onnxruntime.dll') $FaceModelPath
    if($LASTEXITCODE){throw 'Real CPU detector and asynchronous worker gate failed.'}
    $cpu=$true
}
$files=@('src/nr_face_detector.h','src/nr_face_detector.cpp','src/nr_face_worker.h','src/nr_face_worker.cpp',
    'src/nr_region_inputs.h','src/nr_face_tracker_gpu.inl','src/nr_face_tracker_shader.h','src/nr_face_runtime.inl',
    'tests/face_worker.cpp','tests/face_tracker_gpu.cpp','tests/replay_gpu.cpp','tests/ci_warp_runtime.h')
$hashes=@{}
foreach($file in $files){$hashes[$file]=(Get-FileHash -LiteralPath (Join-Path $root $file)).Hash.ToLowerInvariant()}
[ordered]@{schema='nr-face-runtime-validation-v1';success=$true;warp_tracker_verified=$true;
    cpu_detector_worker_verified=$cpu;game_quality_verified=$false;whole_frame_gpu_budget='unmeasured';
    resource_budget='64 MiB GetResourceAllocationInfo accounting; descriptor/driver overhead separate';
    source=$hashes
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output 'validation.json') -Encoding utf8
