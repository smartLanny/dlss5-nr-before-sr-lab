param([Parameter(Mandatory=$true)][string]$ToolchainRoot,[Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=Split-Path -Parent $PSScriptRoot
if(Test-Path -LiteralPath $OutputRoot){throw 'Preserve previous region evidence.'}
New-Item -ItemType Directory -Path $OutputRoot | Out-Null
$binary=Join-Path $OutputRoot 'regions.exe'
$compiler=Join-Path $ToolchainRoot 'bin/x86_64-w64-mingw32-clang++.exe'
if(-not (Test-Path -LiteralPath $compiler -PathType Leaf)){throw 'Region compiler missing.'}
& $compiler -std=c++20 -O2 -Wall -Wextra -Werror -static -I (Join-Path $root 'src') (Join-Path $root 'tests/region_gpu.cpp') -ld3d12 -ldxgi -ldxguid -ld3dcompiler -luuid -o $binary
if($LASTEXITCODE){throw 'Region adapter fixture failed to compile.'}
& $binary
if($LASTEXITCODE){throw 'Actual production region adapter WARP failed.'}
$hashes=@{}
foreach($name in @('src/nr_region_shader.h','src/nr_region_runtime.inl','src/nr_descriptor_plan.h','src/nr_binding_types.inl','src/nr_binding_runtime.inl','src/nr_optional_layer_probe.h','tests/region_gpu.cpp')){$hashes[$name]=(Get-FileHash -LiteralPath (Join-Path $root $name)).Hash.ToLowerInvariant()}
[ordered]@{schema='nr-region-warp-v2';success=$true;source=$hashes;prepare_seal_dispatch=$true;production_binding_runtime=$true;game_verified=$false;gpu_budget='unmeasured'} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputRoot 'validation.json') -Encoding utf8
