param([Parameter(Mandatory=$true)][string]$Validator)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Execute the actual receipt/throw path without replacing a Windows GPU run.
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    (Resolve-Path -LiteralPath $Validator).Path, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Production validator failed PowerShell parsing.' }
$functions = @($ast.FindAll({param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Complete-PostScaledWorkRun'
}, $true))
if ($functions.Count -ne 1) { throw 'Expected one production receipt function.' }
$calls = @($ast.FindAll({param($node)
    $node -is [Management.Automation.Language.CommandAst] -and
    $node.GetCommandName() -eq 'Complete-PostScaledWorkRun'
}, $true))
if ($calls.Count -ne 1) { throw 'Production runner must call its receipt function.' }
Invoke-Expression $functions[0].Extent.Text

$script:checks = 0
function Assert-Receipt([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "FAIL receipt: $Message" }
    ++$script:checks
}
$sandbox = Join-Path ([IO.Path]::GetTempPath()) ('warp-receipt-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $sandbox | Out-Null
try {
    $arguments = @{
        Build=$sandbox; Source=(Join-Path $sandbox 'core.cpp')
        Harness=(Join-Path $sandbox 'harness.cpp'); Template=(Join-Path $sandbox 'template.cpp.in')
        Executable=(Join-Path $sandbox 'fixture.exe'); Stdout=(Join-Path $sandbox 'stdout.txt')
        Stderr=(Join-Path $sandbox 'stderr.txt'); DurationMs=12.5
        WarpRuntimeHelper=(Join-Path $sandbox 'ci_warp_runtime.h')
    }
    foreach ($key in @('Source', 'Harness', 'Template', 'Executable', 'WarpRuntimeHelper')) {
        [IO.File]::WriteAllText($arguments[$key], "fixture $key")
    }
    $summary = 'PASS post-scaled-work WARP: 60 scenarios (production WriteCodecDescriptorSet, FP16 model planes, identity + nonzero neural perturbation, area overlap integral)'
    $finished = "$summary`nCOMPLETE post-scaled-work WARP`n"
    $launchFailure = $null
    try { throw 'original process launch error' } catch { $launchFailure = $_ }
    $cases = @(
        @{ Name='success'; Exit=0; Hex='0x00000000'; Text=$finished; Ended=$true; Error=$null },
        @{ Name='success-crlf'; Exit=0; Hex='0x00000000'; Text=$finished.Replace("`n", "`r`n"); Ended=$true; Error=$null },
        @{ Name='native2173'; Exit=2173; Hex='0x0000087D'; Text=$finished; Ended=$true; Error='Post-scaled-work WARP failed: 2173' },
        @{ Name='null-exit'; Exit=$null; Hex=$null; Text=$finished; Ended=$true; Error='Post-scaled-work WARP exit code was unavailable.' },
        @{ Name='incomplete'; Exit=0; Hex='0x00000000'; Text="$summary`nPASS pe"; Ended=$true; Error='Post-scaled-work WARP output is incomplete:' },
        @{ Name='missing-summary'; Exit=0; Hex='0x00000000'; Text="COMPLETE post-scaled-work WARP`n"; Ended=$true; Error='Post-scaled-work WARP output is incomplete:' },
        @{ Name='short-suite'; Exit=0; Hex='0x00000000'; Text=$finished.Replace('60 scenarios', '59 scenarios'); Ended=$true; Error='Post-scaled-work WARP output is incomplete:' },
        @{ Name='truncated-final-line'; Exit=0; Hex='0x00000000'; Text=$finished.TrimEnd("`n"); Ended=$true; Error='Post-scaled-work WARP output is incomplete:' },
        @{ Name='not-exited'; Exit=0; Hex='0x00000000'; Text=$finished; Ended=$false; Error='Post-scaled-work WARP process termination was not observed.' },
        @{ Name='signed-seh'; Exit=-1073741819; Hex='0xC0000005'; Text=$finished; Ended=$true; Error='Post-scaled-work WARP failed: -1073741819' },
        @{ Name='launch-error'; Exit=$null; Hex=$null; Text=''; Ended=$false; Error='original process launch error'; Failure=$launchFailure }
    )
    foreach ($case in $cases) {
        [IO.File]::WriteAllText($arguments.Stdout, $case.Text)
        [IO.File]::WriteAllText($arguments.Stderr, "stderr retained: $($case.Name)")
        $receiptPath = Join-Path $sandbox 'post-scaled-work-result.json'
        if (Test-Path -LiteralPath $receiptPath) { Remove-Item -LiteralPath $receiptPath }
        $caught = $null
        $failure = if ($case.ContainsKey('Failure')) { $case.Failure } else { $null }
        try {
            Complete-PostScaledWorkRun @arguments -ExitCode $case.Exit -HasExited $case.Ended -ProcessFailure $failure
        } catch { $caught = $_ }
        Assert-Receipt (Test-Path -LiteralPath $receiptPath) "$($case.Name): receipt missing before throw"
        $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
        $expectedPass = $null -eq $case.Error
        Assert-Receipt ($receipt.warp_verified -eq $expectedPass) "$($case.Name): incorrect warp_verified"
        if ($expectedPass) {
            Assert-Receipt ($null -eq $caught) "$($case.Name): unexpected throw $caught"
        } else {
            Assert-Receipt ($null -ne $caught) "$($case.Name): failure did not throw"
            Assert-Receipt ($caught.Exception.Message.StartsWith($case.Error)) "$($case.Name): original failure changed: $caught"
        }
        Assert-Receipt ($receipt.exit_code -ceq $case.Exit) "$($case.Name): decimal exit changed"
        Assert-Receipt ($receipt.exit_code_hex -ceq $case.Hex) "$($case.Name): unsigned hex changed"
        Assert-Receipt ($receipt.has_exited -eq $case.Ended) "$($case.Name): HasExited changed"
        Assert-Receipt ($receipt.duration_ms -eq 12.5) "$($case.Name): duration changed"
        Assert-Receipt (-not $receipt.nvidia_feature18_verified -and -not $receipt.game_runtime_verified) "$($case.Name): GPU scope broadened"
        foreach ($pair in @(
            @('staged_core_sha256','Source'), @('generated_harness_sha256','Harness'),
            @('template_sha256','Template'), @('executable_sha256','Executable'),
            @('stdout_sha256','Stdout'), @('stderr_sha256','Stderr'),
            @('ci_warp_runtime_helper_sha256','WarpRuntimeHelper'))) {
            $expectedHash = (Get-FileHash -LiteralPath $arguments[$pair[1]] -Algorithm SHA256).Hash.ToLowerInvariant()
            Assert-Receipt ($receipt.($pair[0]) -ceq $expectedHash) "$($case.Name): $($pair[0]) changed"
        }
        Assert-Receipt ([IO.File]::ReadAllText($arguments.Stdout) -ceq $case.Text) "$($case.Name): stdout was rewritten"
        if ($null -ne $failure) {
            Assert-Receipt ($receipt.process_error -ceq $failure.Exception.Message) 'launch-error: receipt lost original error'
            Assert-Receipt ([object]::ReferenceEquals($caught.Exception, $failure.Exception)) 'launch-error: original exception was replaced'
        }
    }
    # A successful process cannot qualify when its compiled helper input is absent.
    Remove-Item -LiteralPath $arguments.WarpRuntimeHelper
    [IO.File]::WriteAllText($arguments.Stdout, $finished)
    $caught = $null
    try { Complete-PostScaledWorkRun @arguments -ExitCode 0 -HasExited $true }
    catch { $caught = $_ }
    $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
    Assert-Receipt ($null -ne $caught) 'missing runtime helper must throw'
    Assert-Receipt ($caught.Exception.Message.StartsWith('Post-scaled-work WARP evidence failed:')) 'missing helper lost evidence failure'
    Assert-Receipt (-not $receipt.warp_verified) 'missing helper incorrectly qualified'
    Assert-Receipt ($receipt.evidence_errors -contains 'Missing evidence file: ci_warp_runtime_helper_sha256') 'missing helper not recorded'
    Write-Host "PASS production post-scaled-work receipt: $script:checks checks across $($cases.Count) process outcomes; CPU/PowerShell only"
} finally {
    Remove-Item -LiteralPath $sandbox -Recurse -Force
}
