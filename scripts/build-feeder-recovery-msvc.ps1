param(
    [Parameter(Mandatory=$true)][string]$FeederStageRoot,
    [Parameter(Mandatory=$true)][string]$VcVarsAll,
    [Parameter(Mandatory=$true)][string]$OutputRoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Stage = (Resolve-Path -LiteralPath $FeederStageRoot).Path
$VcVars = (Resolve-Path -LiteralPath $VcVarsAll).Path
$Output = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $Output) { throw 'OutputRoot must be new; retain previous evidence.' }
$Manifest = Get-Content -LiteralPath (Join-Path $Stage 'feeder-external-nr-manifest.json') -Raw | ConvertFrom-Json
if ((Get-FileHash -LiteralPath (Join-Path $Stage 'src/dlss5-feed.cpp')).Hash -ne $Manifest.feed_cpp_sha256) {
    throw 'Staged producer source differs from the transform manifest.'
}
foreach ($Entry in $Manifest.producer_header_sha256.PSObject.Properties) {
    if ([IO.Path]::GetFileName($Entry.Name) -ne $Entry.Name) { throw 'Invalid header manifest name.' }
    if ((Get-FileHash -LiteralPath (Join-Path $Stage "nr_lab_interop/$($Entry.Name)")).Hash -ne $Entry.Value) {
        throw "Staged producer header differs from manifest: $($Entry.Name)"
    }
}
foreach ($File in @('src/dlss5-feed.cpp', 'src/version.rc',
    'nr_lab_interop/nr_external_provider_completion.h',
    'external/ngx/nvsdk_ngx.h', 'external/ngx/libs/nvsdk_ngx_d.lib',
    'external/vulkan/vulkan/vulkan.h')) {
    if (!(Test-Path -LiteralPath (Join-Path $Stage $File))) { throw "Missing real input: $File" }
}
# Command files use literal quoted paths; reject cmd expansion/metacharacters.
foreach ($Value in @($Stage, $VcVars, $Output)) {
    if ($Value -match '[%"\r\n!&|<>^]') { throw 'Build paths contain unsupported command characters.' }
}
New-Item -ItemType Directory -Path $Output | Out-Null
$Batch = @"
@echo off
call "$VcVars" x64
if errorlevel 1 exit /b 1
cd /d "$Stage"
rc /nologo /fo "$Output\version.res" src\version.rc
if errorlevel 1 exit /b 1
cl /nologo /LD /EHsc /O2 /MD /W3 /utf-8 /std:c++20 /Inr_lab_interop /Iexternal\reshade\include /Iexternal\ngx /Iexternal\vulkan /Iexternal\imgui /Iexternal\minhook\include /Fo"$Output\\" /Fd"$Output\\" src\dlss5-feed.cpp external\minhook\src\buffer.c external\minhook\src\hook.c external\minhook\src\trampoline.c external\minhook\src\hde\hde64.c /link /OUT:"$Output\dlss5-feed-external-development.addon64" /IMPLIB:"$Output\dlss5-feed.lib" "$Output\version.res" external\ngx\libs\nvsdk_ngx_d.lib version.lib kernel32.lib user32.lib advapi32.lib ole32.lib
if errorlevel 1 exit /b 1
exit /b 0
"@
$CommandFile = Join-Path $Output 'build-msvc.cmd'
[IO.File]::WriteAllText($CommandFile, $Batch, [Text.Encoding]::Default)
& $CommandFile 2>&1 | Tee-Object -FilePath (Join-Path $Output 'build-msvc.log')
if ($LASTEXITCODE -ne 0) { throw "MSVC build failed ($LASTEXITCODE); see build-msvc.log." }
$Binary = Join-Path $Output 'dlss5-feed-external-development.addon64'
if (!(Test-Path -LiteralPath $Binary)) { throw 'Compiler returned without the expected binary.' }
[ordered]@{
    source_commit=(& git -C (Split-Path -Parent $PSScriptRoot) rev-parse HEAD).Trim()
    staged_source_sha256=(Get-FileHash -LiteralPath (Join-Path $Stage 'src/dlss5-feed.cpp')).Hash.ToLowerInvariant()
    feeder_manifest=$Manifest
    binary_sha256=(Get-FileHash -LiteralPath $Binary).Hash.ToLowerInvariant()
    compile_link_verified=$true
    nvidia_feature18_verified=$false
    game_runtime_verified=$false
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $Output 'validation.json') -Encoding utf8
Write-Output 'Full MSVC Feeder compile/link passed. NVIDIA NR and game execution remain unverified.'
