param(
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [Parameter(Mandatory = $true)][string]$ToolchainRoot,
    [Parameter(Mandatory = $true)][string]$ReShadeSourceRoot,
    [Parameter(Mandatory = $true)][string]$ReShadeDllPath,
    [ValidateSet('en', 'zh')][string]$Language = 'en'
)

# Real ImGui/ReShade/WARP drawing, constructed geometry/progress only. The
# fixture never loads Core/NVIDIA, invokes the capture controller or injects clicks.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path -Parent $PSScriptRoot
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a new evidence directory.' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
function Hash([string]$path) { return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
$report = [ordered]@{
    schema = 'nr050-face-diagnostic-ui-v1'; success = $false; failure = $null
    language = $Language; constructed_geometry_and_progress = $true
    production_ui_rendered = $false; warp_loop_verified = $false
    controller_invoked = $false; mouse_interactions_verified = $false
    nvidia_verified = $false; game_verified = $false; visual_review_required = $true
}
$child = $null
try {
    $compiler = Join-Path $ToolchainRoot 'bin/x86_64-w64-mingw32-clang++.exe'
    $sdkCommit = (& git -C $ReShadeSourceRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $sdkCommit -cne '4a50d1eddace85734871d91792ff214f13f66c01') {
        throw 'Require the established ReShade 19250 SDK source.'
    }
    if ((Get-Content -LiteralPath (Join-Path $ReShadeSourceRoot 'deps/imgui/imgui.h') -Raw) -notmatch '#define\s+IMGUI_VERSION_NUM\s+19250') {
        throw 'Unsupported ImGui function table.'
    }
    if ((Hash $ReShadeDllPath) -cne '0cee63f9c9f13f3ac909c5b4903f4dbb4b719a7ab3b4f13b0deaf83c814b94f7') {
        throw 'Require the established ReShade runtime binary.'
    }
    $hostBinary = Join-Path $OutputDirectory 'menu-host.exe'
    $sentinel = Join-Path $OutputDirectory 'sentinel.addon64'
    $hostSource = Join-Path $projectRoot 'tests/light_style_menu_host.cpp'
    $sentinelSource = Join-Path $projectRoot 'tests/face_diagnostic_ui_sentinel.cpp'
    $sourceHashes = [ordered]@{}
    foreach ($relative in @('tests/light_style_menu_host.cpp', 'tests/face_diagnostic_ui_sentinel.cpp',
        'src/nr_face_diagnostic_ui.inl', 'src/nr_face_region_selection.inl', 'src/nr_face_diagnostic.h', 'src/nr_face_capture_geometry.h',
        'src/nr_model_controls.h', 'src/nr_imgui_abi.h')) {
        $sourceHashes[$relative] = Hash (Join-Path $projectRoot $relative)
    }
    $report.source_files = $sourceHashes
    $report.compiler_sha256 = Hash $compiler
    $report.sdk_commit = $sdkCommit
    $report.reshade_sha256 = Hash $ReShadeDllPath
    & $compiler '-std=c++17' '-O2' '-Wall' '-Wextra' '-Werror' '-static' $hostSource `
        '-o' $hostBinary '-ld3d12' '-ldxgi' '-ldxguid' '-ldbghelp' '-luser32' *> (Join-Path $OutputDirectory 'compile-host.log')
    if ($LASTEXITCODE -ne 0) { throw 'Host compilation failed.' }
    & $compiler '-std=c++17' '-O2' '-Wall' '-Wextra' '-Werror' '-static' '-shared' `
        '-Wno-cast-function-type-mismatch' `
        '-isystem' (Join-Path $ReShadeSourceRoot 'deps/imgui') '-isystem' (Join-Path $ReShadeSourceRoot 'include') `
        '-I' (Join-Path $projectRoot 'src') $sentinelSource '-o' $sentinel '-luser32' *> (Join-Path $OutputDirectory 'compile-sentinel.log')
    if ($LASTEXITCODE -ne 0) { throw 'Production UI fixture compilation failed.' }
    $report.host_sha256 = Hash $hostBinary
    $report.sentinel_sha256 = Hash $sentinel
    Copy-Item -LiteralPath $ReShadeDllPath -Destination (Join-Path $OutputDirectory 'dxgi.dll')
    @'
[GENERAL]
NoEffectCache=1
[OVERLAY]
TutorialProgress=4
Language=en
[STYLE]
Font=C:\Windows\Fonts\msyh.ttc
FontSize=18
[SCREENSHOT]
SavePath=.\
FileFormat=1
SaveOverlayShot=1
'@ | Set-Content -LiteralPath (Join-Path $OutputDirectory 'ReShade.ini') -Encoding utf8
    ('[Fixture]' + "`nChinese=" + [int]($Language -eq 'zh')) |
        Set-Content -LiteralPath (Join-Path $OutputDirectory 'face-fixture.ini') -Encoding utf8
    $child = Start-Process -FilePath $hostBinary -WorkingDirectory $OutputDirectory -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $OutputDirectory 'host.log') `
        -RedirectStandardError (Join-Path $OutputDirectory 'host.err.log')
    if (!$child.WaitForExit(45000)) { throw 'Local UI host timed out.' }
    if ($child.ExitCode -ne 0) { throw "Local UI host failed with exit $($child.ExitCode)." }
    $hostLog = Get-Content -LiteralPath (Join-Path $OutputDirectory 'host.log') -Raw
    $sentinelLog = Get-Content -LiteralPath (Join-Path $OutputDirectory 'sentinel.log') -Raw
    if ($hostLog -notmatch '(?m)^hidden_window 1\r?$' -or $hostLog -notmatch '(?m)^COMPLETE 40\r?$' -or
        [regex]::Matches($hostLog, '(?m)^present_done \d+\r?$').Count -ne 40) { throw 'Incomplete hidden WARP loop.' }
    foreach ($marker in @('constructed_observations 1 1', 'model_module_absent 1 0', 'core_module_absent 1 0',
        'production_ui_drawn 8 0', 'production_ui_drawn 20 1', 'production_ui_drawn 32 0',
        'selection_registration 8 0', 'selection_registration 20 1', 'selection_registration 23 0',
        'selection_registration 40 1', 'destroy_selection_cleanup 1 0', 'diagnostic_page_requested 21 1')) {
        if ($sentinelLog -notmatch [regex]::Escape($marker)) { throw "Missing fixture evidence: $marker" }
    }
    if ($sentinelLog -match 'unexpected_capture_action|tick_without_runtime') { throw 'Unexpected fixture action.' }
    foreach ($callback in [regex]::Matches($sentinelLog, '(?m)^selection_callback (\d+) (\d+)\r?$')) {
        $frame = [int]$callback.Groups[1].Value
        if (($frame -lt 15 -or ($frame -gt 20 -and $frame -lt 37)) -or $callback.Groups[2].Value -ne '0') {
            throw 'Selection callback ran outside its requested interval or with the main panel open.'
        }
    }
    if ($Language -eq 'zh' -and $sentinelLog -notmatch 'chinese_glyphs_available 32 1') { throw 'Chinese glyphs unavailable.' }
    $screenshots = @()
    foreach ($postfix in @('panel', 'picker', 'progress')) {
        $matches = @(Get-ChildItem -LiteralPath $OutputDirectory -File -Filter "*face-ui-$postfix.png")
        if ($matches.Count -ne 1) { throw "Expected one $postfix screenshot." }
        $data = [IO.File]::ReadAllBytes($matches[0].FullName)
        if ($data.Length -lt 24 -or [BitConverter]::ToString($data[0..7]) -cne '89-50-4E-47-0D-0A-1A-0A') { throw 'Invalid screenshot PNG.' }
        $width = [uint32]$data[16]*16777216 + [uint32]$data[17]*65536 + [uint32]$data[18]*256 + $data[19]
        $height = [uint32]$data[20]*16777216 + [uint32]$data[21]*65536 + [uint32]$data[22]*256 + $data[23]
        if ($width -ne 1600 -or $height -ne 1600) { throw 'Unexpected screenshot dimensions.' }
        $screenshots += [ordered]@{name = $matches[0].Name; width = $width; height = $height; sha256 = Hash $matches[0].FullName}
    }
    $report.screenshots = $screenshots
    foreach ($relative in $sourceHashes.Keys) {
        if ((Hash (Join-Path $projectRoot $relative)) -cne $sourceHashes[$relative]) { throw "Source changed during fixture: $relative" }
    }
    $report.overlay_events = [regex]::Matches($sentinelLog, '(?m)^overlay_event ').Count
    $report.production_ui_rendered = $true
    $report.warp_loop_verified = $true
    $report.success = $true
    Write-Output 'PASS production face UI drawn in hidden ReShade/WARP host: 40 frames and three captures. Geometry/progress were constructed; no clicks, controller, NR or game validation.'
} catch {
    $report.failure = $_.Exception.Message
    throw
} finally {
    if ($null -ne $child -and !$child.HasExited) { $child.Kill(); [void]$child.WaitForExit(3000) }
    $report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'validation.json') -Encoding utf8
}
