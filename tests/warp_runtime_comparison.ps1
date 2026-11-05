param([Parameter(Mandatory=$true)][string]$Comparison, [Parameter(Mandatory=$true)][string]$Validator)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$tokens=$null; $errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Resolve-Path $Comparison).Path,[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw 'Comparison script failed parsing.' }
$names=@('Get-ComparisonHash','Assert-ComparisonHash','Import-ComparisonReceiptFunction','Read-BaselineWarpModules','Complete-WarpRuntimeComparison')
foreach ($name in $names) {
    $found=@($ast.FindAll({param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
    },$true))
    if ($found.Count -ne 1) { throw "Expected one actual function: $name" }
    Invoke-Expression $found[0].Extent.Text
}
. (Import-ComparisonReceiptFunction (Resolve-Path $Validator).Path)
$script:checks=0
function Check([bool]$Value,[string]$Message) {
    if (-not $Value) { throw "FAIL comparison: $Message" }
    ++$script:checks
}
$sandbox=Join-Path ([IO.Path]::GetTempPath()) ('warp-comparison-fixture-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $sandbox | Out-Null
try {
    $run=@{Build=$sandbox; Source=(Join-Path $sandbox 'core.cpp'); Harness=(Join-Path $sandbox 'host.cpp')
        Template=(Join-Path $sandbox 'template.in'); Executable=(Join-Path $sandbox 'fixture.exe')
        Stdout=(Join-Path $sandbox 'stdout.txt'); Stderr=(Join-Path $sandbox 'stderr.txt')
        ExitCode=0; HasExited=$true; DurationMs=12.5}
    foreach ($key in @('Source','Harness','Template','Executable','Stderr')) { [IO.File]::WriteAllText($run[$key],$key) }
    # Read real PE version metadata from a copied PowerShell assembly, without
    # loading it as WARP. Runtime execution/module observations remain synthetic.
    $moduleFile=Join-Path $sandbox 'baseline-module.dll'
    # Windows PowerShell 5 loads this assembly from the GAC, whereas pwsh keeps
    # it beside its executable. Follow the assembly that is actually loaded.
    Copy-Item -LiteralPath ([Management.Automation.PSObject].Assembly.Location) -Destination $moduleFile
    $moduleLog=Join-Path $sandbox 'baseline-modules.txt'
    [IO.File]::WriteAllText($moduleLog,"LOADED_MODULE name=d3d10warp.dll path=$moduleFile`n")
    $logHash=Get-ComparisonHash $moduleLog
    $observed=Read-BaselineWarpModules $moduleLog $logHash
    Check ($observed.modules.Count -eq 1) 'baseline actual file observation missing'
    Check ($observed.modules[0].sha256 -ceq (Get-ComparisonHash $moduleFile)) 'baseline file hash differs'
    Check ($observed.files_verified -and -not [string]::IsNullOrWhiteSpace($observed.modules[0].file_version)) 'baseline actual PE version unavailable'
    $rejected=$false
    try { Read-BaselineWarpModules $moduleLog ('0'*64) | Out-Null }
    catch { $rejected=$_.Exception.Message -eq 'WARP comparison hash mismatch: baseline stderr' }
    Check $rejected 'unverified baseline stderr was accepted'
    Remove-Item -LiteralPath $moduleFile
    $unreadable=Read-BaselineWarpModules $moduleLog $logHash
    Check (-not $unreadable.files_verified -and $unreadable.errors.Count -gt 0) 'unreadable baseline module lost its error'
    $baseline=Join-Path $sandbox 'baseline.json'
    $warp=Join-Path $sandbox 'd3d10warp.dll'
    [IO.File]::WriteAllText($warp,'CPU fixture bytes, never a Windows DLL')
    $warpHash=Get-ComparisonHash $warp
    Assert-ComparisonHash $warp $warpHash 'fixture bytes'
    $hashFailure=$false
    try { Assert-ComparisonHash $warp ('0'*64) 'fixture bytes' }
    catch { $hashFailure=$_.Exception.Message -eq 'WARP comparison hash mismatch: fixture bytes' }
    Check $hashFailure 'actual byte hash mismatch must throw'
    $complete="PASS post-scaled-work WARP: 60 scenarios (production WriteCodecDescriptorSet, FP16 model planes, identity + nonzero neural perturbation, area overlap integral)`nCOMPLETE post-scaled-work WARP`n"
    $cases=@('success','wrong-module-hash','wrong-module-path','missing-module','missing-version','changed-baseline','native2173','incomplete','setup-failure','timeout-zero-exit')
    foreach ($case in $cases) {
        [IO.File]::WriteAllText($baseline,'{"warp_verified":false,"exit_code":2173}')
        $before=Get-ComparisonHash $baseline
        [IO.File]::WriteAllText($run.Stdout,$complete)
        $run.ExitCode=0
        $module=[pscustomobject]@{name='d3d10warp.dll';path=$warp;sha256=$warpHash;file_version='fixture-only';product_version='fixture-only'}
        $modules=@($module); $failure=$null
        switch ($case) {
            'wrong-module-hash' { $module.sha256='0'*64 }
            'wrong-module-path' { $module.path=Join-Path $sandbox 'system/d3d10warp.dll' }
            'missing-module' { $modules=@() }
            'missing-version' { $module.file_version='' }
            'changed-baseline' { [IO.File]::AppendAllText($baseline,' ') }
            'native2173' { $run.ExitCode=2173 }
            'incomplete' { [IO.File]::WriteAllText($run.Stdout,'PASS pe') }
            'setup-failure' { try { throw 'original download failure' } catch { $failure=$_ } }
            'timeout-zero-exit' { try { throw [TimeoutException]::new('diagnostic deadline reached') } catch { $failure=$_ } }
        }
        $unchangedAtCall=Get-ComparisonHash $baseline
        $caught=$null
        try {
            Complete-WarpRuntimeComparison -Run $run -Baseline $baseline -BaselineHash $before `
                -ExpectedWarpPath $warp -ExpectedWarpHash $warpHash -Modules $modules -Failure $failure -Metadata @{test_scope='CPU/PowerShell; synthetic module observations'}
        } catch { $caught=$_ }
        $receipt=Get-Content -LiteralPath (Join-Path $sandbox 'warp-runtime-comparison.json') -Raw | ConvertFrom-Json
        $native=Get-Content -LiteralPath (Join-Path $sandbox 'post-scaled-work-result.json') -Raw | ConvertFrom-Json
        $pass=$case -eq 'success'
        Check ($receipt.diagnostic_passed -eq $pass) "$case diagnostic acceptance"
        Check ($native.warp_verified -eq $pass) "$case production receipt acceptance"
        Check (($null -eq $caught) -eq $pass) "$case throw preservation"
        Check ($receipt.original_gate_failed -and -not $receipt.overrides_baseline) "$case baseline failure retained"
        Check ((Get-ComparisonHash $baseline) -ceq $unchangedAtCall) "$case baseline file was rewritten"
        Check ($receipt.baseline_receipt_sha256_before -ceq $before) "$case original receipt hash lost"
        Check ($receipt.baseline_preserved -eq ($case -ne 'changed-baseline')) "$case baseline mutation detection"
        if ($case -eq 'native2173') {
            Check ($native.exit_code -eq 2173 -and $native.exit_code_hex -ceq '0x0000087D') 'native2173 raw exit identity'
            Check ($caught.Exception.Message -eq 'Post-scaled-work WARP failed: 2173') 'native2173 original failure'
        }
        if ($case -in @('setup-failure','timeout-zero-exit')) { Check ([object]::ReferenceEquals($caught.Exception,$failure.Exception)) 'setup original exception' }
    }
    Write-Host "PASS actual WARP comparison receipt/hash functions: $script:checks checks, $($cases.Count) outcomes; CPU/PowerShell only, no Windows module enumeration or WARP execution"
} finally { Remove-Item -LiteralPath $sandbox -Recurse -Force }
