param(
    [Parameter(Mandatory=$true)][string]$SampleRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [ValidateRange(30,600)][int]$Frames=120,
    [switch]$EnableFG,
    [switch]$AllowFocusedWindow
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if (!$AllowFocusedWindow) { throw 'DLSS-G needs a focused presentation window; explicitly opt into this bounded sample run.' }
$Sample=(Resolve-Path -LiteralPath $SampleRoot).Path
$Output=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $Output) { throw 'Existing evidence must not be overwritten.' }
$Bin=Join-Path $Sample '_bin'
$Exe=Join-Path $Bin 'StreamlineSample.exe'
$Pins=@{
    'sl.interposer.dll'='2a79db6857ae8c75bbd871a9489c48bc6a39f7fcc88b9b02afd53d0376cbec66'
    'sl.common.dll'='c57930ef5a8a3fe9be85efdf71a61d8107c1148e8a6aed456464547128f7f4ae'
    'sl.dlss_g.dll'='1fec3f8fdfc59d78c4445c276c1a0fb798bf251985f348597dc2b44d0c995e52'
    'sl.reflex.dll'='7e6e4ccc4b561bd449fb0da90709d9b96b08c3f6f4697362caaa359e72a58a67'
    'sl.pcl.dll'='699ab461e64e95189a7fe6a21c79ad237cf56b60ea748cb6c840cd5431ba91d1'
    'nvngx_dlssg.dll'='135eaf0733c1e37381a8c28abcf7a862404a54132b81787c04e35d09efc5e36f'
}
$Signatures=@()
foreach($leaf in @('sl.interposer.dll','sl.common.dll','sl.dlss_g.dll','sl.reflex.dll','sl.pcl.dll','nvngx_dlssg.dll')) {
    $path=Join-Path $Bin $leaf
    $sig=Get-AuthenticodeSignature -LiteralPath $path
    if($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'NVIDIA') { throw "Invalid official component signature: $leaf" }
    if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $Pins[$leaf]) { throw "Pinned official SDK component changed: $leaf" }
    $Signatures += [ordered]@{file=$leaf;version=(Get-Item -LiteralPath $path).VersionInfo.FileVersion;
        sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant();signature='Valid NVIDIA'}
}
[IO.Directory]::CreateDirectory($Output) | Out-Null
$Arguments=@('-d3d12','-width','640','-height','360','-maxFrames',"$Frames",'-Reflex_mode','1','-Reflex_fpsCap','60','-DLSS_mode','0','-logToFile','-sllog')
if($EnableFG) { $Arguments+=@('-DLSSG_on','-DLSSG_numFrameToGenerate','1') }
$Modules=@{}
$Start=[DateTime]::UtcNow
# Explicit focused-window opt-in above authorizes this WIN32 renderer window.
# Hidden startup suppresses its first ShowWindow and prevents FG presentation.
# The sample requests focus only once, never after a later focus loss.
$Process=Start-Process -FilePath $Exe -ArgumentList $Arguments -WorkingDirectory $Bin -WindowStyle Normal -PassThru
$TimedOut=$false
while(!$Process.WaitForExit(100)) {
    try {
        foreach($Module in (Get-Process -Id $Process.Id -ErrorAction Stop).Modules) {
            if($Module.ModuleName -match '^(sl\.|nvngx|_nvngx|nvapi)') { $Modules[$Module.FileName]=$Module.ModuleName }
        }
    } catch { if(!$Process.HasExited) { Write-Verbose 'Module inventory temporarily unavailable.' } }
    if(([DateTime]::UtcNow-$Start).TotalSeconds -ge 30) {
        $TimedOut=$true
        Stop-Process -Id $Process.Id
        $Process.WaitForExit(5000) | Out-Null
        break
    }
}
$Elapsed=([DateTime]::UtcNow-$Start).TotalSeconds
$LogPath=Join-Path $Output 'sample.log'
Copy-Item -LiteralPath (Join-Path $Bin 'log.txt') -Destination $LogPath
$Log=Get-Content -LiteralPath $LogPath -Raw -Encoding UTF8
$States=@([regex]::Matches($Log,'FG_SAMPLE_STATE mode=(\d+) generatedRequested=(\d+) presented=(\d+) status=(\d+) fence=(\d+) completed=(\d+) inputComplete=(\d+) deviceResult=0x([0-9a-fA-F]+) vram=(\d+)'))
$Valid=@($States | Where-Object { $_.Groups[8].Value -eq '00000000' -and $_.Groups[5].Value -ne '18446744073709551615' -and $_.Groups[6].Value -ne '18446744073709551615' })
$Generated=@($Valid | Where-Object { [int]$_.Groups[3].Value -gt 1 -and $_.Groups[4].Value -eq '0' })
$FrameIds=@([regex]::Matches($Log,'FG_SAMPLE_SOURCE frame=(\d+)') | ForEach-Object { [int]$_.Groups[1].Value } | Sort-Object -Unique)
$Limits=@([regex]::Matches($Log,'FG_SAMPLE_LIMITS result=(\d+) minimum=(\d+) maximumGenerated=(\d+)'))
$Focused=@([regex]::Matches($Log,'FG_SAMPLE_SOURCE frame=\d+ animated=\d+ focused=1 '))
$Inventory=@(foreach($path in $Modules.Keys) {
    $sig=Get-AuthenticodeSignature -LiteralPath $path
    [ordered]@{path=$path;file=$Modules[$path];version=(Get-Item -LiteralPath $path).VersionInfo.FileVersion;
        sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant();signature=[string]$sig.Status}
})
$Report=[ordered]@{sample_commit=(& git -C $Sample rev-parse HEAD).Trim();nr_enabled=$false;fg_requested=[bool]$EnableFG;
    sample_exe_sha256=(Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash.ToLowerInvariant();
    actual_source_sha256=@(foreach($name in @('src/SLWrapper.cpp','src/main.cpp','src/StreamlineSample.cpp','donut/src/app/DeviceManager.cpp')) {
        [ordered]@{file=$name;sha256=(Get-FileHash -LiteralPath (Join-Path $Sample $name) -Algorithm SHA256).Hash.ToLowerInvariant()}
    });
    sdk_local_files=$Signatures;loaded_modules_private=$Inventory;exit_code=$Process.ExitCode;watchdog_timeout=$TimedOut;
    wall_seconds=$Elapsed;source_frames_observed=$FrameIds.Count;state_samples=$States.Count;valid_device_state_samples=$Valid.Count;
    sdk_multi_present_samples=$Generated.Count;max_presented=if($States.Count){[int](($States|ForEach-Object{[int]$_.Groups[3].Value}|Measure-Object -Maximum).Maximum)}else{0};
    focused_source_samples=$Focused.Count;backbuffer_width=640;backbuffer_height=360;
    state_query_results=@($Limits|ForEach-Object{[int]$_.Groups[1].Value}|Sort-Object -Unique);
    minimum_extent_values=@($Limits|ForEach-Object{[int]$_.Groups[2].Value}|Sort-Object -Unique);
    maximum_generated_values=@($Limits|ForEach-Object{[int]$_.Groups[3].Value}|Sort-Object -Unique);
    capability_supported=$Log.Contains('FG_SAMPLE_CAPABILITY result=0 ');
    focus_suppression_observed=$Log.Contains('window not focused');
    clean_plugin_shutdown=$Log.Contains('Shutting down plugin sl.dlss_g');
    game_verified=$false;generated_pixels_verified=$false;display_cadence_verified=$false;performance_verified=$false;
    log_sha256=(Get-FileHash -LiteralPath $LogPath -Algorithm SHA256).Hash.ToLowerInvariant()}
[IO.File]::WriteAllText((Join-Path $Output 'run.json'),($Report|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
Write-Output "Sample exit=$($Report.exit_code), sourceFrames=$($Report.source_frames_observed), capability=$($Report.capability_supported), maxPresented=$($Report.max_presented), multiPresentSamples=$($Generated.Count), timeout=$TimedOut"
Write-Output 'SDK presentation counters are not generated-pixel or display-cadence proof.'
if($TimedOut -or $Process.ExitCode -ne 0 -or $Valid.Count -ne $States.Count -or @($Limits|Where-Object{$_.Groups[1].Value -ne '0'}).Count) { throw 'Sample lifecycle, query or device-health validation failed; evidence retained.' }
