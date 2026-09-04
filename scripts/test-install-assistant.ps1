param([string]$Dotnet = 'C:\Users\pc1\Documents\ChatGPT\DLSS5\dlss5-lab\toolchains\dotnet10\dotnet.exe')
$ErrorActionPreference = 'Stop'
$project = Join-Path (Split-Path -Parent $PSScriptRoot) 'tools\install-assistant.Tests\InstallAssistant.Tests.csproj'
& $Dotnet build $project
if ($LASTEXITCODE -ne 0) { throw 'install assistant tests failed to build' }
& $Dotnet run --project $project --no-build
if ($LASTEXITCODE -ne 0) { throw 'install assistant tests failed' }
