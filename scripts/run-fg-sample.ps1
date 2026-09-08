param(
    [Parameter(Mandatory=$true)][string]$SampleRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [ValidateRange(30,600)][int]$Frames=120,
    [switch]$EnableFG,
    [switch]$EnableSR,
    [string]$CoreDirectory,
    [switch]$EnableNR,
    [switch]$AllowFocusedWindow,
    [string]$DevelopmentSdkRoot,
    [switch]$PrepareOnly
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if (!$PrepareOnly -and !$AllowFocusedWindow) { throw 'DLSS-G needs a focused presentation window; explicitly opt into this bounded sample run.' }
$Sample=(Resolve-Path -LiteralPath $SampleRoot).Path
$Output=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $Output) { throw 'Existing evidence must not be overwritten.' }
$Bin=Join-Path $Sample '_bin'
$Exe=Join-Path $Bin 'StreamlineSample.exe'
if ($CoreDirectory -and (!$DevelopmentSdkRoot -or !$EnableSR)) { throw 'The controlled Core chain requires the fixed development SDK and SR.' }
if ($EnableNR -and !$CoreDirectory) { throw 'NR requires an explicit controlled Core candidate.' }
$Pins=@{
    'sl.interposer.dll'='2a79db6857ae8c75bbd871a9489c48bc6a39f7fcc88b9b02afd53d0376cbec66'
    'sl.common.dll'='c57930ef5a8a3fe9be85efdf71a61d8107c1148e8a6aed456464547128f7f4ae'
    'sl.dlss_g.dll'='1fec3f8fdfc59d78c4445c276c1a0fb798bf251985f348597dc2b44d0c995e52'
    'sl.reflex.dll'='7e6e4ccc4b561bd449fb0da90709d9b96b08c3f6f4697362caaa359e72a58a67'
    'sl.pcl.dll'='699ab461e64e95189a7fe6a21c79ad237cf56b60ea748cb6c840cd5431ba91d1'
    'nvngx_dlssg.dll'='135eaf0733c1e37381a8c28abcf7a862404a54132b81787c04e35d09efc5e36f'
}
if ($DevelopmentSdkRoot) {
    # Development libraries are not production-signed. Accept only these exact
    # files from the already pinned official 2.12 archive, in a fresh private stage.
    $Pins=@{
        'sl.interposer.dll'='f54318c050d7c13528fc1560066584c33766694084f5c416a02ec096884bdb44'
        'sl.common.dll'='b1b4017343a5fbcf2edad190ae0de229725648c7b9f1975ba30563df8371ebee'
        'sl.dlss_g.dll'='fa1f72c54ea3efc92baa5574d112e7b04c4d10949d394c8ec9a0aeba51963a3e'
        'sl.reflex.dll'='84e09f064cddd4712b78105bacf98c69bb4192dbfebe797f1f49bc3206fabced'
        'sl.pcl.dll'='5ce4a529ccb3c47892fb89e1c45eaa6fdad1bc8059ae70ff520bbbacc5bd6502'
        'nvngx_dlssg.dll'='0d33b5de65d60a943c33bd096d574297302b214a1c5baa6bcb74bc1150a608de'
        'WinPixEventRuntime.dll'='0b71e83dbe737c51ab7345541a4914790962e9a9466b74c4c076fc4920a66a65'
    }
    $DevBin=Join-Path (Resolve-Path -LiteralPath $DevelopmentSdkRoot).Path 'bin/x64/development'
    if ($EnableSR) {
        $Pins['sl.dlss.dll']='d2200487675c31479bbe514792d661a963aba49cbc497dc6fe60a9c409508b19'
        $Pins['nvngx_dlss.dll']='1cdfdc957cb7fc9805500ca6793f607ef2d4dbd7c967feac70aca9caf382d0c0'
    }
    foreach($leaf in $Pins.Keys) {
        if((Get-FileHash -LiteralPath (Join-Path $DevBin $leaf)).Hash -ne $Pins[$leaf]) { throw "Development SDK hash mismatch: $leaf" }
    }
    # Sibling of _bin preserves upstream ../media resolution. Do not inherit
    # upstream JSONs (console/debugger paths/NGX logging overrides).
    $Stage=Join-Path $Sample ('_bin-fg-diagnostic-'+[guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($Stage) | Out-Null
    Copy-Item -LiteralPath $Exe -Destination $Stage
    Copy-Item -LiteralPath (Join-Path $Bin 'shaders') -Destination $Stage -Recurse
    foreach($leaf in $Pins.Keys) { Copy-Item -LiteralPath (Join-Path $DevBin $leaf) -Destination $Stage }
    $Bin=$Stage
    $Exe=Join-Path $Bin 'StreamlineSample.exe'
    [IO.File]::WriteAllText((Join-Path $Bin 'sl.interposer.json'),(@{
        showConsole=$false;logLevel=2;logMessageDelayMs=0;waitForDebugger=$false;logPath=$Bin
        forceProxies=$false;trackEngineAllocations=$false
    }|ConvertTo-Json))
    [IO.File]::WriteAllText((Join-Path $Bin 'sl.common.json'),'{"logLevelNGX":2,"enableOTA":false}')
    [IO.File]::WriteAllText((Join-Path $Bin 'sl.dlss_g.json'),'{"showDebugText":true}')
}
$CorePins=@{}
if ($CoreDirectory) {
    $CoreSource=(Resolve-Path -LiteralPath $CoreDirectory).Path
    # Reuse this separately frozen, already built external-provider candidate.
    # These hashes do not relabel it as containing later diagnostic changes.
    $CorePins=@{
        'core.dll'='8e9394889c3fd3309e035bc67e26d4d25c5b711e4a392f86f5e1c89a5a1caf38'
        'nrchain_nvngx.dll'='46041a5ff91ae2fd907e310d132aabc3c4a1ecd48dace511b8672909d5d9c2fb'
        'nvngx_dlssnr.dll'='6eb209e764f39872625debd6abaf45e2bb6322f6f270f781f70c059ae30b3927'
    }
    foreach($leaf in $CorePins.Keys) {
        if((Get-FileHash -LiteralPath (Join-Path $CoreSource $leaf)).Hash -ne $CorePins[$leaf]) { throw "Frozen Core component mismatch: $leaf" }
    }
    foreach($leaf in $CorePins.Keys) { Copy-Item -LiteralPath (Join-Path $CoreSource $leaf) -Destination $Bin }
    $nrEnabled=if($EnableNR){1}else{0}
    [IO.File]::WriteAllText((Join-Path $Bin 'nr_before_sr.ini'),"[NRBeforeSR]`nEnabled=$nrEnabled`nConfigVersion=4`nMode=2`nStrengthConfigVersion=2`nTransferStrength=1.0`nWorkMode=0`n")
}
$Signatures=@()
foreach($leaf in $Pins.Keys) {
    $path=Join-Path $Bin $leaf
    $sig=Get-AuthenticodeSignature -LiteralPath $path
    if(!$DevelopmentSdkRoot -and ($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'NVIDIA')) { throw "Invalid official component signature: $leaf" }
    if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $Pins[$leaf]) { throw "Pinned official SDK component changed: $leaf" }
    $Signatures += [ordered]@{file=$leaf;version=(Get-Item -LiteralPath $path).VersionInfo.FileVersion;
        sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant();signature=[string]$sig.Status}
}
[IO.Directory]::CreateDirectory($Output) | Out-Null
if($PrepareOnly) {
    [IO.File]::WriteAllText((Join-Path $Output 'prepared.json'),(@{
        binary_directory_private=$Bin;sample_exe_sha256=(Get-FileHash $Exe).Hash.ToLowerInvariant();
        development=[bool]$DevelopmentSdkRoot;components=$Signatures;runtime_executed=$false
        sr_requested=[bool]$EnableSR;nr_requested=[bool]$EnableNR;core_components=$CorePins
    }|ConvertTo-Json -Depth 5))
    Write-Output 'Diagnostic stage prepared and hashes checked; no GPU process launched.'
    return
}
$SrMode=if($EnableSR){'3'}else{'0'} # sl::DLSSMode::eMaxQuality / eOff.
$Arguments=@('-d3d12','-width','640','-height','360','-maxFrames',"$Frames",'-Reflex_mode','1','-Reflex_fpsCap','60','-DLSS_mode',$SrMode,'-logToFile','-sllog')
if($DevelopmentSdkRoot) { $Arguments+='-noSigCheck' }
if($EnableFG) { $Arguments+=@('-DLSSG_on','-DLSSG_numFrameToGenerate','1') }
$Modules=@{}
$Start=[DateTime]::UtcNow
# Explicit focused-window opt-in above authorizes this WIN32 renderer window.
# Hidden startup suppresses its first ShowWindow and prevents FG presentation.
# The sample requests focus only once, never after a later focus loss.
$StartInfo=[Diagnostics.ProcessStartInfo]::new()
$StartInfo.FileName=$Exe; $StartInfo.Arguments=($Arguments -join ' ')
$StartInfo.WorkingDirectory=$Bin; $StartInfo.UseShellExecute=$false
$StartInfo.WindowStyle=[Diagnostics.ProcessWindowStyle]::Normal
$StartInfo.EnvironmentVariables.Remove('NR_FG_SAMPLE_CORE')
if($CoreDirectory) { $StartInfo.EnvironmentVariables['NR_FG_SAMPLE_CORE']=Join-Path $Bin 'core.dll' }
$Process=[Diagnostics.Process]::Start($StartInfo)
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
$NrRecords=@([regex]::Matches($Log,'NR_FG_CHAIN frame=(\d+) epoch=(\d+) R=(\d+)x(\d+) accepted=(\d+) recorded=(\d+) reason=([^ ]+) reset=(\d+) provenance=renderer end=1'))
$NrWrites=@($NrRecords | Where-Object {$_.Groups[6].Value -eq '1'})
$NrFences=@([regex]::Matches($Log,'NR_FG_CHAIN completedFence=(\d+) end=1'))
if($CoreDirectory) {
    foreach($leaf in @('nr-before-sr.log','nr_before_sr.ini')) {
        if(Test-Path -LiteralPath (Join-Path $Bin $leaf)) { Copy-Item -LiteralPath (Join-Path $Bin $leaf) -Destination $Output }
    }
}
$Inventory=@(foreach($path in $Modules.Keys) {
    $sig=Get-AuthenticodeSignature -LiteralPath $path
    [ordered]@{path=$path;file=$Modules[$path];version=(Get-Item -LiteralPath $path).VersionInfo.FileVersion;
        sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant();signature=[string]$sig.Status}
})
$Report=[ordered]@{sample_commit=(& git -C $Sample rev-parse HEAD).Trim();nr_requested=[bool]$EnableNR;fg_requested=[bool]$EnableFG;
    sr_requested=[bool]$EnableSR;core_components=$CorePins;core_source=if($CoreDirectory){'e7df0fcf26f28e9661a8dcfe0322377278820dd6'}else{$null};
    nr_frames_observed=$NrRecords.Count;nr_writebacks_recorded=$NrWrites.Count;nr_completed_batches=$NrFences.Count;
    nr_reasons=@($NrRecords|ForEach-Object{$_.Groups[7].Value}|Sort-Object -Unique);
    development_diagnostics=[bool]$DevelopmentSdkRoot;
    diagnostics_revision_2=$Log.Contains('FG_SAMPLE_DIAGNOSTICS revision=2 ');
    sdk_present_counter_total=($Valid|ForEach-Object{[int]$_.Groups[3].Value}|Measure-Object -Sum).Sum;
    state_queries_per_source_frame=if($FrameIds.Count){$States.Count / $FrameIds.Count}else{0};
    vram_estimate_queries=@([regex]::Matches($Log,'FG_SAMPLE_VRAM_ESTIMATE requested=1 result=(\d+) bytes=(\d+)')|ForEach-Object{@{result=[int]$_.Groups[1].Value;bytes=$_.Groups[2].Value}});
    asynchronous_api_errors=@([regex]::Matches($Log,'FG_SAMPLE_API_ERROR hresult=0x([0-9a-fA-F]+)')|ForEach-Object{$_.Groups[1].Value});
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
if($TimedOut -or $Process.ExitCode -ne 0 -or !$States.Count -or !$Report.diagnostics_revision_2 -or $States.Count -ne $FrameIds.Count -or $Valid.Count -ne $States.Count -or @($Limits|Where-Object{$_.Groups[1].Value -ne '0'}).Count -or $Report.asynchronous_api_errors.Count) { throw 'Sample lifecycle, diagnostic revision, query or device-health validation failed; evidence retained.' }
if($CoreDirectory -and (!$NrRecords.Count -or $NrRecords.Count -ne $NrFences.Count -or
    ($EnableNR -and !$NrWrites.Count) -or (!$EnableNR -and $NrWrites.Count) -or
    @($NrRecords|Where-Object{$_.Groups[7].Value -in @('epoch-mismatch','view-mismatch','frame-mismatch')}).Count -or
    (!$EnableNR -and @($NrRecords|Where-Object{$_.Groups[7].Value -ne 'master-disabled'}).Count))) { throw 'Requested NR/source completion contract was not observed; evidence retained.' }
