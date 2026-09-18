param([Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$Output=[IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $Output) { throw 'Use a fresh output; previous evidence is preserved.' }
New-Item -ItemType Directory -Path $Output | Out-Null
$Entry=Join-Path $PSScriptRoot 'record-perf9-comparison.ps1'
$Dummy=Join-Path $Output 'dummy-recorder.ps1'
$Receipt=Join-Path $Output 'dummy-call.json'
$ScriptBody=@'
param([string]$ProcessName,[string]$Root,[int]$Seconds,[int]$Repeat,[string]$State,[string]$Note)
$PSBoundParameters | ConvertTo-Json | Set-Content (Join-Path $PSScriptRoot 'dummy-call.json') -Encoding UTF8
'@
[IO.File]::WriteAllText($Dummy,$ScriptBody)
$global:Perf9CompilerBusy=$true
$global:Perf9DriveFree=20GB
function Get-Process { [CmdletBinding()]param([string[]]$Name)
    if ($global:Perf9CompilerBusy) { [pscustomobject]@{ProcessName='compile-codec-artifact';Id=42} }
}
function Get-PSDrive { param([string]$Name) [pscustomobject]@{Free=$global:Perf9DriveFree} }
$Args=@{RecorderPath=$Dummy;CaptureRoot=(Join-Path $Output 'captures');Route='NRSR'}
try {
    $Probe=(& $Entry @Args -PreflightOnly | Out-String | ConvertFrom-Json)
    if ($Probe.ready -or $Probe.recorderStarted -or $Probe.activeCompilers.Count -ne 1) { throw 'Busy preflight report failed.' }
    $Rejected=$false
    try { & $Entry @Args } catch { $Rejected=$_.Exception.Message -match 'Active compiler detected' }
    if (-not $Rejected -or (Test-Path $Receipt)) { throw 'Busy build did not prevent recorder entry.' }
    $global:Perf9CompilerBusy=$false;$global:Perf9DriveFree=1GB
    $Rejected=$false
    try { & $Entry @Args } catch { $Rejected=$_.Exception.Message -match 'at least 6 GiB' }
    if (-not $Rejected -or (Test-Path $Receipt)) { throw 'Low space did not prevent recorder entry.' }
    $global:Perf9DriveFree=20GB
    $Probe=(& $Entry @Args -PreflightOnly | Out-String | ConvertFrom-Json)
    if (-not $Probe.ready -or (Test-Path $Receipt)) { throw 'Read-only ready preflight started recorder.' }
    $Args.Route='SRNR'
    & $Entry @Args
    $Call=Get-Content $Receipt -Raw | ConvertFrom-Json
    if ($Call.State -ne 'OFF-ON-OFF' -or $Call.Seconds -ne 45 -or $Call.Repeat -ne 1 -or $Call.Note -notmatch 'Perf9 SRNR') { throw 'Clean entry changed recorder arguments.' }
    @{status='passed';mock_process_and_drive=$true;real_recorder_started=$false;processes_stopped=$false;tests=5} | ConvertTo-Json | Set-Content (Join-Path $Output 'validation.json') -Encoding UTF8
    Write-Host 'PASS 5 recorder preflight cases: busy/low-space block, read-only status, clean forwarding; dummy recorder only.'
    $global:LASTEXITCODE=0
} finally {
    Remove-Item Function:\Get-Process,Function:\Get-PSDrive -ErrorAction SilentlyContinue
    Remove-Variable Perf9CompilerBusy,Perf9DriveFree -Scope Global -ErrorAction SilentlyContinue
}
