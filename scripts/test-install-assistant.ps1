param([string]$Dotnet = 'C:\Users\pc1\Documents\ChatGPT\DLSS5\dlss5-lab\toolchains\dotnet10\dotnet.exe')
$ErrorActionPreference = 'Stop'
$project = Join-Path (Split-Path -Parent $PSScriptRoot) 'tools\install-assistant.Tests\InstallAssistant.Tests.csproj'
& $Dotnet build $project
if ($LASTEXITCODE -ne 0) { throw 'install assistant tests failed to build' }
& $Dotnet run --project $project --no-build
if ($LASTEXITCODE -ne 0) { throw 'install assistant tests failed' }
$ui = Get-Content -LiteralPath (Join-Path (Split-Path -Parent $PSScriptRoot) 'MainWindow.xaml.cs') -Raw
foreach ($required in @('Deployer.Inspect', 'Deployer.Apply', 'installCancellation', 'CancelInstall', 'Deployer.Uninstall', 'Deployer.RestoreLatest', 'Deployer.Recover', 'SteamScanner.Scan', 'Launcher.ReadStatus', 'Launcher.Start', 'ElevationHelper.TryRunFor')) {
    if ($ui.IndexOf($required, [StringComparison]::Ordinal) -lt 0) { throw "UI engine contract missing: $required" }
}
Write-Host 'PASS UI engine contract: inspect/plan/apply/cancel/recover/uninstall/launch/status/elevation'
$release = Join-Path (Split-Path -Parent $PSScriptRoot) 'release\install-assistant-beta0.3.7-r3-final2'
if (Test-Path -LiteralPath $release) {
    foreach ($packageDir in Get-ChildItem -LiteralPath $release -Directory -Filter 'offline-package-*') {
        $manifest = Get-Content -Raw -Encoding UTF8 (Join-Path $packageDir.FullName 'manifest.json') | ConvertFrom-Json
        if ($manifest.schema -ne 'dlss5-install-manifest/v2' -or $manifest.files.Count -ne 4) { throw "invalid offline manifest: $($packageDir.Name)" }
        foreach ($item in $manifest.files) {
            $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $packageDir.FullName $item.path)).Hash
            if ($actual -cne $item.sha256) { throw "offline manifest hash mismatch: $($packageDir.Name)/$($item.path)" }
        }
    }
    Write-Host 'PASS offline manifest schema and file hashes'
}
