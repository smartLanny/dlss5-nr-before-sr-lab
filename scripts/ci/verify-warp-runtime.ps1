param(
    [Parameter(Mandatory=$true)][string]$Receipt,
    [Parameter(Mandatory=$true)][string]$EvidenceDirectory,
    [Parameter(Mandatory=$true)][ValidateSet('Begin','End')][string]$Phase,
    [string[]]$RequiredSites = @()
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Read-PinnedWarpReceipt([string]$Path) {
    if ((Get-Item -LiteralPath $Path).Length -gt 65536) { throw 'Oversized WARP preparation receipt.' }
    $raw = [IO.File]::ReadAllText([IO.Path]::GetFullPath($Path), (New-Object Text.UTF8Encoding($false, $true)))
    if (-not $raw.TrimStart().StartsWith('{')) { throw 'WARP preparation receipt must be one JSON object.' }
    $data = $raw | ConvertFrom-Json
    # ConvertFrom-Json can silently keep the last duplicate property. Tokenize
    # strings before punctuation so text inside a JSON value cannot look like
    # another identity key. JSON syntax itself is still checked by the parser.
    $tokens = @([regex]::Matches($raw, '"(?:[^"\\]|\\.)*"|[{}\[\]:]'))
    $depth = 0; $names = @{}
    for ($i = 0; $i -lt $tokens.Count; $i++) {
        $token = $tokens[$i].Value
        if ($token -eq '{' -or $token -eq '[') { $depth++; continue }
        if ($token -eq '}' -or $token -eq ']') { $depth--; continue }
        if ($depth -eq 1 -and $token.StartsWith('"') -and $i + 1 -lt $tokens.Count -and
            $tokens[$i + 1].Value -eq ':') {
            $key = $token.Substring(1, $token.Length - 2)
            if ($key.Contains('\') -or $names.ContainsKey($key)) {
                throw 'Duplicate or escaped WARP preparation identity key.'
            }
            $names[$key] = $true
        }
    }
    $expected = 'bb8ee56fd0f7ee665abb61178301e612181e5e2f57af0e5ab64e456b8f1a8c4f'
    if ($data.schema -cne 'dlss5-ci-warp-runtime-v1' -or $data.version -cne '1.0.22' -or
        $data.package_sha256 -cne 'afc058f4d1066edc4231a38f3ab3592b1930f7287d4ac700e6a056e0874bb0be' -or
        $data.dll_sha256 -cne $expected -or $data.test_only -isnot [bool] -or
        $data.redistributed -isnot [bool] -or -not $data.test_only -or $data.redistributed) {
        throw 'Pinned WARP preparation identity mismatch.'
    }
    if (-not [IO.Path]::IsPathRooted($data.dll_path) -or
        ([IO.Path]::DirectorySeparatorChar -eq '\' -and $data.dll_path -notmatch '^(?:[A-Za-z]:[\\/]|\\\\)') -or
        [IO.Path]::GetFileName($data.dll_path) -ine 'd3d10warp.dll') {
        throw 'Pinned WARP DLL path is not an absolute DLL path.'
    }
    $dll = [IO.Path]::GetFullPath($data.dll_path)
    $temporary = [IO.Path]::GetFullPath($env:RUNNER_TEMP).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    if (-not $dll.StartsWith($temporary, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Pinned WARP DLL must remain in this runner temporary directory.'
    }
    if ((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant() -cne $expected -or
        (Get-Item -LiteralPath $dll).Length -ne $data.dll_bytes) {
        throw 'Pinned WARP DLL changed before or during the test step.'
    }
    $version = [Diagnostics.FileVersionInfo]::GetVersionInfo($dll)
    if ([string]::IsNullOrWhiteSpace($data.file_version) -or $version.FileVersion -cne $data.file_version) {
        throw 'Pinned WARP DLL version changed.'
    }
    $data
}

function Read-WarpHostEvidence([string]$Path, [string]$ExpectedDll, [string]$ExpectedHash) {
    if ((Get-Item -LiteralPath $Path).Length -gt 32768) { throw 'Oversized WARP host evidence.' }
    $rows = @{}
    $keys = @('schema','host_exe','pid','site','expected_sha256','actual_module','warp_instances','verified')
    $utf8 = New-Object Text.UTF8Encoding($false, $true)
    foreach ($line in [IO.File]::ReadAllLines($Path, $utf8)) {
        if ($line.Length -eq 0) { continue }
        $parts = $line -split '=', 2
        if ($parts.Count -ne 2 -or $parts[0] -cnotin $keys -or $rows.ContainsKey($parts[0]) -or
            $line.IndexOf([char]0) -ge 0) { throw 'Malformed or duplicate WARP host evidence field.' }
        $rows[$parts[0]] = $parts[1]
    }
    if ($rows.Count -ne $keys.Count -or $rows.schema -cne '1' -or $rows.verified -cne 'true' -or
        $rows.warp_instances -cne '1' -or $rows.expected_sha256 -cne $ExpectedHash -or
        $rows.pid -cnotmatch '^[1-9][0-9]*$' -or $rows.site -cnotmatch '^[A-Za-z0-9_-]{1,96}$' -or
        -not [IO.Path]::IsPathRooted($rows.host_exe) -or
        -not [IO.Path]::IsPathRooted($rows.actual_module) -or
        -not [string]::Equals([IO.Path]::GetFullPath($rows.actual_module),
            [IO.Path]::GetFullPath($ExpectedDll), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'WARP host evidence does not prove the configured runtime.'
    }
    if ([IO.Path]::DirectorySeparatorChar -eq '\') {
        foreach ($nativePath in @($rows.host_exe, $rows.actual_module)) {
            if ($nativePath -notmatch '^(?:[A-Za-z]:[\\/]|\\\\)') {
                throw 'WARP host evidence needs fully qualified Windows paths.'
            }
        }
    }
    if ([IO.Path]::GetFileName($Path) -cnotmatch ('^warp-' + $rows.pid + '-[1-9][0-9]*\.txt$')) {
        throw 'WARP host evidence filename does not match its process.'
    }
    [ordered]@{file=[IO.Path]::GetFileName($Path); sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
        host_exe=$rows.host_exe; pid=$rows.pid; site=$rows.site; actual_module=$rows.actual_module}
}

if ([string]::IsNullOrWhiteSpace($env:RUNNER_TEMP)) { throw 'RUNNER_TEMP is required.' }
$runtime = Read-PinnedWarpReceipt $Receipt
$directory = [IO.Path]::GetFullPath($EvidenceDirectory)
$dllDirectory = (Split-Path -Parent $runtime.dll_path).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
if (($directory.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar).StartsWith($dllDirectory, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Host evidence must be outside the vendor payload directory.'
}
if ($Phase -eq 'Begin') {
    if ((Test-Path -LiteralPath $directory) -and @(Get-ChildItem -LiteralPath $directory -Force).Count) {
        throw 'Each GPU step needs a new empty host evidence directory.'
    }
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    $env:DLSS5_TEST_WARP_DLL = [IO.Path]::GetFullPath($runtime.dll_path)
    $env:DLSS5_TEST_WARP_SHA256 = $runtime.dll_sha256
    $env:DLSS5_TEST_WARP_EVIDENCE_DIR = $directory
    Write-Host 'Pinned WARP context initialized; native hosts must prove their actual loaded module.'
} else {
    if ($env:DLSS5_TEST_WARP_DLL -cne [IO.Path]::GetFullPath($runtime.dll_path) -or
        $env:DLSS5_TEST_WARP_SHA256 -cne $runtime.dll_sha256 -or
        $env:DLSS5_TEST_WARP_EVIDENCE_DIR -cne $directory) {
        throw 'Pinned WARP process configuration changed within the step.'
    }
    $files = @(Get-ChildItem -LiteralPath $directory -Force)
    if (-not $files.Count -or $files.Count -gt 10000) { throw 'Missing or excessive WARP host evidence.' }
    $hosts = @()
    foreach ($file in $files) {
        if ($file.PSIsContainer -or $file.Extension -cne '.txt') { throw 'Unexpected file in WARP host evidence directory.' }
        $hosts += Read-WarpHostEvidence $file.FullName $runtime.dll_path $runtime.dll_sha256
    }
    foreach ($site in $RequiredSites) {
        if (-not @($hosts | Where-Object { $_.site -ceq $site }).Count) {
            throw "Missing actual WARP host family: $site"
        }
    }
    [IO.File]::WriteAllText((Join-Path $directory 'step-runtime.json'), ([ordered]@{
        schema='dlss5-ci-warp-step-v1'; runtime_version=$runtime.version; dll_path=$runtime.dll_path
        dll_sha256=$runtime.dll_sha256; file_version=$runtime.file_version
        dll_rehashed_after_tests=$true; actual_host_records=$hosts.Count; hosts=$hosts; required_sites=$RequiredSites
        software_only=$true; game_verified=$false; nvidia_inference_verified=$false
    } | ConvertTo-Json -Depth 6), (New-Object Text.UTF8Encoding($false)))
    Write-Host ('Verified pinned WARP runtime for ' + $hosts.Count + ' actual native device records.')
}
