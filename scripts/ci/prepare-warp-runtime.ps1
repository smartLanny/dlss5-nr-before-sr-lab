# CI test dependency only. Never copy this DLL into a Core package.
param([Parameter(Mandatory=$true)][string]$ReceiptPath)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
Set-StrictMode -Version Latest
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ($env:OS -ne 'Windows_NT') { throw 'Pinned WARP preparation requires Windows.' }
if ([string]::IsNullOrWhiteSpace($env:RUNNER_TEMP)) { throw 'RUNNER_TEMP is required.' }
$receipt = [IO.Path]::GetFullPath($ReceiptPath)
if (Test-Path -LiteralPath $receipt) { throw 'Refusing to replace a WARP preparation receipt.' }
$private = Join-Path ([IO.Path]::GetFullPath($env:RUNNER_TEMP)) ('dlss5-test-warp-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $private | Out-Null
$package = Join-Path $private 'warp.nupkg'
$dll = Join-Path $private 'd3d10warp.dll'
$packageHash = 'afc058f4d1066edc4231a38f3ab3592b1930f7287d4ac700e6a056e0874bb0be'
$dllHash = 'bb8ee56fd0f7ee665abb61178301e612181e5e2f57af0e5ab64e456b8f1a8c4f'
$url = 'https://api.nuget.org/v3-flatcontainer/microsoft.direct3d.warp/1.0.22/microsoft.direct3d.warp.1.0.22.nupkg'
try {
    Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $package
    if ((Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash.ToLowerInvariant() -cne $packageHash) {
        throw 'Official WARP package digest mismatch.'
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($package)
    try {
        $entry = $zip.GetEntry('build/native/bin/x64/d3d10warp.dll')
        if ($null -eq $entry) { throw 'Pinned package has no x64 WARP DLL.' }
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $dll)
    } finally { $zip.Dispose() }
    if ((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant() -cne $dllHash) {
        throw 'Official x64 WARP DLL digest mismatch.'
    }
    $version = [Diagnostics.FileVersionInfo]::GetVersionInfo($dll)
    if ([string]::IsNullOrWhiteSpace($version.FileVersion)) { throw 'WARP file version unavailable.' }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $receipt) | Out-Null
    [IO.File]::WriteAllText($receipt, ([ordered]@{
        schema = 'dlss5-ci-warp-runtime-v1'; version = '1.0.22'
        package_url = $url; package_sha256 = $packageHash
        dll_path = $dll; dll_sha256 = $dllHash
        dll_bytes = (Get-Item -LiteralPath $dll).Length
        file_version = $version.FileVersion; product_version = $version.ProductVersion
        test_only = $true; redistributed = $false
        ci_warp_runtime_helper_sha256 = (Get-FileHash -LiteralPath (Join-Path $repo 'tests/ci_warp_runtime.h') -Algorithm SHA256).Hash.ToLowerInvariant()
        image_os = $env:ImageOS; image_version = $env:ImageVersion
    } | ConvertTo-Json), (New-Object Text.UTF8Encoding($false)))
    # Do not export process configuration via GITHUB_ENV. Each GPU step opens
    # its own verified context; unrelated CPU and A/B diagnostic steps stay separate.
    Write-Host ('Prepared test-only Microsoft WARP 1.0.22: ' + $dllHash)
} catch {
    Remove-Item -LiteralPath $private -Recurse -Force -ErrorAction SilentlyContinue
    throw
}
