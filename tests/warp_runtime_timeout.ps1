param([Parameter(Mandatory=$true)][string]$Comparison)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$tokens=$null; $errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Resolve-Path $Comparison).Path,[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw 'Comparison script failed parsing.' }
$found=@($ast.FindAll({param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Stop-WarpComparisonOnTimeout'
},$true))
if ($found.Count -ne 1) { throw 'Expected actual timeout function.' }
Invoke-Expression $found[0].Extent.Text
$engineName=if ($PSVersionTable.PSEdition -eq 'Desktop') { 'powershell.exe' } elseif ($env:OS -eq 'Windows_NT') { 'pwsh.exe' } else { 'pwsh' }
$engine=Join-Path $PSHOME $engineName
$owned=$null; $untouched=$null
try {
    # Real child processes of this PowerShell engine, never a simulated Process.
    $arguments=@('-NoLogo','-NoProfile','-Command','[Threading.Thread]::Sleep(15000)')
    $untouched=Start-Process -FilePath $engine -ArgumentList $arguments -PassThru
    $owned=Start-Process -FilePath $engine -ArgumentList $arguments -PassThru
    $ownedHandle=$owned.Handle; $otherHandle=$untouched.Handle
    $run=@{HasExited=$false;ExitCode=$null}; $metadata=@{}
    $before=Stop-WarpComparisonOnTimeout -Process $owned -ElapsedSeconds 0 -LimitSeconds 0.1 -Run $run -Metadata $metadata
    if ($before -ne $false -or $owned.HasExited -or $metadata.Count) { throw 'Early timeout mutated the running process.' }
    $watch=[Diagnostics.Stopwatch]::StartNew()
    do { Start-Sleep -Milliseconds 20 } while ($watch.Elapsed.TotalSeconds -lt 0.1)
    $caught=$null
    try { Stop-WarpComparisonOnTimeout -Process $owned -ElapsedSeconds $watch.Elapsed.TotalSeconds -LimitSeconds 0.1 -Run $run -Metadata $metadata }
    catch { $caught=$_ }
    if ($null -eq $caught -or $caught.Exception -isnot [TimeoutException]) { throw 'Actual deadline did not preserve TimeoutException.' }
    if (-not $owned.HasExited -or -not $run.HasExited -or $null -eq $run.ExitCode) { throw 'Terminated process state was not recorded.' }
    if (-not $metadata.diagnostic_timed_out -or -not $metadata.process_termination_observed) { throw 'Timeout evidence missing.' }
    if ($untouched.HasExited) { throw 'Timeout terminated another process.' }
    if ($watch.Elapsed.TotalSeconds -gt 7) { throw 'Termination did not remain bounded.' }
    $run2=@{HasExited=$false;ExitCode=$null};$metadata2=@{}
    $after=Stop-WarpComparisonOnTimeout -Process $owned -ElapsedSeconds 900 -LimitSeconds 900 -Run $run2 -Metadata $metadata2
    if ($after -ne $false -or $metadata2.Count) { throw 'Already exited process was reclassified as timeout.' }
    Write-Host "PASS actual diagnostic timeout: real owned child terminated, second child retained, original TimeoutException/state recorded; CPU/PowerShell only"
} finally {
    foreach ($child in @($owned,$untouched)) {
        if ($null -ne $child) {
            if (-not $child.HasExited) { $child.Kill();[void]$child.WaitForExit(5000) }
            $child.Dispose()
        }
    }
}
