param(
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [Parameter(Mandatory = $true)][string]$ToolchainRoot,
    [Parameter(Mandatory = $true)][string]$ReShadeSourceRoot,
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot),
    [switch]$CompileOnly,
    [string]$CorePath,
    [string]$CoreSha256,
    [string]$CoreSourceCommit,
    [string]$SourceManifestPath,
    [string]$BuildValidationPath,
    [string]$ReShadeDllPath,
    [string]$ReShadeSha256 = '0cee63f9c9f13f3ac909c5b4903f4dbb4b719a7ab3b4f13b0deaf83c814b94f7',
    [string]$ExpectedSdkCommit = '4a50d1eddace85734871d91792ff214f13f66c01',
    [string]$ExpectedVersion = 'beta0.5-dline7',
    [string]$ExpectedPeVersion = '0.5.1.7',
    [ValidateSet(0,1)][int]$LightControlMode = 1
)

# Real hidden-host integration, not a game or preset-click simulation. Generated
# DLLs, logs, screenshots, paths and crash evidence belong only to OutputDirectory.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$ToolchainRoot = (Resolve-Path -LiteralPath $ToolchainRoot).Path
$ReShadeSourceRoot = (Resolve-Path -LiteralPath $ReShadeSourceRoot).Path
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a new output directory; previous evidence is never overwritten.' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$reportPath = Join-Path $OutputDirectory 'menu-validation.json'
$report = [ordered]@{
    schema = 'nr050-actual-menu-v1'; compile_only = [bool]$CompileOnly
    success = $false; failure = $null; warp_loop_verified = $false
    actual_core_module_verified = $false; core_window_visual_verified = $false
    light_style_group_visual_verified = $false; preset_clicks_verified = $false
    nvidia_feature18_verified = $false; game_verified = $false
    visual_review_required = $true; frames = 0; overlay_events = 0
    fixture_light_control_mode = $LightControlMode
}
function Hash([string]$path) { return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Resolve-File([string]$path) {
    if ([string]::IsNullOrWhiteSpace($path) -or !(Test-Path -LiteralPath $path -PathType Leaf)) { throw 'A required input file is missing.' }
    return (Resolve-Path -LiteralPath $path).Path
}
function Check-Hash([string]$path, [string]$expected) {
    if ($expected -notmatch '^[a-fA-F0-9]{64}$' -or (Hash $path) -ine $expected) { throw "Hash mismatch: $path" }
}
function Verify-CoreInput {
    Check-Hash $CorePath $CoreSha256
    Check-Hash $ReShadeDllPath $ReShadeSha256
    if ($CoreSourceCommit -notmatch '^[a-fA-F0-9]{40}$') { throw 'Require the exact Core source commit.' }
    $manifest = Get-Content -LiteralPath $SourceManifestPath -Raw | ConvertFrom-Json
    $build = Get-Content -LiteralPath $BuildValidationPath -Raw | ConvertFrom-Json
    if (!$build.success -or $manifest.source_commit -ine $CoreSourceCommit -or
        $build.source_commit -ine $CoreSourceCommit -or $manifest.experiment -cne $ExpectedVersion) {
        throw 'Source manifest, build receipt, version and requested Core commit disagree.'
    }
    Check-Hash $SourceManifestPath $build.source_manifest_sha256
    $coreFile = Get-Item -LiteralPath $CorePath
    $artifactMatches = @($build.artifacts.PSObject.Properties | Where-Object {
        $_.Name -like '*.addon64' -and $_.Value.sha256 -ieq $CoreSha256 -and $_.Value.bytes -eq $coreFile.Length
    })
    if ($artifactMatches.Count -ne 1) { throw 'The exact Core binary is not uniquely present in the build receipt.' }
    $sourceRoot = Split-Path -Parent $SourceManifestPath
    $sourcePrefix = [IO.Path]::GetFullPath($sourceRoot).TrimEnd([char[]]"\/") + [IO.Path]::DirectorySeparatorChar
    $files = @($manifest.files.PSObject.Properties)
    if ($files.Count -eq 0 -or !$manifest.files.PSObject.Properties['core/src/nr_before_sr.cpp']) {
        throw 'Require a full generated-source manifest containing the actual Core source.'
    }
    foreach ($file in $files) {
        $path = [IO.Path]::GetFullPath((Join-Path $sourceRoot $file.Name))
        if (!$path.StartsWith($sourcePrefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'Manifest path escapes its source root.' }
        Check-Hash $path ([string]$file.Value)
    }
    $version = [Diagnostics.FileVersionInfo]::GetVersionInfo($CorePath)
    $numericVersion = '{0}.{1}.{2}.{3}' -f $version.FileMajorPart,$version.FileMinorPart,$version.FileBuildPart,$version.FilePrivatePart
    if ($numericVersion -cne $ExpectedPeVersion) { throw "Unexpected Core PE identity: $numericVersion" }
    return [ordered]@{
        source_commit = $CoreSourceCommit; source_manifest_sha256 = Hash $SourceManifestPath
        build_validation_sha256 = Hash $BuildValidationPath; verified_source_files = $files.Count
        core_sha256 = Hash $CorePath; core_bytes = $coreFile.Length
        artifact_name = $artifactMatches[0].Name; pe_version = $numericVersion
        expected_version = $ExpectedVersion; reshade_sha256 = Hash $ReShadeDllPath
    }
}

$child = $null
try {
    $compiler = Resolve-File (Join-Path $ToolchainRoot 'bin/x86_64-w64-mingw32-clang++.exe')
    $sdkCommit = (& git -C $ReShadeSourceRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $sdkCommit -cne $ExpectedSdkCommit) { throw 'ReShade SDK source is not the requested fixed commit.' }
    $imgui = Resolve-File (Join-Path $ReShadeSourceRoot 'deps/imgui/imgui.h')
    $overlay = Resolve-File (Join-Path $ReShadeSourceRoot 'include/reshade_overlay.hpp')
    if ((Get-Content -LiteralPath $imgui -Raw) -notmatch '#define\s+IMGUI_VERSION_NUM\s+19250') {
        throw 'This fixture requires the fixed ImGui 19250 function table.'
    }
    $hostSource = Resolve-File (Join-Path $ProjectRoot 'tests/light_style_menu_host.cpp')
    $sentinelSource = Resolve-File (Join-Path $ProjectRoot 'tests/light_style_menu_sentinel.cpp')
    $hostBinary = Join-Path $OutputDirectory 'menu-host.exe'
    $sentinel = Join-Path $OutputDirectory 'sentinel.addon64'
    $report.fixture_inputs = [ordered]@{
        compiler_sha256 = Hash $compiler; reshade_sdk_commit = $sdkCommit
        imgui_header_sha256 = Hash $imgui; overlay_header_sha256 = Hash $overlay
        host_source_sha256 = Hash $hostSource; sentinel_source_sha256 = Hash $sentinelSource
        script_sha256 = Hash $PSCommandPath
    }
    $hostArgs = @('-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',$hostSource,
        '-o',$hostBinary,'-ld3d12','-ldxgi','-ldxguid','-ldbghelp','-luser32')
    & $compiler @hostArgs *> (Join-Path $OutputDirectory 'compile-host.log')
    if ($LASTEXITCODE -ne 0) { throw 'Menu host compilation failed; see compile-host.log.' }
    $sentinelArgs = @('-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-shared',
        '-isystem',(Join-Path $ReShadeSourceRoot 'deps/imgui'),'-isystem',(Join-Path $ReShadeSourceRoot 'include'),
        $sentinelSource,'-o',$sentinel,'-luser32')
    & $compiler @sentinelArgs *> (Join-Path $OutputDirectory 'compile-sentinel.log')
    if ($LASTEXITCODE -ne 0) { throw 'Menu sentinel compilation failed; see compile-sentinel.log.' }
    $report.fixture_binaries = [ordered]@{host_sha256 = Hash $hostBinary; sentinel_sha256 = Hash $sentinel}
    if ($CompileOnly) {
        $report.success = $true
        $report.visual_review_required = $false
        Write-Output 'PASS compile only: host and sentinel built; no process or GPU started.'
        return
    }

    $CorePath = Resolve-File $CorePath
    $ReShadeDllPath = Resolve-File $ReShadeDllPath
    $SourceManifestPath = Resolve-File $SourceManifestPath
    $BuildValidationPath = Resolve-File $BuildValidationPath
    $report.core_input = Verify-CoreInput
    $run = Join-Path $OutputDirectory 'run'
    New-Item -ItemType Directory -Path $run | Out-Null
    Copy-Item -LiteralPath $hostBinary -Destination (Join-Path $run 'menu-host.exe')
    Copy-Item -LiteralPath $sentinel -Destination (Join-Path $run 'sentinel.addon64')
    Copy-Item -LiteralPath $CorePath -Destination (Join-Path $run 'core-under-test.addon64')
    Copy-Item -LiteralPath $ReShadeDllPath -Destination (Join-Path $run 'dxgi.dll')
    Check-Hash (Join-Path $run 'menu-host.exe') $report.fixture_binaries.host_sha256
    Check-Hash (Join-Path $run 'sentinel.addon64') $report.fixture_binaries.sentinel_sha256
    Check-Hash (Join-Path $run 'core-under-test.addon64') $CoreSha256
    Check-Hash (Join-Path $run 'dxgi.dll') $ReShadeSha256
    @'
[GENERAL]
NoEffectCache=1
[OVERLAY]
TutorialProgress=4
[SCREENSHOT]
SavePath=.\
FileFormat=1
SaveOverlayShot=1
'@ | Set-Content -LiteralPath (Join-Path $run 'ReShade.ini') -Encoding utf8
    @'
[NRBeforeSR]
TransferStrength=1
PostTransferStrength=1
StrengthConfigVersion=1
Mode=2
ConfigVersion=4
WorkMode=0
CustomWorkScale=1
'@ | Set-Content -LiteralPath (Join-Path $run 'nr_before_sr.ini') -Encoding utf8
    $iniPath = Join-Path $run 'nr_before_sr.ini'
    Add-Content -LiteralPath $iniPath -Value ('LightControlMode=' + $LightControlMode) -Encoding utf8
    $iniBefore = Hash $iniPath
    $child = Start-Process -FilePath (Join-Path $run 'menu-host.exe') -WorkingDirectory $run -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $run 'host.log') -RedirectStandardError (Join-Path $run 'host.err.log')
    $report.started_pid = $child.Id
    if (!$child.WaitForExit(30000)) {
        $child.Kill() # Only the process created immediately above; never a process-name search.
        [void]$child.WaitForExit(3000)
        throw 'The owned host exceeded its 30-second bound; evidence is preserved.'
    }
    $report.exit_code = $child.ExitCode
    if ($child.ExitCode -ne 0) { throw "Owned host failed with exit code $($child.ExitCode)." }
    $hostLog = Get-Content -LiteralPath (Join-Path $run 'host.log') -Raw
    $sentinelLog = Get-Content -LiteralPath (Join-Path $run 'sentinel.log') -Raw
    $reshadeLog = Get-Content -LiteralPath (Join-Path $run 'ReShade.log') -Raw
    $frameMatches = [regex]::Matches($hostLog,'(?m)^present_done (\d+)\r?$')
    $report.frames = $frameMatches.Count
    $frameSequence = @($frameMatches | ForEach-Object { $_.Groups[1].Value }) -join ','
    $report.overlay_events = [regex]::Matches($sentinelLog,'(?m)^overlay_event \d+ [01]\r?$').Count
    $report.overlay_event_sequence = @([regex]::Matches($sentinelLog,'(?m)^overlay_event (\d+) ([01])\r?$') |
        ForEach-Object { '{0}:{1}' -f $_.Groups[1].Value,$_.Groups[2].Value }) -join ','
    $requests = [regex]::Matches($sentinelLog,'(?m)^open_return (3|10|15|22|27|34) 1\r?$').Count
    if ($frameSequence -cne ((1..40) -join ',') -or $report.overlay_events -ne 6 -or $requests -ne 6 -or
        $report.overlay_event_sequence -cne '1:1,2:0,3:1,4:0,5:1,6:0' -or
        $hostLog -notmatch '(?m)^COMPLETE 40\r?$' -or $hostLog -notmatch '(?m)^hidden_window 1\r?$') {
        throw 'Incomplete 40-frame/6-event hidden menu loop.'
    }
    if ($sentinelLog -notmatch '(?m)^actual_core_module 1 0\r?$' -or
        $reshadeLog -notmatch 'core-under-test\.addon64' -or $reshadeLog -notmatch [regex]::Escape($ExpectedVersion)) {
        throw 'Actual Core load and requested version were not observed.'
    }
    if ([regex]::Matches($sentinelLog,'(?m)^focus_requested (4|16|28) 1\r?$').Count -ne 3 -or
        [regex]::Matches($sentinelLog,'(?m)^screenshot_request (8|20|32) 1\r?$').Count -ne 3) {
        throw 'Missing actual Core focus or screenshot API evidence.'
    }
    $screenshots = @(Get-ChildItem -LiteralPath $run -File -Filter '*core-menu-*.png')
    if ($screenshots.Count -ne 3) { throw 'Expected three captured menu images.' }
    foreach ($postfix in @('first','second','third')) {
        if (@($screenshots | Where-Object { $_.Name -like "*core-menu-$postfix.png" }).Count -ne 1) {
            throw 'Missing a distinct capture for one of the three menu openings.'
        }
    }
    $report.screenshots = @($screenshots | ForEach-Object {
        $data = [IO.File]::ReadAllBytes($_.FullName)
        if ($data.Length -lt 24 -or [BitConverter]::ToString($data[0..7]) -cne '89-50-4E-47-0D-0A-1A-0A') { throw 'Invalid screenshot PNG.' }
        $width = [uint32]$data[16]*16777216 + [uint32]$data[17]*65536 + [uint32]$data[18]*256 + $data[19]
        $height = [uint32]$data[20]*16777216 + [uint32]$data[21]*65536 + [uint32]$data[22]*256 + $data[23]
        if ($width -ne 1600 -or $height -ne 1600) { throw 'Unexpected menu capture dimensions.' }
        [ordered]@{name = $_.Name; width = $width; height = $height; sha256 = Hash $_.FullName}
    })
    $report.ini_before_sha256 = $iniBefore
    $report.ini_after_sha256 = Hash $iniPath
    $report.ini_changed = $report.ini_before_sha256 -cne $report.ini_after_sha256
    $report.preset_interaction_note = 'No preset clicks were injected. INI differences are not attributed to a preset transaction.'
    $report.core_input_after = Verify-CoreInput
    Check-Hash (Join-Path $run 'core-under-test.addon64') $CoreSha256
    Check-Hash (Join-Path $run 'dxgi.dll') $ReShadeSha256
    Check-Hash (Join-Path $run 'menu-host.exe') $report.fixture_binaries.host_sha256
    Check-Hash (Join-Path $run 'sentinel.addon64') $report.fixture_binaries.sentinel_sha256
    $report.warp_loop_verified = $true
    $report.actual_core_module_verified = $true
    $report.success = $true
    Write-Output 'PASS owned WARP menu loop: 40 frames, six real open/close events and three captures. Visually inspect the actual Core/settings content before marking UI verified.'
} catch {
    $report.failure = $_.Exception.Message
    throw
} finally {
    if ($null -ne $child -and !$child.HasExited) {
        $child.Kill(); [void]$child.WaitForExit(3000)
    }
    $report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $reportPath -Encoding utf8
}
