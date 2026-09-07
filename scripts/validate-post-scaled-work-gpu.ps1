param(
    [Parameter(Mandatory = $true)][string]$SourceRoot,
    [string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT,
    [string]$OutputRoot = ""
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([string]::IsNullOrWhiteSpace($ToolchainRoot)) { throw 'Provide ToolchainRoot or DLSS5_LLVM_MINGW_ROOT.' }
$root = Split-Path -Parent $PSScriptRoot
$staged = (Resolve-Path -LiteralPath $SourceRoot).Path
$toolchain = (Resolve-Path -LiteralPath $ToolchainRoot).Path
if ([string]::IsNullOrWhiteSpace($OutputRoot)) { $OutputRoot = Join-Path $root 'build/post-scaled-work-gpu' }
$build = [IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Force -Path $build | Out-Null
$source = Join-Path $staged 'src/nr_before_sr.cpp'
$template = Join-Path $root 'tests/post_scaled_work_gpu.cpp.in'
$outSource = Join-Path $build 'post_scaled_work_gpu.cpp'
& python (Join-Path $PSScriptRoot 'extract-post-gpu.py') --source-root $staged --template $template --output $outSource
if ($LASTEXITCODE) { throw 'Exact production post-codec extraction failed.' }
$compiler = Join-Path $toolchain 'bin/x86_64-w64-mingw32-clang++.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw "Missing compiler: $compiler" }
$exe = Join-Path $build 'post_scaled_work_gpu.exe'
& $compiler '-std=c++17' '-O2' '-static' '-Wall' '-Wextra' '-Werror' $outSource '-o' $exe "-I$(Join-Path $staged 'src/interop')" '-ld3d12' '-ldxgi' '-ld3dcompiler' '-ldxguid' '-lole32'
if ($LASTEXITCODE) { throw "Post-scaled-work compile failed: $LASTEXITCODE" }
# PS5 native stderr must not bypass inspection of the real process exit code.
$stdout = Join-Path $build 'stdout.txt'
$stderr = Join-Path $build 'stderr.txt'
$process = Start-Process -FilePath $exe -NoNewWindow -Wait -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
if (Test-Path -LiteralPath $stdout) { Get-Content -LiteralPath $stdout -Encoding UTF8 | Write-Output }
if (Test-Path -LiteralPath $stderr) { Get-Content -LiteralPath $stderr -Encoding UTF8 | Write-Output }
if ($process.ExitCode) { throw "Post-scaled-work WARP failed: $($process.ExitCode)" }
$hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
$evidence = [ordered]@{
    schema=1; staged_core_sha256=$hash
    generated_harness_sha256=(Get-FileHash -LiteralPath $outSource -Algorithm SHA256).Hash.ToLowerInvariant()
    template_sha256=(Get-FileHash -LiteralPath $template -Algorithm SHA256).Hash.ToLowerInvariant()
    stdout_sha256=(Get-FileHash -LiteralPath $stdout -Algorithm SHA256).Hash.ToLowerInvariant()
    exit_code=$process.ExitCode; warp_verified=$true; nvidia_feature18_verified=$false; game_runtime_verified=$false
}
[IO.File]::WriteAllText((Join-Path $build 'post-scaled-work-result.json'), ($evidence | ConvertTo-Json -Depth 3), (New-Object Text.UTF8Encoding($false)))
Write-Host "PASS actual staged post-scaling HLSL and descriptors; core SHA256=$hash"
Write-Host 'Test-only neural identity/perturbation; NOT NVIDIA NR or game runtime verification.'
