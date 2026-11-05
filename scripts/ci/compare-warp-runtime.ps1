param(
    [Parameter(Mandatory=$true)][string]$ExistingExe,
    [Parameter(Mandatory=$true)][string]$BaselineReceipt,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [string]$SourceRoot = "",
    [string]$WorkingDirectory = (Get-Location).Path
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
Set-StrictMode -Version Latest

function Get-ComparisonHash([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Assert-ComparisonHash([string]$Path, [string]$Expected, [string]$Identity) {
    if ((Get-ComparisonHash $Path) -cne $Expected) { throw "WARP comparison hash mismatch: $Identity" }
}
function Import-ComparisonReceiptFunction([string]$Validator) {
    $tokens=$null; $errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile($Validator,[ref]$tokens,[ref]$errors)
    if ($errors.Count) { throw 'Cannot parse production WARP receipt validator.' }
    $functions=@($ast.FindAll({param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Complete-PostScaledWorkRun'
    },$true))
    if ($functions.Count -ne 1) { throw 'Expected one production WARP receipt function.' }
    # Return the actual definition for dot invocation in the caller's scope.
    [scriptblock]::Create($functions[0].Extent.Text)
}
function Read-BaselineWarpModules([string]$Stderr, [string]$ExpectedHash) {
    Assert-ComparisonHash $Stderr $ExpectedHash 'baseline stderr'
    $records=@(); $errors=@(); $seen=@{}
    foreach ($line in [IO.File]::ReadAllLines($Stderr)) {
        if ($line -cmatch '^LOADED_MODULE_UNAVAILABLE name=.+ error=.+$') { $errors += $line; continue }
        if ($line -cnotmatch '^LOADED_MODULE name=(d3d10warp\.dll|d3d12\.dll|d3d12core\.dll|d3d12sdklayers\.dll|dxgi\.dll|d3dcompiler_47\.dll) path=(.+)$') { continue }
        $name=$Matches[1]; $path=$Matches[2]
        if ($seen.ContainsKey($path)) { continue }
        $seen[$path]=$true
        try {
            if (-not [IO.Path]::IsPathRooted($path)) { throw 'Module path is not absolute.' }
            $version=[Diagnostics.FileVersionInfo]::GetVersionInfo($path)
            $hash=Get-ComparisonHash $path
            $records += [ordered]@{name=$name; path=$path; file_version=$version.FileVersion
                product_version=$version.ProductVersion; sha256=$hash
                metadata_source='baseline stderr path; file metadata read after baseline exit'}
            if ([string]::IsNullOrWhiteSpace($version.FileVersion)) { $errors += "$name`: file version unavailable." }
        } catch { $errors += "$name`: $($_.Exception.Message)" }
    }
    if (-not $seen.Count) { $errors += 'No baseline LOADED_MODULE path records were found.' }
    [ordered]@{stderr_sha256=$ExpectedHash; modules=@($records); errors=@($errors)
        files_verified=($records.Count -gt 0 -and $errors.Count -eq 0)}
}
function Stop-WarpComparisonOnTimeout {
    param([Diagnostics.Process]$Process, [double]$ElapsedSeconds,
          [double]$LimitSeconds, [hashtable]$Run, [hashtable]$Metadata)
    if ($ElapsedSeconds -lt $LimitSeconds -or $Process.HasExited) { return $false }
    $Metadata['diagnostic_timed_out']=$true
    $Metadata['diagnostic_timeout_seconds']=$LimitSeconds
    $Metadata['diagnostic_timeout_elapsed_seconds']=$ElapsedSeconds
    $Metadata['process_termination_requested']=$true
    # This is the exact Process created by this script, never a name/PID search.
    try {
        $Process.Kill()
        if (-not $Process.WaitForExit(5000)) {
            $Metadata['process_termination_error']='Termination not observed within 5 seconds.'
        }
    } catch { $Metadata['process_termination_error']=$_.Exception.Message }
    try {
        $Run.HasExited=$Process.HasExited
        if ($Run.HasExited) { $Run.ExitCode=$Process.ExitCode }
    } catch { $Metadata['process_state_error']=$_.Exception.Message }
    $Metadata['process_termination_observed']=$Run.HasExited
    # Even a concurrent natural exit or zero Kill status cannot erase timeout.
    throw [TimeoutException]::new("WARP runtime comparison diagnostic process timed out after $LimitSeconds seconds; harness wait remains unchanged.")
}
function Complete-WarpRuntimeComparison {
    param([hashtable]$Run, [string]$Baseline, [string]$BaselineHash,
          [string]$ExpectedWarpPath, [string]$ExpectedWarpHash, [object[]]$Modules,
          [AllowNull()][object]$Failure, [hashtable]$Metadata)
    $identityErrors=@()
    $after=$null
    try { $after=Get-ComparisonHash $Baseline }
    catch { $identityErrors += 'Baseline receipt is no longer readable.' }
    if ($after -cne $BaselineHash) { $identityErrors += 'Baseline receipt changed.' }
    $warp=@($Modules | Where-Object { $_.name -ieq 'd3d10warp.dll' })
    if ($warp.Count -ne 1) { $identityErrors += 'Exactly one actual loaded WARP module was not observed.' }
    elseif (-not [string]::Equals([IO.Path]::GetFullPath($warp[0].path),
            [IO.Path]::GetFullPath($ExpectedWarpPath),[StringComparison]::OrdinalIgnoreCase) -or
            $warp[0].sha256 -cne $ExpectedWarpHash -or
            [string]::IsNullOrWhiteSpace($warp[0].file_version)) {
        $identityErrors += 'Actual loaded WARP path, version or SHA256 did not match the pinned copy.'
    }
    if ($identityErrors.Count -and $null -eq $Failure) {
        try { throw ($identityErrors -join ' ') } catch { $Failure=$_ }
    }
    $gateFailure=$null
    try { Complete-PostScaledWorkRun @Run -ProcessFailure $Failure }
    catch { $gateFailure=$_ }
    $receipt=[ordered]@{
        schema=1; purpose='same existing EXE; CI-only WARP runtime comparison'
        original_gate_failed=$true; overrides_baseline=$false
        baseline_receipt_sha256_before=$BaselineHash; baseline_receipt_sha256_after=$after
        baseline_preserved=($BaselineHash -ceq $after)
        expected_warp_sha256=$ExpectedWarpHash; expected_warp_path=$ExpectedWarpPath
        actual_loaded_modules=@($Modules); runtime_identity_verified=($identityErrors.Count -eq 0)
        identity_errors=@($identityErrors); diagnostic_passed=($null -eq $gateFailure)
        error=$(if ($null -ne $gateFailure) { $gateFailure.Exception.Message } else { $null })
        metadata=$Metadata
    }
    [IO.File]::WriteAllText((Join-Path $Run.Build 'warp-runtime-comparison.json'),
        ($receipt | ConvertTo-Json -Depth 8),(New-Object Text.UTF8Encoding($false)))
    if ($null -ne $gateFailure) { throw $gateFailure }
}

# All vendor files and the EXE copy remain outside uploaded evidence directories.
if ($env:OS -ne 'Windows_NT') { throw 'WARP runtime comparison requires Windows.' }
if ([string]::IsNullOrWhiteSpace($env:RUNNER_TEMP)) { throw 'RUNNER_TEMP is required.' }
$repo=Split-Path -Parent $PSScriptRoot
$repo=Split-Path -Parent $repo
if ([string]::IsNullOrWhiteSpace($SourceRoot)) { $SourceRoot=$repo }
$exe=(Resolve-Path -LiteralPath $ExistingExe).Path
$baseline=(Resolve-Path -LiteralPath $BaselineReceipt).Path
$baselineHash=Get-ComparisonHash $baseline
$baselineData=Get-Content -LiteralPath $baseline -Raw | ConvertFrom-Json
if ($baselineData.warp_verified -ne $false) { throw 'Comparison requires an existing failed baseline receipt.' }
Assert-ComparisonHash $exe $baselineData.executable_sha256 'existing EXE versus failed baseline'
$source=Join-Path ([IO.Path]::GetFullPath($SourceRoot)) 'src/nr_before_sr.cpp'
$template=Join-Path $repo 'tests/post_scaled_work_gpu.cpp.in'
$harness=[IO.Path]::ChangeExtension($exe,'.cpp')
Assert-ComparisonHash $source $baselineData.staged_core_sha256 'staged Core'
Assert-ComparisonHash $template $baselineData.template_sha256 'template'
Assert-ComparisonHash $harness $baselineData.generated_harness_sha256 'generated harness'
$build=[IO.Path]::GetFullPath($OutputRoot)
if ((Test-Path -LiteralPath $build) -and @(Get-ChildItem -LiteralPath $build -Force).Count) {
    throw 'Comparison output must be a new or empty directory; baseline files cannot be overwritten.'
}
$tempRoot=[IO.Path]::GetFullPath($env:RUNNER_TEMP)
$separator=[IO.Path]::DirectorySeparatorChar
if (($tempRoot.TrimEnd('\','/')+$separator).StartsWith($build.TrimEnd('\','/')+$separator,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Vendor temporary directory cannot be inside the evidence output directory.'
}
New-Item -ItemType Directory -Force -Path $build | Out-Null
$private=Join-Path $tempRoot ('warp-compare-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $private | Out-Null
$copy=Join-Path $private 'post_scaled_work_gpu.exe'
$warpPath=Join-Path $private 'd3d10warp.dll'
$packageHash='afc058f4d1066edc4231a38f3ab3592b1930f7287d4ac700e6a056e0874bb0be'
$warpHash='bb8ee56fd0f7ee665abb61178301e612181e5e2f57af0e5ab64e456b8f1a8c4f'
$url='https://api.nuget.org/v3-flatcontainer/microsoft.direct3d.warp/1.0.22/microsoft.direct3d.warp.1.0.22.nupkg'
$metadata=@{package_url=$url; package_version='1.0.22'; package_sha256=$packageHash
    license_url='https://www.nuget.org/packages/Microsoft.Direct3D.WARP/1.0.22/License'
    executable_sha256=$baselineData.executable_sha256; baseline_exit_code=$baselineData.exit_code
    arguments=@(); working_directory=[IO.Path]::GetFullPath($WorkingDirectory)
    image_os=$env:ImageOS; image_version=$env:ImageVersion; module_capture_errors=@()
    diagnostic_timeout_seconds=900; diagnostic_timed_out=$false
    baseline_loaded_modules=@(); baseline_module_errors=@(); baseline_module_files_verified=$false}
$stdout=Join-Path $build 'stdout.txt'; $stderr=Join-Path $build 'stderr.txt'
$run=@{Build=$build; Source=$source; Harness=$harness; Template=$template; Executable=$copy
    Stdout=$stdout; Stderr=$stderr; ExitCode=$null; HasExited=$false; DurationMs=0.0}
$failure=$null; $process=$null; $modules=@{}
$watch=[Diagnostics.Stopwatch]::StartNew()
. (Import-ComparisonReceiptFunction (Join-Path $repo 'scripts/validate-post-scaled-work-gpu.ps1'))
try {
    $baselineModules=Read-BaselineWarpModules (Join-Path (Split-Path -Parent $baseline) 'stderr.txt') $baselineData.stderr_sha256
    $metadata.baseline_stderr_sha256=$baselineModules.stderr_sha256
    $metadata.baseline_loaded_modules=$baselineModules.modules
    $metadata.baseline_module_errors=$baselineModules.errors
    $metadata.baseline_module_files_verified=$baselineModules.files_verified
    Copy-Item -LiteralPath $exe -Destination $copy
    Assert-ComparisonHash $copy $baselineData.executable_sha256 'copied EXE'
    $package=Join-Path $private 'warp.nupkg'
    Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $package
    Assert-ComparisonHash $package $packageHash 'official NuGet package'
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip=[IO.Compression.ZipFile]::OpenRead($package)
    try {
        $entry=$zip.GetEntry('build/native/bin/x64/d3d10warp.dll')
        if ($null -eq $entry) { throw 'Pinned package has no x64 WARP entry.' }
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$warpPath)
    } finally { $zip.Dispose() }
    Assert-ComparisonHash $warpPath $warpHash 'official x64 WARP DLL'
    $watch.Restart()
    # No arguments: identical to the original post-scaling preflight invocation.
    $process=Start-Process -FilePath $copy -WorkingDirectory $metadata.working_directory -NoNewWindow -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $processHandle=$process.Handle # retain the real process handle before waiting
    $metadata['process_id']=$process.Id
    do {
        $process.Refresh()
        try {
            foreach ($module in $process.Modules) {
                $name=$module.ModuleName.ToLowerInvariant()
                if ($name -notin @('d3d10warp.dll','d3d12.dll','d3d12core.dll','d3d12sdklayers.dll','dxgi.dll','d3dcompiler_47.dll')) { continue }
                $path=$module.FileName
                if (-not $modules.ContainsKey($path)) {
                    $modules[$path]=[ordered]@{name=$name; path=$path
                        file_version=$module.FileVersionInfo.FileVersion
                        product_version=$module.FileVersionInfo.ProductVersion
                        sha256=(Get-ComparisonHash $path)}
                }
            }
        } catch {
            if ($metadata.module_capture_errors.Count -lt 8) { $metadata.module_capture_errors += $_.Exception.Message }
        }
        [void](Stop-WarpComparisonOnTimeout -Process $process -ElapsedSeconds $watch.Elapsed.TotalSeconds `
            -LimitSeconds 900 -Run $run -Metadata $metadata)
        if (-not $process.HasExited) { Start-Sleep -Milliseconds 100 }
    } while (-not $process.HasExited)
    $process.WaitForExit()
    $run.HasExited=$process.HasExited; $run.ExitCode=$process.ExitCode
    Assert-ComparisonHash $copy $baselineData.executable_sha256 'EXE after execution'
    Assert-ComparisonHash $exe $baselineData.executable_sha256 'original EXE after execution'
} catch { $failure=$_ }
finally {
    $watch.Stop(); $run.DurationMs=$watch.Elapsed.TotalMilliseconds
}
# Preserve files if process termination could not be established. This also
# covers an unexpected observer failure while the native process is still live.
$cleanupAllowed=$null -eq $process
if ($null -ne $process) {
    try { $cleanupAllowed=$process.HasExited }
    catch { $metadata['process_state_error']=$_.Exception.Message }
}
$metadata['temporary_payload_cleanup_allowed']=$cleanupAllowed
try {
    try {
        foreach ($log in @($stdout,$stderr)) {
            if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Encoding UTF8 | Write-Output }
        }
    } catch { if ($null -eq $failure) { $failure=$_ } }
    Complete-WarpRuntimeComparison -Run $run -Baseline $baseline -BaselineHash $baselineHash `
        -ExpectedWarpPath $warpPath -ExpectedWarpHash $warpHash -Modules @($modules.Values) -Failure $failure -Metadata $metadata
} finally {
    if ($null -ne $process) { $process.Dispose() }
    if ($cleanupAllowed) { Remove-Item -LiteralPath $private -Recurse -Force -ErrorAction SilentlyContinue }
}
