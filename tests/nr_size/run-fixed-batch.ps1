param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$BootstrapDirectory,
    [Parameter(Mandatory=$true)][string]$NrDirectory,
    [Parameter(Mandatory=$true)][string]$EvidenceDirectory,
    [switch]$AuthorizedRun
)
$ErrorActionPreference='Stop'
if (!$AuthorizedRun) { throw 'Requires reviewed single-batch GPU authorization.' }
if (Test-Path -LiteralPath $EvidenceDirectory) { throw 'Refusing an existing evidence directory or repeated batch.' }
$sizeExe=(Resolve-Path -LiteralPath $Executable).Path
$sizeSdk=(Resolve-Path -LiteralPath $BootstrapDirectory).Path
$sizeNr=(Resolve-Path -LiteralPath $NrDirectory).Path
New-Item -ItemType Directory -Path $EvidenceDirectory | Out-Null
$sizeEvidence=(Resolve-Path -LiteralPath $EvidenceDirectory).Path
$sizeFiles=@($sizeExe,(Join-Path $sizeSdk 'nvngx_dlss.dll'),(Join-Path $sizeNr 'nvngx_dlssnr.dll'),(Join-Path $sizeNr 'nrchain_nvngx.dll'))
@($sizeFiles | ForEach-Object { @{path=$_;sha256=(Get-FileHash -LiteralPath $_).Hash} }) | ConvertTo-Json | Set-Content (Join-Path $sizeEvidence 'pre-run-lock.json')
foreach ($sizeCase in @('320x184','320x176','256x144','160x88')) {
    $sizeOut=Join-Path $sizeEvidence $sizeCase
    $sizeArgs=@('--fixed-size',$sizeCase,$sizeSdk,$sizeNr,$sizeOut,'SyntheticAreaReset1') | ForEach-Object { '"'+$_+'"' }
    $sizeProcess=Start-Process -FilePath $sizeExe -ArgumentList $sizeArgs -WorkingDirectory $sizeEvidence -WindowStyle Hidden -RedirectStandardOutput (Join-Path $sizeEvidence "$sizeCase.stdout.txt") -RedirectStandardError (Join-Path $sizeEvidence "$sizeCase.stderr.txt") -PassThru
    $sizeTimeout= -not $sizeProcess.WaitForExit(45000)
    if ($sizeTimeout) { Stop-Process -Id $sizeProcess.Id -Force; $sizeProcess.WaitForExit() }
    $sizeProcess.Refresh()
    @{case=$sizeCase;pid=$sizeProcess.Id;exit_code=$sizeProcess.ExitCode;timeout=$sizeTimeout;exited=$sizeProcess.HasExited} | ConvertTo-Json | Set-Content (Join-Path $sizeEvidence "$sizeCase.process.json")
    if ($sizeTimeout -or $sizeProcess.ExitCode -ne 0) { throw "Stopped entire batch at $sizeCase; no fallback or retry." }
}
