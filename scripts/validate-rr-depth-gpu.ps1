param(
    [string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT,
    [string]$OutputRoot = "",
    [switch]$RunWarp
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repo = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($ToolchainRoot)) {
    $ToolchainRoot = Join-Path (Split-Path -Parent $repo) `
        'dlss5-lab/toolchains/llvm-mingw-20260826-ucrt-x86_64'
}
$toolchain = (Resolve-Path -LiteralPath $ToolchainRoot).Path
$compiler = Join-Path $toolchain 'bin/x86_64-w64-mingw32-clang++.exe'
if (-not (Test-Path -LiteralPath $compiler)) {
    throw "Missing compiler: $compiler"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $repo 'build/rr-depth-gpu'
}
$build = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath (Join-Path $build 'result.json')) {
    throw "OutputRoot already contains result.json; choose a new evidence directory: $build"
}
New-Item -ItemType Directory -Force -Path $build | Out-Null

$source = Join-Path $repo 'tests/rr_depth_gpu.cpp'
$exe = Join-Path $build 'rr_depth_gpu.exe'
$compileLog = Join-Path $build 'compile.log'
$stdout = Join-Path $build 'stdout.txt'
$stderr = Join-Path $build 'stderr.txt'
$resultJson = Join-Path $build 'result.json'

if (-not (Test-Path -LiteralPath $source)) {
    throw "Missing reusable GPU harness: $source"
}
$sourceText = Get-Content -LiteralPath $source -Raw -Encoding UTF8
if ($sourceText -notmatch 'kFenceTimeoutMs\s*=\s*20000') {
    throw 'The GPU harness must keep the required 20 second fence timeout.'
}

$compileArguments = @(
    '-std=c++20', '-O2', '-static', '-Wall', '-Wextra', '-Werror',
    $source, '-o', $exe, '-ld3d12', '-ldxgi', '-ldxguid',
    '-ld3dcompiler', '-lole32'
)
$compileOutput = & $compiler @compileArguments 2>&1
[IO.File]::WriteAllText($compileLog, ($compileOutput -join "`r`n") + "`r`n",
    [Text.UTF8Encoding]::new($false))
$compileOutput | Write-Output
if ($LASTEXITCODE -ne 0) {
    throw "RR depth GPU harness compile failed; evidence retained in $compileLog"
}
Write-Output "PASS compiled production RR depth shader harness: $exe"

$metadata = [ordered]@{
    schema = 1
    source = $source
    source_sha256 = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
    compiler = $compiler
    executable = $exe
    executable_sha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    compile_log = $compileLog
    fence_timeout_ms = 20000
    warp_requested = [bool]$RunWarp
    warp_verified = $false
    nvidia_feature18_verified = $false
    game_runtime_verified = $false
}

if (-not $RunWarp) {
    $metadata.status = 'compile_only_waiting_for_warp_authorization'
    [IO.File]::WriteAllText($resultJson, ($metadata | ConvertTo-Json -Depth 4),
        [Text.UTF8Encoding]::new($false))
    Write-Output 'COMPILE_ONLY: WARP dispatch not requested; use -RunWarp when the GPU slot is available.'
    Write-Output "Evidence retained under $build"
    return
}

# The harness itself enforces the 20,000 ms fence wait. This path keeps every
# run artifact, including a timeout/device-removal log, for independent review.
$oldPath = $env:PATH
$env:PATH = (Join-Path $toolchain 'bin') + ';' + $oldPath
$process = $null
try {
    $psi = [Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = $exe
    $psi.WorkingDirectory = $repo
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $psi
    if (-not $process.Start()) { throw 'Unable to start RR depth WARP harness' }
    if (-not $process.WaitForExit(25000)) {
        try { $process.Kill() } catch { }
        $metadata.status = 'process_timeout_after_25s'
        throw 'RR depth WARP harness exceeded the 25 second outer wait; evidence retained.'
    }
    $runStdout = $process.StandardOutput.ReadToEnd()
    $runStderr = $process.StandardError.ReadToEnd()
    [IO.File]::WriteAllText($stdout, $runStdout, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($stderr, $runStderr, [Text.UTF8Encoding]::new($false))
    if ($runStdout) { Write-Output $runStdout.TrimEnd() }
    if ($runStderr) { Write-Output $runStderr.TrimEnd() }
    $metadata.exit_code = $process.ExitCode
    $metadata.stdout_sha256 = (Get-FileHash -LiteralPath $stdout -Algorithm SHA256).Hash.ToLowerInvariant()
    $metadata.stderr_sha256 = (Get-FileHash -LiteralPath $stderr -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($process.ExitCode -ne 0) {
        $metadata.status = 'warp_failed_evidence_retained'
        throw "RR depth WARP contract failed with exit code $($process.ExitCode); evidence retained."
    }
    $metadata.status = 'warp_pass'
    $metadata.warp_verified = $true
    Write-Output 'PASS RR depth WARP contract; source input unchanged and private R32 output verified.'
} catch {
    if (-not (Test-Path -LiteralPath $stdout)) {
        [IO.File]::WriteAllText($stdout, '', [Text.UTF8Encoding]::new($false))
    }
    if (-not (Test-Path -LiteralPath $stderr)) {
        [IO.File]::WriteAllText($stderr, $_.Exception.ToString(), [Text.UTF8Encoding]::new($false))
    }
    $metadata.stdout_sha256 = (Get-FileHash -LiteralPath $stdout -Algorithm SHA256).Hash.ToLowerInvariant()
    $metadata.stderr_sha256 = (Get-FileHash -LiteralPath $stderr -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText($resultJson, ($metadata | ConvertTo-Json -Depth 4),
        [Text.UTF8Encoding]::new($false))
    Write-Output "Evidence retained under $build"
    throw
} finally {
    if ($process -ne $null) { $process.Dispose() }
    $env:PATH = $oldPath
}

[IO.File]::WriteAllText($resultJson, ($metadata | ConvertTo-Json -Depth 4),
    [Text.UTF8Encoding]::new($false))
Write-Output "Evidence retained under $build"
