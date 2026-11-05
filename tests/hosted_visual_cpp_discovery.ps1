param([Parameter(Mandatory=$true)][string]$Bootstrap)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Import the real discovery function without downloading any build inputs.
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    (Resolve-Path -LiteralPath $Bootstrap).Path, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Production bootstrap failed PowerShell parsing.' }
$functions = @($ast.FindAll({param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Find-HostedVisualCpp'
}, $true))
if ($functions.Count -ne 1) { throw 'Expected one production VS discovery function.' }
$productionFunction = $functions[0].Extent.Text
Invoke-Expression $productionFunction

$script:checks = 0
function Assert-Discovery([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "FAIL discovery: $Message" }
    ++$script:checks
}
$sandbox = Join-Path ([IO.Path]::GetTempPath()) ('vs-discovery-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $sandbox | Out-Null
$environmentNames = @('DLSS5_VS_FIXTURE_PYTHON', 'DLSS5_VS_FIXTURE_SCRIPT', 'DLSS5_VS_FIXTURE_CASE', 'DLSS5_VS_FIXTURE_ROOT', 'DLSS5_VS_FIXTURE_INSTALL')
$savedEnvironment = @{}
foreach ($name in $environmentNames) { $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
$expectedQuery = @('-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath')
try {
    $python = (Get-Command python -CommandType Application -ErrorAction Stop).Source
    $nativeScript = Join-Path $sandbox 'vswhere_standin.py'
    # A real child process provides stdout/stderr, delayed termination, and an
    # actual native exit status. It does not simulate an installed VS inventory.
    $source = @'
import os
from pathlib import Path
import sys
import time

root = Path(os.environ['DLSS5_VS_FIXTURE_ROOT'])
case = os.environ['DLSS5_VS_FIXTURE_CASE']
install = os.environ['DLSS5_VS_FIXTURE_INSTALL']
(root / 'query-args.txt').write_bytes('\n'.join(sys.argv[1:]).encode('utf-8'))
if case == 'blank-and-path':
    print('')
    print('  ' + install + '  ', flush=True)
    print(' \t')
elif case == 'missing-vcvars':
    print(str(root / 'missing installation'), flush=True)
elif case != 'empty':
    print(install, flush=True)
if case == 'multiple':
    print(install + '-other', flush=True)
if case == 'stderr-and-path':
    print('fixture diagnostic on stderr', file=sys.stderr, flush=True)
if case in ('delayed', 'nonzero-with-path'):
    time.sleep(0.6)
(root / 'native-completed.txt').write_text('finished before exit', encoding='utf-8')
sys.exit(19 if case == 'nonzero-with-path' else 0)
'@
    [IO.File]::WriteAllText($nativeScript, $source, (New-Object Text.UTF8Encoding($false)))
    $env:DLSS5_VS_FIXTURE_PYTHON = $python
    $env:DLSS5_VS_FIXTURE_SCRIPT = $nativeScript
    if ($env:OS -eq 'Windows_NT') {
        $selector = Join-Path $sandbox 'vswhere-standin.cmd'
        [IO.File]::WriteAllText($selector, "@echo off`r`n`"%DLSS5_VS_FIXTURE_PYTHON%`" `"%DLSS5_VS_FIXTURE_SCRIPT%`" %*`r`nexit /b %errorlevel%`r`n")
    } else {
        $selector = Join-Path $sandbox 'vswhere-standin'
        [IO.File]::WriteAllText($selector, "#!/bin/sh`n" + 'exec "$DLSS5_VS_FIXTURE_PYTHON" "$DLSS5_VS_FIXTURE_SCRIPT" "$@"' + "`n")
        & chmod +x $selector
        if ($LASTEXITCODE) { throw 'Could not make the native stand-in executable.' }
    }
    $install = Join-Path $sandbox 'Visual Studio 2022 Fixture'
    $vcvars = Join-Path $install 'VC/Auxiliary/Build/vcvarsall.bat'
    New-Item -ItemType Directory -Path (Split-Path -Parent $vcvars) -Force | Out-Null
    [IO.File]::WriteAllText($vcvars, 'fixture: presence only; not executed')
    $env:DLSS5_VS_FIXTURE_INSTALL = $install
    # Unix PowerShell expands native wildcards; an empty working directory keeps
    # the Windows query's literal '*' intact in this cross-platform stand-in.
    $emptyWork = Join-Path $sandbox 'empty-working-directory'
    New-Item -ItemType Directory -Path $emptyWork | Out-Null

    function Test-DiscoveryCase([string]$Name, [string]$Failure, $ExpectedExit, [int]$Count, [string]$Suffix = '') {
        $caseRoot = Join-Path $sandbox ($Name + $Suffix)
        New-Item -ItemType Directory -Path $caseRoot | Out-Null
        $env:DLSS5_VS_FIXTURE_CASE = $Name
        $env:DLSS5_VS_FIXTURE_ROOT = $caseRoot
        $receiptPath = Join-Path $caseRoot 'visual-studio-discovery.json'
        $completionMarker = Join-Path $caseRoot 'native-completed.txt'
        $command = if ($Name -eq 'missing-selector') { Join-Path $caseRoot 'absent.exe' } else { $selector }
        $global:LASTEXITCODE = 83
        $caught = $null
        $selected = $null
        Push-Location -LiteralPath $emptyWork
        try { $selected = Find-HostedVisualCpp -VsWhere $command -EvidencePath $receiptPath }
        catch { $caught = $_ }
        finally {
            # Snapshot before JSON parsing/assertions; an orphaned .cmd child may
            # finish later and must not rewrite what we observed at return.
            $script:completedAtReturn = [IO.File]::Exists($completionMarker)
            Pop-Location
        }
        Assert-Discovery (Test-Path -LiteralPath $receiptPath -PathType Leaf) "$Name receipt missing before failure"
        $receipt = [IO.File]::ReadAllText($receiptPath) | ConvertFrom-Json
        $expectedPass = [string]::IsNullOrEmpty($Failure)
        Assert-Discovery ($receipt.schema -ceq 'hosted-visual-cpp-discovery-v1') "$Name schema"
        Assert-Discovery ($receipt.verified -eq $expectedPass) "$Name verified"
        Assert-Discovery ($receipt.exit_code -ceq $ExpectedExit) "$Name native exit or stale status"
        Assert-Discovery ($receipt.failure -ceq $(if ($expectedPass) { $null } else { $Failure })) "$Name failure classification"
        Assert-Discovery ($receipt.result_count -eq $Count) "$Name path count"
        Assert-Discovery ($receipt.selector -ceq $command) "$Name executable identity"
        Assert-Discovery (($receipt.arguments -join "`n") -ceq ($expectedQuery -join "`n")) "$Name recorded query"
        Assert-Discovery ($ErrorActionPreference -ceq 'Stop') "$Name error preference restored"
        if ($expectedPass) {
            Assert-Discovery ($null -eq $caught) "$Name unexpected error $caught"
            Assert-Discovery ($selected.VisualStudio -ceq $install) "$Name selected installation"
            Assert-Discovery ($selected.VcVars -ceq $vcvars) "$Name selected environment script"
        } else {
            Assert-Discovery ($null -ne $caught) "$Name did not throw"
            Assert-Discovery ($caught.Exception.Message.Contains($receiptPath)) "$Name error did not identify evidence"
        }
        if ($Name -ne 'missing-selector') {
            Assert-Discovery $script:completedAtReturn "$Name child was stopped before completion"
            $actualQuery = [IO.File]::ReadAllText((Join-Path $caseRoot 'query-args.txt'))
            Assert-Discovery ($actualQuery -ceq ($expectedQuery -join "`n")) "$Name actual component query changed: $actualQuery"
        }
        if ($Name -eq 'stderr-and-path') {
            Assert-Discovery ($receipt.stdout.Count -eq 1) "$Name stderr was treated as an installation"
            Assert-Discovery (($receipt.stderr -join "`n").Contains('fixture diagnostic on stderr')) "$Name stderr missing"
        }
        if ($Name -eq 'blank-and-path') {
            Assert-Discovery ($receipt.stdout.Count -eq 3) "$Name raw output was discarded"
            Assert-Discovery ($receipt.stdout[1] -ceq ('  ' + $install + '  ')) "$Name raw whitespace changed"
        }
    }

    Test-DiscoveryCase 'success' '' 0 1
    Test-DiscoveryCase 'delayed' '' 0 1
    Test-DiscoveryCase 'blank-and-path' '' 0 1
    Test-DiscoveryCase 'stderr-and-path' '' 0 1
    Test-DiscoveryCase 'nonzero-with-path' 'native-failure' 19 1
    Test-DiscoveryCase 'empty' 'no-matching-installation' 0 0
    Test-DiscoveryCase 'multiple' 'ambiguous-installations' 0 2
    Test-DiscoveryCase 'missing-vcvars' 'vcvars-missing' 0 1
    Test-DiscoveryCase 'missing-selector' 'selector-missing' $null 0

    # Exact old pipeline: accepting its first line is insufficient evidence that
    # the process finished. Exercise the same positive contract against it.
    $capture = '$RawOutput = @(& $VsWhere @Query 2>&1)'
    $earlyPipeline = $productionFunction.Replace($capture, '$RawOutput = @(& $VsWhere @Query 2>&1 | Select-Object -First 1)')
    Assert-Discovery ($earlyPipeline -cne $productionFunction) 'early-pipeline mutation applied'
    Invoke-Expression $earlyPipeline
    $negative = $null
    try { Test-DiscoveryCase 'delayed' '' 0 1 '-early-pipeline' }
    catch { $negative = $_ }
    $negativeControls = 1
    if ($null -ne $negative) {
        Assert-Discovery ($negative.Exception.Message.StartsWith('FAIL discovery: delayed')) 'early-pipeline failed outside the actual discovery contract'
        ++$negativeControls
        Write-Host "Early-pipeline contract rejected; child_complete_at_return=$script:completedAtReturn"
    } else {
        # Some native/PowerShell combinations may finish before Select-First
        # stops upstream. A fully completed valid query is not a product failure.
        Assert-Discovery $script:completedAtReturn 'early pipeline neither completed nor failed its contract'
        Write-Host 'Early-pipeline characterization completed on this host; no early-stop claim.'
    }
    # A Windows .cmd launcher can leave its child alive briefly after cancellation.
    # Let that finite stand-in finish before deleting the isolated test directory.
    Start-Sleep -Milliseconds 800

    $exitCheck = 'elseif ($Discovery.exit_code -ne 0)'
    $ignoreExit = $productionFunction.Replace($exitCheck, 'elseif ($false)')
    Assert-Discovery ($ignoreExit -cne $productionFunction) 'ignored-exit mutation applied'
    Invoke-Expression $ignoreExit
    $negative = $null
    try { Test-DiscoveryCase 'nonzero-with-path' 'native-failure' 19 1 '-ignored-exit' }
    catch { $negative = $_ }
    Assert-Discovery ($null -ne $negative) 'nonzero native status was not rejected by the actual contract'
    Invoke-Expression $productionFunction
    Write-Host "PASS production Visual C++ discovery: $script:checks checks, 9 discovery outcomes and $negativeControls exact negative controls; no VS inventory claim"
} finally {
    foreach ($name in $environmentNames) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process') }
    Remove-Item -LiteralPath $sandbox -Recurse -Force
}
# Intentional native failures belong to the assertions above. Report this
# script's success to callers that propagate LASTEXITCODE (GitHub PowerShell).
exit 0
