[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RecorderPath,
    [Parameter(Mandatory=$true)][string]$CaptureRoot,
    [Parameter(Mandatory=$true)][ValidateSet('NRSR','SRNR')][string]$Route,
    [string]$ProcessName='HTGame',
    [ValidateRange(30,180)][int]$Seconds=45,
    [switch]$PreflightOnly
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$Recorder=[IO.Path]::GetFullPath($RecorderPath)
$Destination=[IO.Path]::GetFullPath($CaptureRoot)
if (-not (Test-Path -LiteralPath $Recorder -PathType Leaf)) { throw 'Recorder script not found.' }
$DriveRoot=[IO.Path]::GetPathRoot($Destination)
if ($DriveRoot -notmatch '^([A-Za-z]):\\$') { throw 'Choose a local drive for this capture.' }
$Drive=Get-PSDrive -Name $Matches[1]
# Do not stop another agent, game compiler, security service or process. Merely
# refuse a contaminated benchmark. Idle IDEs are not treated as active compilers.
$Busy=@(Get-Process -Name 'compile-codec-artifact','ShaderCompileWorker','clang','clang++','clang-cl','cl','link','MSBuild' -ErrorAction SilentlyContinue)
$Ready=$Drive.Free -ge 6GB -and $Busy.Count -eq 0
$Check=[ordered]@{ready=$Ready;route=$Route;freeBytes=$Drive.Free;minimumFreeBytes=6GB;
    activeCompilers=@($Busy | Select-Object ProcessName,Id);recorderStarted=$false;settingsChanged=$false}
if ($PreflightOnly) { $Check | ConvertTo-Json -Depth 4; return }
if ($Busy.Count) {
    $Busy | Select-Object ProcessName,Id | Format-Table | Out-Host
    throw 'Active compiler detected. Finish/pause that build before benchmarking; no process was stopped.'
}
if (-not $Ready) { throw 'Capture drive needs at least 6 GiB free. No files were deleted.' }
Write-Host ('Route: '+$Route+'. Keep the matching baseline scene, settings and actual NR work extent.')
Write-Host 'Manual OFF 15 s -> ON 15 s -> OFF 15 s. Save F8 AFTER recording. This script does not press F6.'
Write-Host 'Name this run Perf9-NRSR or Perf9-SRNR. No plugin installation or INI change is performed.'
& $Recorder -ProcessName $ProcessName -Root $Destination -Seconds $Seconds -Repeat 1 -State 'OFF-ON-OFF' -Note ('Perf9 '+$Route+'; compiler preflight passed; preserve matching settings and work extent; F8 after capture')
