param(
    [Parameter(Mandatory=$true)][string]$PresentMon,
    [Parameter(Mandatory=$true)][string]$SampleRoot,
    [Parameter(Mandatory=$true)][string]$DevelopmentSdkRoot,
    [Parameter(Mandatory=$true)][string]$CoreDirectory,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [switch]$AllowFocusedWindow,
    [switch]$PrepareOnly
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if (!$PrepareOnly -and !$AllowFocusedWindow) { throw 'Explicit focused sample opt-in is required.' }
$Collector=(Resolve-Path -LiteralPath $PresentMon).Path
$Sample=(Resolve-Path -LiteralPath $SampleRoot).Path
$Sdk=(Resolve-Path -LiteralPath $DevelopmentSdkRoot).Path
$Core=(Resolve-Path -LiteralPath $CoreDirectory).Path
$SampleHash=(Get-FileHash -LiteralPath (Join-Path $Sample '_bin/StreamlineSample.exe')).Hash.ToLowerInvariant()
if($SampleHash -ne '55c1d42b1c101d7f3b01c04c4effcbb05284931e021184177f4c6a4a089fa2fd') {
    throw 'Require the separately frozen abc2952 controlled sample executable.'
}
$Output=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $Output) { throw 'Existing evidence must not be overwritten.' }
if((Get-FileHash -LiteralPath $Collector).Hash -ne '9bec3083069f58f911e6a512f4806db51a27bd096103087bc1d05ef54c80a191') {
    throw 'Require the fixed PresentMon 2.5.1 executable.'
}
$Signature=Get-AuthenticodeSignature -LiteralPath $Collector
if($Signature.Status -ne 'Valid' -or $Signature.SignerCertificate.Subject -notmatch 'Intel Corporation') {
    throw 'PresentMon signature does not match the fixed Intel tool.'
}
$Session='NRFG-'+[guid]::NewGuid().ToString('N')
$Csv=Join-Path $Output 'display.csv'
# Every supplied path is passed as an argument, never evaluated as shell text.
$Arguments=@('--process_name','StreamlineSample.exe','--session_name',$Session,
    '--output_file',$Csv,'--timed','40','--terminate_after_timed',
    '--no_console_stats','--no_track_input','--track_frame_type','--qpc_time')
[IO.Directory]::CreateDirectory($Output) | Out-Null
$Plan=[ordered]@{collector_version='2.5.1';collector_sha256=(Get-FileHash -LiteralPath $Collector).Hash.ToLowerInvariant();
    launcher_source=(& git -C (Join-Path $PSScriptRoot '..') rev-parse HEAD).Trim();
    launcher_sha256=(Get-FileHash -LiteralPath $PSCommandPath).Hash.ToLowerInvariant();
    sample_launcher_sha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'run-fg-sample.ps1')).Hash.ToLowerInvariant();
    sample_exe_sha256=$SampleHash;sample_build_source='abc2952b60fb6a11effc4c3e85dae07a30912782';
    collector_arguments_private=$Arguments;sample_root_private=$Sample;sdk_root_private=$Sdk;core_root_private=$Core;
    sample_frames_requested=60;nr_requested=$true;sr_requested=$true;fg_requested=$true;
    collector_started=$false;sample_started=$false;generated_pixels_verified=$false;display_cadence_verified=$false;
    frame_type_requires_instrumentation=$true;input_tracking=$false}
$Plan | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Output 'plan.json') -Encoding utf8
if($PrepareOnly) { Write-Output 'Capture plan prepared; no collector or GPU process started.'; return }

# No elevation, account/group changes, or termination of unrelated sessions.
# ProcessStartInfo with redirected streams also keeps the collector window hidden.
$Start=[Diagnostics.ProcessStartInfo]::new()
$Start.FileName=$Collector; $Start.UseShellExecute=$false; $Start.CreateNoWindow=$true
$Start.RedirectStandardOutput=$true; $Start.RedirectStandardError=$true
# Windows paths cannot contain quotes. These are simple native arguments, not a shell command.
$Start.Arguments=($Arguments | ForEach-Object { '"'+$_+'"' }) -join ' '
$Process=[Diagnostics.Process]::Start($Start)
$OutTask=$Process.StandardOutput.ReadToEndAsync(); $ErrTask=$Process.StandardError.ReadToEndAsync()
$Plan.collector_started=$true
$SampleFailure=$null
try {
    # Reject startup failures such as access denied before launching the renderer.
    if($Process.WaitForExit(1000)) { throw "Collector exited during startup: $($Process.ExitCode)." }
    $Plan.sample_started=$true
    & (Join-Path $PSScriptRoot 'run-fg-sample.ps1') -SampleRoot $Sample -DevelopmentSdkRoot $Sdk `
        -CoreDirectory $Core -OutputRoot (Join-Path $Output 'sample') `
        -EnableSR -EnableNR -EnableFG -Frames 60 -AllowFocusedWindow
} catch { $SampleFailure=$_.Exception.Message }
finally {
    if(!$Process.HasExited) {
        # The exact random session belongs to this invocation only. Let PresentMon
        # stop/flush it through its own documented session control.
        & $Collector --session_name $Session --terminate_existing_session 1> (Join-Path $Output 'stop.stdout.txt') 2> (Join-Path $Output 'stop.stderr.txt')
        if(!$Process.WaitForExit(5000)) {
            $SampleFailure='Collector did not flush after its own session stop; no display evidence accepted.'
            Stop-Process -Id $Process.Id
            $Process.WaitForExit(5000) | Out-Null
        }
    }
    $OutTask.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $Output 'collector.stdout.txt') -Encoding utf8
    $ErrTask.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $Output 'collector.stderr.txt') -Encoding utf8
    $Plan['collector_exit_code']=$Process.ExitCode
    $Plan['failure']=$SampleFailure
    $Plan['csv_exists']=Test-Path -LiteralPath $Csv
    $Plan | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Output 'result.json') -Encoding utf8
}
if($SampleFailure -or $Process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $Csv)) {
    throw 'Capture incomplete; exact process output and sample evidence retained.'
}
Write-Output 'Capture retained for independent frame/OS-display analysis; no pixel or performance conclusion is inferred.'
