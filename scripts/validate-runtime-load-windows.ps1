param(
    [Parameter(Mandatory=$true)][string]$ToolchainRoot,
    [string]$SourceRoot,
    [string]$FixtureRoot,
    [string]$OutputRoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([string]::IsNullOrWhiteSpace($SourceRoot)) { $SourceRoot = Split-Path -Parent $PSScriptRoot }
if ([string]::IsNullOrWhiteSpace($FixtureRoot)) { $FixtureRoot = Split-Path -Parent $PSScriptRoot }
if ([string]::IsNullOrWhiteSpace($OutputRoot)) { $OutputRoot = Join-Path $SourceRoot 'build/runtime-load-windows' }
$Root = (Resolve-Path -LiteralPath $SourceRoot).Path
$Fixtures = (Resolve-Path -LiteralPath $FixtureRoot).Path
$Toolchain = (Resolve-Path -LiteralPath $ToolchainRoot).Path
$Output = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $Output) { throw 'OutputRoot must not exist.' }
New-Item -ItemType Directory -Path $Output | Out-Null
$Cxx = Join-Path $Toolchain 'bin/x86_64-w64-mingw32-clang++.exe'
if (-not (Test-Path -LiteralPath $Cxx)) { throw "Missing compiler: $Cxx" }
$Fixture = Join-Path $Fixtures 'tests/runtime_load_windows'
$Dependency = Join-Path $Output 'runtime-load-dependency.dll'
$ImportLibrary = Join-Path $Output 'libruntime-load-dependency.a'
$Target = Join-Path $Output 'runtime-load-target.dll'
$Harness = Join-Path $Output 'runtime-load-windows.exe'
& $Cxx '-std=c++20' '-Wall' '-Wextra' '-Werror' '-shared' '-static' `
    (Join-Path $Fixture 'dependency.cpp') '-o' $Dependency ("-Wl,--out-implib,$ImportLibrary")
if ($LASTEXITCODE) { throw 'Dependency fixture compilation failed.' }
& $Cxx '-std=c++20' '-Wall' '-Wextra' '-Werror' '-shared' '-static' `
    (Join-Path $Fixture 'target.cpp') $ImportLibrary '-o' $Target
if ($LASTEXITCODE) { throw 'Target fixture compilation failed.' }
& $Cxx '-std=c++20' '-Wall' '-Wextra' '-Werror' '-static' '-municode' `
    ("-I" + (Join-Path $Root 'src')) (Join-Path $Fixture 'harness.cpp') '-o' $Harness
if ($LASTEXITCODE) { throw 'Runtime-load harness compilation failed.' }

$Good = Join-Path $Output 'good'
$MissingDependency = Join-Path $Output 'missing-dependency'
New-Item -ItemType Directory -Path $Good, $MissingDependency | Out-Null
Copy-Item -LiteralPath $Dependency -Destination $Good
Copy-Item -LiteralPath $Target -Destination $Good
Copy-Item -LiteralPath $Target -Destination $MissingDependency
& $Harness (Join-Path $Output 'absent.dll') '2'
if ($LASTEXITCODE) { throw 'Absent target was not classified as file-absent with Win32 126.' }
& $Harness (Join-Path $MissingDependency 'runtime-load-target.dll') '3'
if ($LASTEXITCODE) { throw 'Present target with an absent dependency was not classified as dependency-missing with Win32 126.' }
& $Harness (Join-Path $Good 'runtime-load-target.dll') '1'
if ($LASTEXITCODE) { throw 'Complete fixture did not load through the restricted search path.' }
Write-Host 'Runtime-load Windows fixture PASS: absent target and absent dependency both returned Win32 126 and were distinguished by file presence.'
