param(
    [Parameter(Mandatory=$true)][string]$ProjectRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [string]$NgxSdkRoot = $env:DLSS5_NGX_SDK_ROOT,
    [string]$VcVarsPath = $env:DLSS5_VCVARS_PATH,
    [string]$ReuseBridgeRoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Root = (Resolve-Path -LiteralPath $ProjectRoot).Path
$Output = [IO.Path]::GetFullPath($OutputRoot)
$Pins = [ordered]@{
    'LICENSE.txt' = 'dc2778a3283427285984cdb5b3f7f03eae7d8a06057f672f2999c5ee7fd4f67d'
    'include/nvsdk_ngx.h' = '35d19dba363786d27d11536a4628a430596477f39dbfed5eae482132ae6e794a'
    'include/nvsdk_ngx_defs.h' = 'a9481cbc78d53ee56639f0a573ec095c6c1ea1e5f296d139c20c42bf342c2e1b'
    'include/nvsdk_ngx_params.h' = '51a1f94116cf82f95c63a2c197371ed22e08bddb09918d36cc6e2ac712753c80'
    'lib/Windows_x86_64/x64/nvsdk_ngx_s.lib' = '36eab29264c2a06456ba8415f20086aea36f0cbb314d020085cb288cc06bb9ae'
}
$Source = Join-Path $Root 'src/nrchain_nvngx.cpp'
$Binary = Join-Path $Output 'nrchain_nvngx.dll'
if ($ReuseBridgeRoot) {
    # This explicit path reuses an existing official-SDK build only when its
    # receipt matches the current source bytes and every pinned SDK input.
    # Preserve its original receipt and mark the result as reused, never rebuilt.
    $Reuse = (Resolve-Path -LiteralPath $ReuseBridgeRoot).Path
    if ($Reuse -eq $Output) { throw 'Reuse source and build output must differ.' }
    $ReceiptPath = Join-Path $Reuse 'nrchain-build.json'
    $Receipt = Get-Content -LiteralPath $ReceiptPath -Raw -Encoding utf8 | ConvertFrom-Json
    if ($Receipt.schema -ne 'nrchain-official-ngx-build-v1' -or
        $Receipt.sdk -ne 'NVIDIA/DLSS' -or
        $Receipt.sdk_commit -ne 'a291cc7d2cc642a51566f3dfd5376f635cd1b284' -or
        $Receipt.crt -ne 'static /MT') { throw 'Unrecognized official NGX bridge receipt.' }
    foreach ($Name in $Pins.Keys) {
        if ($Receipt.input_sha256.$Name -ne $Pins[$Name]) { throw "Bridge receipt SDK input changed: $Name" }
    }
    if ((Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Receipt.source_sha256) {
        throw 'Reusable bridge source differs from the current production source.'
    }
    if ((Get-FileHash -LiteralPath (Join-Path $Reuse 'nrchain_nvngx.dll') -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Receipt.binary_sha256) {
        throw 'Reusable bridge binary does not match its build receipt.'
    }
    if ((Get-FileHash -LiteralPath (Join-Path $Reuse 'NVIDIA-NGX-LICENSE.txt') -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Pins['LICENSE.txt']) {
        throw 'Reusable bridge official SDK license changed.'
    }
    $ReceiptHash = (Get-FileHash -LiteralPath $ReceiptPath -Algorithm SHA256).Hash.ToLowerInvariant()
    New-Item -ItemType Directory -Path $Output -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $Reuse 'nrchain_nvngx.dll') -Destination $Binary -Force
    if ((Get-FileHash -LiteralPath $Binary -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Receipt.binary_sha256) {
        throw 'Reused bridge copy changed.'
    }
    Copy-Item -LiteralPath (Join-Path $Reuse 'NVIDIA-NGX-LICENSE.txt') -Destination (Join-Path $Output 'NVIDIA-NGX-LICENSE.txt') -Force
    Copy-Item -LiteralPath $ReceiptPath -Destination (Join-Path $Output 'nrchain-original-build.json') -Force
    $Receipt | Add-Member -NotePropertyName reused_binary -NotePropertyValue $true -Force
    $Receipt | Add-Member -NotePropertyName original_receipt_sha256 -NotePropertyValue $ReceiptHash -Force
    $Receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Output 'nrchain-build.json') -Encoding utf8
    Write-Host 'Reused verified identical-source official NGX bridge; no new bridge compilation.'
    $global:LASTEXITCODE = 0
    return
}
if (!$NgxSdkRoot -or !$VcVarsPath) {
    throw 'The production cold NGX bridge requires -NgxSdkRoot and -VcVarsPath, or explicit -ReuseBridgeRoot with an identical-source official SDK build receipt.'
}
$Sdk = (Resolve-Path -LiteralPath $NgxSdkRoot).Path
$VcVars = (Resolve-Path -LiteralPath $VcVarsPath).Path
foreach ($Name in $Pins.Keys) {
    if ((Get-FileHash -LiteralPath (Join-Path $Sdk $Name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Pins[$Name]) {
        throw "Official NGX SDK input changed: $Name"
    }
}
New-Item -ItemType Directory -Path $Output -Force | Out-Null
# Only these validated absolute paths enter the command file. Reject cmd.exe
# expansion/control characters; JSON encoding is not shell quoting.
foreach ($Path in @($Root,$Sdk,$VcVars,$Output,$Source,$Binary)) {
    if ($Path -match '[\r\n"%!]') { throw 'Build paths contain unsupported command-file characters.' }
}
$CommandFile = Join-Path $Output 'build-nrchain.cmd'
$Lines = @(
    '@echo off',
    ('call "' + $VcVars + '" x64'),
    'if errorlevel 1 exit /b 1',
    ('cl /nologo /LD /EHsc /O2 /MT /utf-8 /std:c++20 /W4 /Brepro /I"' + (Join-Path $Sdk 'include') +
     '" /Fo"' + (Join-Path $Output 'nrchain_nvngx.obj') + '" "' + $Source +
     '" /link /Brepro /OUT:"' + $Binary + '" /IMPLIB:"' + (Join-Path $Output 'nrchain_nvngx.lib') + '" "' + (Join-Path $Sdk 'lib/Windows_x86_64/x64/nvsdk_ngx_s.lib') +
     '" kernel32.lib user32.lib advapi32.lib ole32.lib shell32.lib'),
    'exit /b %errorlevel%'
)
[IO.File]::WriteAllLines($CommandFile, $Lines, (New-Object Text.UTF8Encoding($false)))
& cmd.exe /d /c $CommandFile
if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $Binary -PathType Leaf)) { throw 'Official SDK nrchain build failed.' }
Copy-Item -LiteralPath (Join-Path $Sdk 'LICENSE.txt') -Destination (Join-Path $Output 'NVIDIA-NGX-LICENSE.txt') -Force
[ordered]@{
    schema='nrchain-official-ngx-build-v1'; sdk='NVIDIA/DLSS'; sdk_commit='a291cc7d2cc642a51566f3dfd5376f635cd1b284';
    input_sha256=$Pins; source_sha256=(Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash.ToLowerInvariant();
    binary_sha256=(Get-FileHash -LiteralPath $Binary -Algorithm SHA256).Hash.ToLowerInvariant();
    crt='static /MT'; bootstrap='official D3D12 Init_with_ProjectID; no Feature1';
    command_file_sha256=(Get-FileHash -LiteralPath $CommandFile -Algorithm SHA256).Hash.ToLowerInvariant()
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Output 'nrchain-build.json') -Encoding utf8
