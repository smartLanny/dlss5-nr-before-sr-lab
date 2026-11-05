param(
    [Parameter(Mandatory=$true)][string]$Verifier,
    [Parameter(Mandatory=$true)][string]$RuntimeReceipt,
    [switch]$Portable
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$windows = [Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT
if ($Portable -and $windows) { throw 'Portable mode cannot skip native version checks on Windows.' }
if (-not $windows -and -not $Portable) { throw 'Use -Portable outside Windows; native PE version validation requires Windows.' }
if ([string]::IsNullOrWhiteSpace($env:RUNNER_TEMP)) { throw 'RUNNER_TEMP is required.' }

# Import actual production definitions, including their small receipt/path
# helpers. Do not execute the verifier entry point or mirror its decisions.
$tokens=$null; $errors=$null
$verifierPath=(Resolve-Path -LiteralPath $Verifier).Path
$ast=[Management.Automation.Language.Parser]::ParseFile($verifierPath,[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw 'WARP verifier failed PowerShell parsing.' }
$definitions=@($ast.EndBlock.Statements | Where-Object { $_ -is [Management.Automation.Language.FunctionDefinitionAst] })
foreach ($name in @('Read-PinnedWarpReceipt','Read-WarpHostEvidence')) {
    if (@($definitions | Where-Object { $_.Name -ceq $name }).Count -ne 1) { throw "Expected one production function: $name" }
}
foreach ($definition in $definitions) { Invoke-Expression $definition.Extent.Text }

$script:checks=0
function Check([bool]$Value,[string]$Message) {
    if (-not $Value) { throw "FAIL WARP evidence: $Message" }
    ++$script:checks
}
function Reject([scriptblock]$Action,[string]$Case,[string]$Expected='') {
    $failure=$null
    try { & $Action | Out-Null } catch { $failure=$_ }
    Check ($null -ne $failure) "$Case was accepted"
    if ($Expected) { Check ($failure.Exception.Message -match $Expected) "$Case failed for a different reason: $failure" }
    elseif ($Portable) { throw "Portable rejection needs an exact pre-version oracle: $Case" }
}
function Write-Utf8([string]$Path,[string]$Text) {
    [IO.File]::WriteAllText($Path,$Text,(New-Object Text.UTF8Encoding($false)))
}

$originalJson=Get-Content -LiteralPath $RuntimeReceipt -Raw -Encoding UTF8
$original=$originalJson | ConvertFrom-Json
$pinned='bb8ee56fd0f7ee665abb61178301e612181e5e2f57af0e5ab64e456b8f1a8c4f'
$sourceHash=(Get-FileHash -LiteralPath $original.dll_path -Algorithm SHA256).Hash.ToLowerInvariant()
$receiptHash=(Get-FileHash -LiteralPath $RuntimeReceipt -Algorithm SHA256).Hash.ToLowerInvariant()
Check ($sourceHash -ceq $pinned) 'fixture input must be the real fixed Microsoft WARP DLL'
$sandbox=Join-Path ([IO.Path]::GetFullPath($env:RUNNER_TEMP)) ('warp-evidence-fixture-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $sandbox | Out-Null
$saved=@{}
foreach ($name in @('DLSS5_TEST_WARP_DLL','DLSS5_TEST_WARP_SHA256','DLSS5_TEST_WARP_EVIDENCE_DIR')) {
    $saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')
}
try {
    $payload=Join-Path $sandbox 'payload=real'
    New-Item -ItemType Directory -Path $payload | Out-Null
    $dll=Join-Path $payload 'd3d10warp.dll'
    Copy-Item -LiteralPath $original.dll_path -Destination $dll
    $base=$originalJson | ConvertFrom-Json
    $base.dll_path=$dll
    $baselineJson=$base | ConvertTo-Json -Depth 4
    $receipt=Join-Path $sandbox 'receipt.json'
    Write-Utf8 $receipt $baselineJson
    if ($Portable) {
        # Do not replace FileVersionInfo or use a managed stand-in. The actual
        # Linux runtime cannot read this native PE's version resource.
        Reject { Read-PinnedWarpReceipt $receipt } 'native PE metadata unavailable on Linux' '^Pinned WARP DLL version changed\.$'
        Write-Host 'SKIP native PE version positive/version mutations and verifier entry-point lifecycle: require Windows; real DLL hash/path and parser cases still run.'
    } else {
        $accepted=Read-PinnedWarpReceipt $receipt
        Check ($accepted.dll_path -ceq $dll -and $accepted.dll_sha256 -ceq $pinned) 'real copied DLL identity'
        $actualVersion=[Diagnostics.FileVersionInfo]::GetVersionInfo($dll)
        Check ($accepted.file_version -ceq $actualVersion.FileVersion -and -not [string]::IsNullOrWhiteSpace($actualVersion.FileVersion)) 'real native PE version metadata'
        # Exercise the exact UTF8 writer/PS5 reader boundary with a real DLL.
        $unicodeDirectory=Join-Path $sandbox ('payload-'+[char]0x4e2d)
        New-Item -ItemType Directory -Path $unicodeDirectory | Out-Null
        $unicodeDll=Join-Path $unicodeDirectory 'd3d10warp.dll'
        Copy-Item -LiteralPath $dll -Destination $unicodeDll
        $unicode=$baselineJson | ConvertFrom-Json; $unicode.dll_path=$unicodeDll
        Write-Utf8 $receipt ($unicode | ConvertTo-Json -Depth 4)
        Check ((Read-PinnedWarpReceipt $receipt).dll_path -ceq $unicodeDll) 'UTF8 receipt preserves a non-ASCII absolute path'
    }
    $receiptCases=@(
        @{Name='schema'; Field='schema'; Value='wrong'; Error='preparation identity mismatch'},
        @{Name='package-version'; Field='version'; Value='1.0.21'; Error='preparation identity mismatch'},
        @{Name='package-hash'; Field='package_sha256'; Value=('0'*64); Error='preparation identity mismatch'},
        @{Name='dll-hash'; Field='dll_sha256'; Value=('0'*64); Error='preparation identity mismatch'},
        @{Name='not-test-only'; Field='test_only'; Value=$false; Error='preparation identity mismatch'},
        @{Name='string-boolean'; Field='test_only'; Value='true'; Error='preparation identity mismatch'},
        @{Name='redistribution'; Field='redistributed'; Value=$true; Error='preparation identity mismatch'},
        @{Name='relative-dll'; Field='dll_path'; Value='d3d10warp.dll'; Error='absolute DLL path'},
        @{Name='wrong-dll-name'; Field='dll_path'; Value=(Join-Path $payload 'other.dll'); Error='absolute DLL path'},
        @{Name='outside-runner-temp'; Field='dll_path'; Value=(Join-Path ([IO.Path]::GetPathRoot($dll)) 'd3d10warp.dll'); Error='runner temporary directory'},
        @{Name='wrong-size'; Field='dll_bytes'; Value=([long]$base.dll_bytes+1); Error='changed before or during'}
    )
    if ($windows) {
        $receiptCases+=@(
            @{Name='wrong-file-version'; Field='file_version'; Value='0.0.0.0'; Error='DLL version changed'},
            @{Name='missing-file-version'; Field='file_version'; Value=''; Error='DLL version changed'},
            @{Name='root-relative-dll'; Field='dll_path'; Value='\d3d10warp.dll'; Error='absolute DLL path'},
            @{Name='drive-relative-dll'; Field='dll_path'; Value='C:d3d10warp.dll'; Error='absolute DLL path'}
        )
    }
    foreach ($case in $receiptCases) {
        $data=$baselineJson | ConvertFrom-Json; $data.($case.Field)=$case.Value
        Write-Utf8 $receipt ($data | ConvertTo-Json -Depth 4)
        Reject { Read-PinnedWarpReceipt $receipt } $case.Name $case.Error
    }
    # Mutate only an isolated copy, then restore the real byte before continuing.
    Write-Utf8 $receipt $baselineJson
    $stream=[IO.File]::Open($dll,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    try {
        [void]$stream.Seek(-1,[IO.SeekOrigin]::End); $last=$stream.ReadByte()
        [void]$stream.Seek(-1,[IO.SeekOrigin]::End); $stream.WriteByte([byte]($last -bxor 1))
    } finally { $stream.Dispose() }
    Reject { Read-PinnedWarpReceipt $receipt } 'changed-real-dll-byte' 'changed before or during'
    Copy-Item -LiteralPath $original.dll_path -Destination $dll -Force
    Check ((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant() -ceq $pinned) 'real fixture DLL restored'

    $duplicates=@(
        ($baselineJson -replace '^\{','{"schema":"wrong",'),
        ($baselineJson -replace '^\{','{"dll_sha256":"wrong",')
    )
    foreach ($json in $duplicates) {
        Write-Utf8 $receipt $json
        Reject { Read-PinnedWarpReceipt $receipt } 'duplicate-receipt-key' '(?i)duplicate'
    }
    Write-Utf8 $receipt ('['+$baselineJson+']')
    Reject { Read-PinnedWarpReceipt $receipt } 'array-wrapped-receipt' '(?i)object'
    Write-Utf8 $receipt ($baselineJson+(' '*65536))
    Reject { Read-PinnedWarpReceipt $receipt } 'oversized-receipt' 'Oversized WARP preparation receipt'
    [IO.File]::WriteAllBytes($receipt,[byte[]]@(0xff))
    Reject { Read-PinnedWarpReceipt $receipt } 'invalid-receipt-UTF8' '(?i)translat|decod|fallback|invalid'
    Write-Utf8 $receipt ($baselineJson.Replace('"schema"','"\u0073chema"'))
    Reject { Read-PinnedWarpReceipt $receipt } 'escaped-receipt-key' '(?i)escaped'
    $missing=$baselineJson | ConvertFrom-Json; $missing.PSObject.Properties.Remove('schema')
    Write-Utf8 $receipt ($missing | ConvertTo-Json -Depth 4)
    Reject { Read-PinnedWarpReceipt $receipt } 'missing-receipt-identity-field' '(?i)schema'
    $quoted=$baselineJson | ConvertFrom-Json
    $quoted.image_os='a string containing "schema":"wrong", {"dll_sha256":"wrong"}'
    Write-Utf8 $receipt ($quoted | ConvertTo-Json -Depth 4)
    if ($Portable) {
        Reject { Read-PinnedWarpReceipt $receipt } 'quoted keys are not object keys; reach native metadata limit' '^Pinned WARP DLL version changed\.$'
    } else { Check ((Read-PinnedWarpReceipt $receipt).dll_sha256 -ceq $pinned) 'quoted key text inside a string is accepted' }
    Write-Utf8 $receipt $baselineJson

    # Host observations below are synthetic records with real file identities;
    # this validates parsing/acceptance, never WARP loading or device creation.
    $hostDirectory=Join-Path $sandbox 'host-records'
    New-Item -ItemType Directory -Path $hostDirectory | Out-Null
    $creation='134000000000000000'; $otherCreation='134000000000000001'
    $hostFile=Join-Path $hostDirectory ("warp-$PID-$creation-1.txt")
    $hostExe=(Get-Process -Id $PID).Path
    if ([string]::IsNullOrWhiteSpace($hostExe) -and -not $windows) { $hostExe=Join-Path $PSHOME 'pwsh' }
    Check (-not [string]::IsNullOrWhiteSpace($hostExe) -and (Test-Path -LiteralPath $hostExe)) 'synthetic record names a real host executable file'
    $record="schema=2`nhost_exe=$hostExe`npid=$PID`nprocess_created=$creation`nsite=fixture-parser`nexpected_sha256=$pinned`nactual_module=$dll`nwarp_instances=1`nverified=true`n"
    Write-Utf8 $hostFile $record
    $observed=Read-WarpHostEvidence $hostFile $dll $pinned
    Check ($observed.actual_module -ceq $dll -and $observed.site -ceq 'fixture-parser') 'valid host record preserves identity and equals-sign path'
    Check ($observed.sha256 -ceq (Get-FileHash -LiteralPath $hostFile -Algorithm SHA256).Hash.ToLowerInvariant()) 'host record is hashed from its actual bytes'
    Check ($observed.process_created -ceq $creation) 'process creation identity survives parsing'
    $reusedFile=Join-Path $hostDirectory ("warp-$PID-$otherCreation-1.txt")
    $reusedRecord=$record.Replace("process_created=$creation","process_created=$otherCreation")
    Write-Utf8 $reusedFile $reusedRecord
    $reused=Read-WarpHostEvidence $reusedFile $dll $pinned
    Check ($reused.pid -ceq $observed.pid -and $reused.process_created -ceq $otherCreation -and $reused.file -cne $observed.file) 'PID reuse retains distinct process records'
    $hostCases=@(
        @{Name='missing-record-field'; Text=$record.Replace("verified=true`n",''); Error='does not prove'},
        @{Name='old-host-schema'; Text=$record.Replace('schema=2','schema=1'); Error='does not prove'},
        @{Name='not-verified'; Text=$record.Replace('verified=true','verified=false'); Error='does not prove'},
        @{Name='no-warp-module'; Text=$record.Replace('warp_instances=1','warp_instances=0'); Error='does not prove'},
        @{Name='multiple-warp-modules'; Text=$record.Replace('warp_instances=1','warp_instances=2'); Error='does not prove'},
        @{Name='wrong-host-sha'; Text=$record.Replace($pinned,('0'*64)); Error='does not prove'},
        @{Name='invalid-pid'; Text=$record.Replace("pid=$PID",'pid=0'); Error='does not prove'},
        @{Name='pid-overflow'; Text=$record.Replace("pid=$PID",'pid=4294967296'); Error='does not prove'},
        @{Name='missing-creation'; Text=$record.Replace("process_created=$creation`n",''); Error='does not prove'},
        @{Name='zero-creation'; Text=$record.Replace("process_created=$creation",'process_created=0'); Error='does not prove'},
        @{Name='negative-creation'; Text=$record.Replace("process_created=$creation",'process_created=-1'); Error='does not prove'},
        @{Name='leading-zero-creation'; Text=$record.Replace("process_created=$creation",'process_created=0123'); Error='does not prove'},
        @{Name='creation-overflow'; Text=$record.Replace("process_created=$creation",'process_created=18446744073709551616'); Error='does not prove'},
        @{Name='creation-filename-mismatch'; Text=$reusedRecord; Error='filename does not match'},
        @{Name='invalid-site'; Text=$record.Replace('site=fixture-parser','site=bad/name'); Error='does not prove'},
        @{Name='relative-host'; Text=$record.Replace($hostExe,'relative.exe'); Error='does not prove'},
        @{Name='relative-module'; Text=$record.Replace($dll,'d3d10warp.dll'); Error='does not prove'},
        @{Name='wrong-module-path'; Text=$record.Replace($dll,(Join-Path $sandbox 'd3d10warp.dll')); Error='does not prove'},
        @{Name='duplicate-host-field'; Text=($record+"verified=true`n"); Error='Malformed or duplicate'},
        @{Name='unknown-host-field'; Text=($record+"extra=1`n"); Error='Malformed or duplicate'},
        @{Name='field-case'; Text=$record.Replace('schema=2','Schema=2'); Error='Malformed or duplicate'},
        @{Name='embedded-nul'; Text=$record.Replace('verified=true',("verified=true"+[char]0)); Error='Malformed or duplicate'},
        @{Name='malformed-line'; Text=($record+"broken`n"); Error='Malformed or duplicate'},
        @{Name='oversized-record'; Text=($record+('x'*32768)); Error='Oversized WARP host evidence'}
    )
    if ($windows) {
        $hostCases+=@(
            @{Name='drive-relative-host'; Text=$record.Replace($hostExe,'C:relative.exe'); Error='fully qualified Windows paths'},
            @{Name='root-relative-host'; Text=$record.Replace($hostExe,'\relative.exe'); Error='fully qualified Windows paths'},
            @{Name='drive-relative-module'; Text=$record.Replace($dll,'C:d3d10warp.dll'); Error='does not prove|fully qualified Windows paths'},
            @{Name='root-relative-module'; Text=$record.Replace($dll,'\d3d10warp.dll'); Error='does not prove|fully qualified Windows paths'}
        )
    }
    foreach ($case in $hostCases) {
        Write-Utf8 $hostFile $case.Text
        Reject { Read-WarpHostEvidence $hostFile $dll $pinned } $case.Name $case.Error
    }
    [IO.File]::WriteAllBytes($hostFile,[byte[]]@(0xff))
    Reject { Read-WarpHostEvidence $hostFile $dll $pinned } 'invalid-UTF8' '(?i)translat|decod|fallback|invalid'
    Write-Utf8 $hostFile $record
    foreach ($name in @("warp-0-$creation-1.txt","warp-$PID-$creation-0.txt","warp-$PID-1.txt",'other.txt')) {
        $wrong=Join-Path $hostDirectory $name; Write-Utf8 $wrong $record
        Reject { Read-WarpHostEvidence $wrong $dll $pinned } 'wrong-record-filename' 'filename does not match'
    }

    if ($windows) {
        $lifecycle=Join-Path $sandbox 'lifecycle'
        & $verifierPath -Receipt $receipt -EvidenceDirectory $lifecycle -Phase Begin
        Reject { & $verifierPath -Receipt $receipt -EvidenceDirectory $lifecycle -Phase End -RequiredSites 'fixture-parser' } 'no-host-records' 'Missing or excessive WARP host evidence'
        Write-Utf8 (Join-Path $lifecycle ("warp-$PID-$creation-1.txt")) $record
        Write-Utf8 (Join-Path $lifecycle ("warp-$PID-$otherCreation-1.txt")) $reusedRecord
        Reject { & $verifierPath -Receipt $receipt -EvidenceDirectory $lifecycle -Phase End -RequiredSites 'absent-site' } 'missing-required-site' 'Missing actual WARP host family'
        & $verifierPath -Receipt $receipt -EvidenceDirectory $lifecycle -Phase End -RequiredSites 'fixture-parser'
        $step=Get-Content -LiteralPath (Join-Path $lifecycle 'step-runtime.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        Check ($step.actual_host_records -eq 2 -and $step.dll_rehashed_after_tests -and $step.required_sites[0] -ceq 'fixture-parser') 'actual entry-point receipt retains both reused-PID records'
        Check (@($step.hosts.pid | Select-Object -Unique).Count -eq 1 -and @($step.hosts.process_created | Select-Object -Unique).Count -eq 2) 'step host JSON preserves distinct creation identities'
    }
} finally {
    foreach ($name in $saved.Keys) { [Environment]::SetEnvironmentVariable($name,$saved[$name],'Process') }
    Remove-Item -LiteralPath $sandbox -Recurse -Force
    Check ((Get-FileHash -LiteralPath $original.dll_path -Algorithm SHA256).Hash.ToLowerInvariant() -ceq $sourceHash) 'original DLL was not modified'
    Check ((Get-FileHash -LiteralPath $RuntimeReceipt -Algorithm SHA256).Hash.ToLowerInvariant() -ceq $receiptHash) 'original receipt was not modified'
}
Write-Host ("PASS actual WARP evidence functions: $script:checks checks; PowerShell " + $PSVersionTable.PSVersion.ToString() + '; parser/file checks only, no WARP device execution')
