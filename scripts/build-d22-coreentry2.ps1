param(
    [Parameter(Mandatory=$true)][string]$UpstreamRoot,
    [Parameter(Mandatory=$true)][string]$ToolchainRoot,
    [Parameter(Mandatory=$true)][string]$ReShadeSourceRoot,
    [Parameter(Mandatory=$true)][string]$DirectXMathRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [string]$NrchainReuseRoot
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$Root=Split-Path -Parent $PSScriptRoot
$Output=[IO.Path]::GetFullPath($OutputRoot)
$Gate=$Output+'.core-entry-gate'
if ((Test-Path -LiteralPath $Output) -or (Test-Path -LiteralPath $Gate)) { throw 'Choose a NEW output path; no overwrite.' }
$Head=(& git -C $Root rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $Head -notmatch '^[0-9a-f]{40}$') { throw 'Missing candidate HEAD.' }
$Dirty=@(& git -C $Root status --porcelain --untracked-files=no)
if ($LASTEXITCODE -ne 0 -or $Dirty.Count) { throw 'Commit the isolated candidate before generation.' }
$LockBlob=(& git -C $Root rev-parse HEAD:src/nr_command_state.inl).Trim()
if ($LASTEXITCODE -ne 0 -or $LockBlob -ne 'b515acb10b55e4f988e32c247db2a1828e30549a') { throw 'Reviewed lockperf1 state changed. Review rather than restore old global locking.' }
$RawCore=Get-Content -LiteralPath (Join-Path $Root 'src/nr_before_sr.cpp') -Raw -Encoding UTF8
if ($RawCore.Contains('TryRecoverDirectCoreWrapperAlias')) { throw 'Speculative Evaluate alias helper is still present; use the complete coreentry2 commit.' }
# This gate uses real GetProcAddress/MinHook on isolated fake NGX DLLs.
& python (Join-Path $PSScriptRoot 'validate-core-create-windows.py') --toolchain $ToolchainRoot --output $Gate --minhook (Join-Path $Root 'third_party/minhook')
if ($LASTEXITCODE -ne 0) { throw 'Core-entry regression gate failed. No game candidate accepted.' }
& (Join-Path $PSScriptRoot 'build-multi-api-candidate.ps1') @PSBoundParameters
if ($LASTEXITCODE -ne 0) { throw 'Canonical generated Core build failed.' }
$Manifest=Get-Content -LiteralPath (Join-Path $Output 'source/source-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$Validation=Get-Content -LiteralPath (Join-Path $Output 'validation.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$GeneratedCore=Get-Content -LiteralPath (Join-Path $Output 'source/core/src/nr_before_sr.cpp') -Raw -Encoding UTF8
$GeneratedFeedback=Get-Content -LiteralPath (Join-Path $Output 'source/core/src/nr_feedback_capture.inl') -Raw -Encoding UTF8
if (-not $GeneratedCore.Contains('#include "nr_core_create_boundary.inl"') -or -not $GeneratedCore.Contains('(void)InstallCoreCreateBoundaryHook();') -or -not $GeneratedFeedback.Contains('#include "nr_core_create_boundary_feedback.inl"')) { throw 'Source stage did not integrate the actual entry helper/worker/feedback.' }
if ($Manifest.source_commit -ne $Head -or $Manifest.integration.version -ne 'beta0.5-dline22-coreentry2') { throw 'Candidate identity mismatch.' }
if (-not $Validation.source_verified -or -not $Validation.compile_verified -or -not $Validation.warp_verified) { throw 'Canonical source/compile/WARP gates incomplete.' }
$Build=Join-Path $Output 'source/core/build'
$Addons=@(Get-ChildItem -LiteralPath $Build -Filter '*.addon64' -File)
if ($Addons.Count -ne 2) { throw 'Expected two ordinary language builds.' }
$Maps=@(Get-ChildItem -LiteralPath (Join-Path $Build 'obj') -Filter '*.map' -File)
if ($Maps.Count -lt 2) { throw 'Preserve exact linker maps before testing.' }
$Symbols=Join-Path $Output 'PRIVATE-SYMBOLS';New-Item -ItemType Directory -Path $Symbols | Out-Null
foreach($Map in $Maps){Copy-Item -LiteralPath $Map.FullName -Destination $Symbols}
$Rows=@()
foreach($Addon in $Addons){
    $Language=if($Addon.Name -match '\.en\.addon64$'){'en'}else{'zh-CN'}
    $Folder=Join-Path $Output ('TEST-D22-'+$Language);New-Item -ItemType Directory -Path $Folder | Out-Null
    $Target=Join-Path $Folder ('nr-before-sr.D22.'+$Language+'.addon64')
    Copy-Item -LiteralPath $Addon.FullName -Destination $Target
    $Rows += [ordered]@{language=$Language;file=(Split-Path -Leaf $Target);sha256=(Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash.ToLowerInvariant()}
    $Note=@('D22-coreentry2 candidate; game NOT verified.',('Source: '+$Head),'Exit game. Keep exactly one Core addon in all addon search paths.','Do not overwrite model/NRchain/FG/SR/RR DLLs or INI.','Cold-launch SOTTR; F6 cannot reset a previously latched process.','If still inactive, one F8 after testing is enough; coreEntry fields distinguish the new first blocker.') -join "`r`n"
    [IO.File]::WriteAllText((Join-Path $Folder 'READ-FIRST.txt'),$Note,(New-Object Text.UTF8Encoding($false)))
}
[ordered]@{candidate='beta0.5-dline22-coreentry2';head=$Head;ordinary=$true;windows_core_entry_gate=$Gate;canonical_validation=(Join-Path $Output 'validation.json');game_verified=$false;NVIDIA_verified=$false;deployed=$false;binaries=$Rows} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $Output 'D22-CANDIDATE.json') -Encoding UTF8
Write-Host 'D22 ordinary candidate ready for owner validation. No game directory was changed.'
Write-Host $Output
