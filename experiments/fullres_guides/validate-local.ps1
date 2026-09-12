[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EvidenceDirectory,
    [switch]$BuildAddon,
    [string]$FfxRoot,
    [string[]]$FfxLibraries = @(),
    [string]$NvofInclude,
    [string]$ReshadeInclude,
    [string]$ImguiInclude
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'This script requires Windows. It does not substitute CPU tests for Windows compilation.'
}
$source = $PSScriptRoot
$evidence = [IO.Path]::GetFullPath($EvidenceDirectory)
if (Test-Path -LiteralPath $evidence) { throw 'EvidenceDirectory must be new; existing evidence is never overwritten.' }
$repo = [IO.Path]::GetFullPath((Join-Path $source '..\..'))
# Keep logs/binaries outside the source repository and never deploy to a game.
if ($evidence.Equals($repo, [StringComparison]::OrdinalIgnoreCase) -or
    $evidence.StartsWith($repo.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Use a new evidence directory outside the repository.'
}
$null = New-Item -ItemType Directory -Path $evidence
$checks = [Collections.Generic.List[object]]::new()
function SourceHashes {
    @(Get-ChildItem -LiteralPath $source -File | Sort-Object Name | ForEach-Object {
        [ordered]@{ name = $_.Name; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
    })
}
$before = SourceHashes
$record = [ordered]@{
    schema = 1; startedUtc = [DateTime]::UtcNow.ToString('o'); status = 'failed';
    addonRequested = [bool]$BuildAddon; source = $before; checks = $checks;
    sdkGpu = 'not-run'; game = 'not-run'; neural = 'not-connected';
    note = 'Shader compiler execution is not GPU execution. No package/game deployment occurs.'
}
function RunChecked([string]$Name, [string]$Program, [string[]]$Arguments) {
    $log = Join-Path $evidence ($Name + '.txt')
    $row = [ordered]@{ name = $Name; result = 'failed'; exitCode = $null; log = [IO.Path]::GetFileName($log) }
    $checks.Add($row)
    $exe = (Get-Command $Program -CommandType Application -ErrorAction Stop).Source
    # Fail-fast on each actual exit code; do not treat log creation as success.
    & $exe @Arguments *> $log
    $row.exitCode = $LASTEXITCODE
    if ($LASTEXITCODE -ne 0) { throw "$Name failed; inspect $($row.log)." }
    $row.result = 'passed'
    Write-Host "$Name passed"
}
try {
    $configure = @('-S', $source, '-B', (Join-Path $evidence 'build'), '-A', 'x64',
        '-DNR_OF_BUILD_SHADER_CHECK=ON', ('-DNR_OF_BUILD_ADDON=' + $(if ($BuildAddon) { 'ON' } else { 'OFF' })))
    if ($BuildAddon) {
        if (-not $FfxRoot -or $FfxLibraries.Count -eq 0 -or -not $NvofInclude -or -not $ReshadeInclude -or -not $ImguiInclude) {
            throw 'BuildAddon requires explicit approved SDK libraries and matching ReShade/ImGui headers.'
        }
        foreach ($lib in $FfxLibraries) {
            if (-not [IO.Path]::IsPathRooted($lib) -or -not (Test-Path -LiteralPath $lib -PathType Leaf)) {
                throw 'Each FfxLibraries item must be an existing absolute library path.'
            }
        }
        $record.sdkLibraries = @($FfxLibraries | ForEach-Object {
            [ordered]@{ name = [IO.Path]::GetFileName($_); sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash }
        })
        $configure += @("-DFFX_ROOT=$FfxRoot", ('-DFFX_LIBRARIES=' + ($FfxLibraries -join ';')),
            "-DNVOF_INCLUDE=$NvofInclude", "-DRESHADE_INCLUDE=$ReshadeInclude", "-DIMGUI_INCLUDE=$ImguiInclude")
    }
    RunChecked 'configure' 'cmake' $configure
    RunChecked 'compile' 'cmake' @('--build', (Join-Path $evidence 'build'), '--config', 'Release')
    RunChecked 'tests' 'ctest' @('--test-dir', (Join-Path $evidence 'build'), '-C', 'Release', '--output-on-failure', '--no-tests=error')
    $after = SourceHashes
    if (($before | ConvertTo-Json -Depth 5 -Compress) -cne ($after | ConvertTo-Json -Depth 5 -Compress)) {
        throw 'Source changed during validation. Keep this failed record and rerun on a frozen worktree.'
    }
    $record.status = 'passed'
    $record.artifacts = @(Get-ChildItem -LiteralPath (Join-Path $evidence 'build') -File -Recurse |
        Where-Object { $_.Extension -in @('.exe', '.addon64') } | ForEach-Object {
            [ordered]@{ name = $_.Name; bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
        })
} catch {
    $record.failure = $_.Exception.Message
    throw
} finally {
    $record.finishedUtc = [DateTime]::UtcNow.ToString('o')
    $record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $evidence 'validation.json') -Encoding UTF8
}
