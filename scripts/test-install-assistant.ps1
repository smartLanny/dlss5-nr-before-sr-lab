param(
    [string]$Dotnet = 'C:\Users\pc1\Documents\ChatGPT\DLSS5\dlss5-lab\toolchains\dotnet10\dotnet.exe',
    [Parameter(Mandatory=$true)][string]$VerifiedHostPath,
    [string]$PackageRoot
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$project = Join-Path $root 'tools/install-assistant.Tests/InstallAssistant.Tests.csproj'
if ((Get-FileHash -LiteralPath $VerifiedHostPath -Algorithm SHA256).Hash -ne '0CEE63F9C9F13F3AC909C5B4903F4DBB4B719A7AB3B4F13B0DEAF83C814B94F7') { throw 'Tests require the pinned official FullAddon host; arbitrary mocks are not accepted.' }
$output = Join-Path $root 'test-output/installer'
New-Item -ItemType Directory -Path $output -Force | Out-Null
& $Dotnet run --project $project -- $VerifiedHostPath | Tee-Object -FilePath (Join-Path $output 'transaction-tests.txt')
if ($LASTEXITCODE -ne 0) { throw 'Installer production-seam regressions failed.' }
& $Dotnet build (Join-Path $root 'tools/install-assistant/Dlss5InstallAssistant.csproj') -c Release
if ($LASTEXITCODE -ne 0) { throw 'WPF build failed.' }
& $Dotnet build (Join-Path $root 'tools/install-assistant.Helper/Dlss5InstallAssistant.Helper.csproj') -c Release
if ($LASTEXITCODE -ne 0) { throw 'Helper build failed.' }
if ($PackageRoot) {
    foreach ($package in Get-ChildItem -LiteralPath $PackageRoot -Directory -Filter 'offline-package-*') {
        & $Dotnet run --project $project --no-build -- --verify-production-package $package.FullName
        if ($LASTEXITCODE -ne 0) { throw "Production signed package validation failed: $($package.Name)" }
    }
}
Write-Host 'PASS production transactions, signed payloads, process locks, recovery, identity, helper result, WPF and helper builds.'
