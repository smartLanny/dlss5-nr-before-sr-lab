param(
    [string]$Dotnet = 'C:\Users\pc1\Documents\ChatGPT\DLSS5\dlss5-lab\toolchains\dotnet10\dotnet.exe',
    [string]$OutputRoot = 'test-output/installer/publish'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$output = [IO.Path]::GetFullPath((Join-Path $root $OutputRoot))
& $Dotnet publish (Join-Path $root 'tools/install-assistant/Dlss5InstallAssistant.csproj') -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:DebugType=None -p:DebugSymbols=false -o $output
if ($LASTEXITCODE -ne 0) { throw 'WPF publish failed.' }
$helper = Join-Path $root 'test-output/installer/helper-publish'
& $Dotnet publish (Join-Path $root 'tools/install-assistant.Helper/Dlss5InstallAssistant.Helper.csproj') -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:DebugType=None -p:DebugSymbols=false -o $helper
if ($LASTEXITCODE -ne 0) { throw 'Helper publish failed.' }
Copy-Item -LiteralPath (Join-Path $helper 'DLSS5InstallAssistant.Helper.exe') -Destination $output -Force
Write-Host "Built self-contained Windows x64 app/helper at $output. Elevation requires final signed release envelope."
