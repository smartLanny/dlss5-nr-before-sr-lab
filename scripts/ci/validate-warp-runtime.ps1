param(
    [Parameter(Mandatory=$true)][string]$ToolchainRoot,
    [Parameter(Mandatory=$true)][string]$Receipt,
    [Parameter(Mandatory=$true)][string]$OutputRoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$build = [IO.Path]::GetFullPath($OutputRoot)
if ((Test-Path -LiteralPath $build) -and @(Get-ChildItem -LiteralPath $build -Force).Count) {
    throw 'Runtime contract output must be new or empty.'
}
New-Item -ItemType Directory -Force -Path $build | Out-Null
$exe = Join-Path $build 'ci-warp-runtime-probe.exe'
$compiler = Join-Path $ToolchainRoot 'bin/x86_64-w64-mingw32-clang++.exe'
& $compiler '-std=c++17' '-O0' '-Wall' '-Wextra' '-Werror' '-static' `
    (Join-Path $repo 'tests/ci_warp_runtime_probe.cpp') '-ld3d12' '-ldxgi' '-ldxguid' '-o' $exe
if ($LASTEXITCODE) { throw 'Actual native WARP runtime probe did not compile.' }
$runtime = Get-Content -LiteralPath $Receipt -Raw -Encoding UTF8 | ConvertFrom-Json
$verify = Join-Path $PSScriptRoot 'verify-warp-runtime.ps1'
$evidence = Join-Path $build 'positive-hosts'
$results = @()

function Run-RuntimeProbe([string]$Case, [string]$Program, [string]$Arguments,
                          [bool]$WantSuccess, [string]$ExpectedToken) {
    $stdout = Join-Path $build ($Case + '.stdout.txt')
    $stderr = Join-Path $build ($Case + '.stderr.txt')
    $parameters = @{FilePath=$Program; WorkingDirectory=$build; NoNewWindow=$true; PassThru=$true
        RedirectStandardOutput=$stdout; RedirectStandardError=$stderr}
    if ($Arguments) { $parameters.ArgumentList = $Arguments }
    $process = Start-Process @parameters
    try {
        $handle = $process.Handle
        if (-not $process.WaitForExit(45000)) {
            $process.Kill()
            [void]$process.WaitForExit(5000)
            throw "Native runtime contract process timed out: $Case"
        }
        $exit = $process.ExitCode
        if ($null -eq $exit -or (($exit -eq 0) -ne $WantSuccess)) {
            throw "Unexpected runtime contract exit: $Case / $exit"
        }
        $text = (Get-Content -LiteralPath $stdout -Raw) + (Get-Content -LiteralPath $stderr -Raw)
        if (-not $text.Contains($ExpectedToken)) { throw "Missing exact runtime contract outcome: $Case" }
        Write-Host ("PASS native runtime contract: $Case (exit $exit)")
        [ordered]@{case=$Case; exit_code=$exit; success_expected=$WantSuccess; token=$ExpectedToken
            stdout_sha256=(Get-FileHash -LiteralPath $stdout -Algorithm SHA256).Hash.ToLowerInvariant()
            stderr_sha256=(Get-FileHash -LiteralPath $stderr -Algorithm SHA256).Hash.ToLowerInvariant()}
    } finally { $process.Dispose() }
}

$saved = @{}
foreach ($name in @('DLSS5_TEST_WARP_DLL','DLSS5_TEST_WARP_SHA256','DLSS5_TEST_WARP_EVIDENCE_DIR')) {
    $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    & $verify -Receipt $Receipt -EvidenceDirectory $evidence -Phase Begin
    $results += Run-RuntimeProbe 'direct' $exe '' $true 'PASS CI_WARP_PROBE configured=1'
    $driver = Join-Path $build 'python-child.py'
    [IO.File]::WriteAllText($driver, "import subprocess,sys`nsubprocess.run([sys.argv[1]],check=True,timeout=30)`n", (New-Object Text.UTF8Encoding($false)))
    $python = (Get-Command python -ErrorAction Stop).Source
    $results += Run-RuntimeProbe 'python-child' $python ('"' + $driver + '" "' + $exe + '"') $true 'PASS CI_WARP_PROBE configured=1'
    & $verify -Receipt $Receipt -EvidenceDirectory $evidence -Phase End -RequiredSites 'runtime-probe'
    $positive = Get-Content -LiteralPath (Join-Path $evidence 'step-runtime.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $processIdentities = @($positive.hosts | ForEach-Object { $_.pid + ':' + $_.process_created } | Select-Object -Unique)
    if ($positive.actual_host_records -ne 2 -or $processIdentities.Count -ne 2) {
        throw 'Direct and Python-child probes did not both produce distinct actual device records.'
    }
    $results += Run-RuntimeProbe 'system-preloaded' $exe '--system-first' $false 'reason=unexpected-warp-module'
    $env:DLSS5_TEST_WARP_SHA256 = $null
    $results += Run-RuntimeProbe 'incomplete-config' $exe '' $false 'reason=incomplete-configuration'
    $env:DLSS5_TEST_WARP_SHA256 = $runtime.dll_sha256
    $env:DLSS5_TEST_WARP_DLL = Join-Path (Join-Path $build 'absent') 'd3d10warp.dll'
    $results += Run-RuntimeProbe 'missing-dll' $exe '' $false 'reason=warp-file-missing'
    $env:DLSS5_TEST_WARP_DLL = $runtime.dll_path
    $env:DLSS5_TEST_WARP_EVIDENCE_DIR = Join-Path $build 'absent-evidence'
    $results += Run-RuntimeProbe 'missing-evidence' $exe '' $false 'reason=evidence-directory-missing'
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $null, 'Process') }
    $results += Run-RuntimeProbe 'unconfigured-local' $exe '' $true 'PASS CI_WARP_PROBE configured=0'
    if (@(Get-ChildItem -LiteralPath $evidence -Filter 'warp-*.txt').Count -ne 2) {
        throw 'A negative or unconfigured probe produced an accepted host record.'
    }
    [IO.File]::WriteAllText((Join-Path $build 'validation.json'), ([ordered]@{
        schema='dlss5-ci-warp-loader-contract-v1'; passed=$true; cases=$results
        header_sha256=(Get-FileHash -LiteralPath (Join-Path $repo 'tests/ci_warp_runtime.h') -Algorithm SHA256).Hash.ToLowerInvariant()
        source_sha256=(Get-FileHash -LiteralPath (Join-Path $repo 'tests/ci_warp_runtime_probe.cpp') -Algorithm SHA256).Hash.ToLowerInvariant()
        executable_sha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
        original_runtime_receipt_sha256=(Get-FileHash -LiteralPath $Receipt -Algorithm SHA256).Hash.ToLowerInvariant()
        actual_native_device_processes=2; game_verified=$false; shader_workload_verified=$false
    } | ConvertTo-Json -Depth 6), (New-Object Text.UTF8Encoding($false)))
} finally {
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process') }
}
