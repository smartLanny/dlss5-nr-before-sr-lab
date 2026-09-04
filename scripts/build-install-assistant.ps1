param([string]$Dotnet = 'C:\Users\pc1\Documents\ChatGPT\DLSS5\dlss5-lab\toolchains\dotnet10\dotnet.exe')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$assistant = Join-Path $root 'tools\install-assistant\Dlss5InstallAssistant.csproj'
$helper = Join-Path $root 'tools\install-assistant.Helper\Dlss5InstallAssistant.Helper.csproj'
& $Dotnet publish $assistant -c Release
if ($LASTEXITCODE -ne 0) { throw 'assistant build failed' }
& $Dotnet publish $helper -c Release
if ($LASTEXITCODE -ne 0) { throw 'helper build failed' }
$assistantOut = Join-Path $root 'tools\install-assistant\bin\Release\net10.0-windows\win-x64'
$helperOut = Join-Path $root 'tools\install-assistant.Helper\bin\Release\net10.0-windows\win-x64\DLSS5InstallAssistant.Helper.exe'
Copy-Item -LiteralPath $helperOut -Destination (Join-Path $assistantOut 'DLSS5InstallAssistant.Helper.exe') -Force
Write-Host "Built assistant and colocated minimal elevation helper."
