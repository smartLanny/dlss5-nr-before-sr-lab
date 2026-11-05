param(
    [Parameter(Mandatory = $true)][string]$SourceRoot,
    [string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT,
    [string]$OutputRoot = ""
)
# Keep receipt creation and acceptance together so every observed process failure
# is recorded before it is rethrown. The CPU fixture executes this actual function.
function Complete-PostScaledWorkRun {
    param(
        [string]$Build, [string]$Source, [string]$Harness, [string]$Template,
        [string]$Executable, [string]$Stdout, [string]$Stderr,
        [AllowNull()][object]$ExitCode, [bool]$HasExited, [double]$DurationMs,
        [AllowNull()][object]$ProcessFailure = $null,
        [string]$WarpRuntimeHelper = ""
    )
    $files = [ordered]@{
        staged_core_sha256=$Source; generated_harness_sha256=$Harness
        template_sha256=$Template; executable_sha256=$Executable
        stdout_sha256=$Stdout; stderr_sha256=$Stderr
    }
    if ($WarpRuntimeHelper) { $files['ci_warp_runtime_helper_sha256'] = $WarpRuntimeHelper }
    $evidence = [ordered]@{ schema=2 }
    $evidenceErrors = @()
    foreach ($key in $files.Keys) {
        $evidence[$key] = $null
        try {
            if (Test-Path -LiteralPath $files[$key] -PathType Leaf) {
                $evidence[$key] = (Get-FileHash -LiteralPath $files[$key] -Algorithm SHA256).Hash.ToLowerInvariant()
            } else { $evidenceErrors += "Missing evidence file: $key" }
        } catch { $evidenceErrors += "$key`: $($_.Exception.Message)" }
    }
    $stdoutText = ''
    try {
        if (Test-Path -LiteralPath $Stdout -PathType Leaf) {
            $stdoutText = [IO.File]::ReadAllText($Stdout)
        }
    } catch { $evidenceErrors += "Read stdout: $($_.Exception.Message)" }
    $summary = $stdoutText -cmatch '(?m)^PASS post-scaled-work WARP: 60 scenarios \(production WriteCodecDescriptorSet, FP16 model planes, identity \+ nonzero neural perturbation, area overlap integral\)\r?$'
    # The host emits COMPLETE only after the Gpu object has been destroyed.
    $complete = $stdoutText -cmatch '(?m)^COMPLETE post-scaled-work WARP\r?\n\z'
    $exitHex = $null
    if ($null -ne $ExitCode) {
        $exitHex = '0x{0:X8}' -f [uint32]([long]$ExitCode -band 4294967295L)
    }
    $verified = $null -eq $ProcessFailure -and $HasExited -and
        $null -ne $ExitCode -and $ExitCode -eq 0 -and $summary -and $complete -and
        $evidenceErrors.Count -eq 0
    $evidence['exit_code'] = $ExitCode
    $evidence['exit_code_hex'] = $exitHex
    $evidence['has_exited'] = $HasExited
    $evidence['duration_ms'] = $DurationMs
    $evidence['scenario_summary_present'] = $summary
    $evidence['completion_marker_present'] = $complete
    $evidence['process_error'] = if ($null -ne $ProcessFailure) { $ProcessFailure.Exception.Message } else { $null }
    $evidence['evidence_errors'] = @($evidenceErrors)
    $evidence['warp_verified'] = [bool]$verified
    $evidence['nvidia_feature18_verified'] = $false
    $evidence['game_runtime_verified'] = $false
    [IO.File]::WriteAllText((Join-Path $Build 'post-scaled-work-result.json'),
        ($evidence | ConvertTo-Json -Depth 4), (New-Object Text.UTF8Encoding($false)))
    if ($null -ne $ProcessFailure) { throw $ProcessFailure }
    if (-not $HasExited) { throw 'Post-scaled-work WARP process termination was not observed.' }
    if ($null -eq $ExitCode) { throw 'Post-scaled-work WARP exit code was unavailable.' }
    if ($ExitCode -ne 0) { throw "Post-scaled-work WARP failed: $ExitCode" }
    if (-not ($summary -and $complete)) { throw 'Post-scaled-work WARP output is incomplete: expected 60 scenarios and final COMPLETE marker.' }
    if ($evidenceErrors.Count) { throw "Post-scaled-work WARP evidence failed: $($evidenceErrors -join '; ')" }
}

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
$process = $null
$exitCode = $null
$hasExited = $false
$processFailure = $null
$watch = [Diagnostics.Stopwatch]::StartNew()
try {
    $process = Start-Process -FilePath $exe -NoNewWindow -Wait -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $hasExited = $process.HasExited
    $exitCode = $process.ExitCode
} catch { $processFailure = $_ }
finally { $watch.Stop() }
try {
    if (Test-Path -LiteralPath $stdout) { Get-Content -LiteralPath $stdout -Encoding UTF8 | Write-Output }
    if (Test-Path -LiteralPath $stderr) { Get-Content -LiteralPath $stderr -Encoding UTF8 | Write-Output }
} catch {
    if ($null -eq $processFailure) { $processFailure = $_ }
}
Complete-PostScaledWorkRun -Build $build -Source $source -Harness $outSource -Template $template `
    -Executable $exe -Stdout $stdout -Stderr $stderr -ExitCode $exitCode -HasExited $hasExited `
    -DurationMs $watch.Elapsed.TotalMilliseconds -ProcessFailure $processFailure `
    -WarpRuntimeHelper (Join-Path $build 'ci_warp_runtime.h')
$hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "PASS actual staged post-scaling HLSL and descriptors; core SHA256=$hash"
Write-Host 'Test-only neural identity/perturbation; NOT NVIDIA NR or game runtime verification.'
