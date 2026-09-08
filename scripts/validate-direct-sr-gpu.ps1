param(
    [ValidateSet('Warp','Hardware','Both')][string]$Mode = 'Both',
    [string]$EvidenceDirectory = '',
    [string]$FfxRoot = '',
    [string]$DlssRoot = '',
    [string]$VsDevCmd = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$lab = Split-Path -Parent $repo
if (-not $FfxRoot) { $FfxRoot = Join-Path $lab 'dlss5-lab\deps\FidelityFX-1.1.4-api' }
if (-not $DlssRoot) { $DlssRoot = Join-Path $lab 'dlss5-lab\staging\renodx-source\external\DLSS' }
$ffx = [IO.Path]::GetFullPath($FfxRoot)
$dlss = [IO.Path]::GetFullPath($DlssRoot)
if (-not $VsDevCmd) {
    $vswhere = Join-Path $lab 'dlss5-lab\staging\renodx-source\external\Streamline\tools\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) { throw 'Pass -VsDevCmd; vswhere was not found' }
    $install = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
    if (-not $install) { throw 'Pass -VsDevCmd; no MSVC installation was found' }
    $VsDevCmd = Join-Path $install 'Common7\Tools\VsDevCmd.bat'
}
$vsdev = [IO.Path]::GetFullPath($VsDevCmd)
if (-not $EvidenceDirectory) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $EvidenceDirectory = Join-Path $lab "evidence\direct-sr-gpu-$stamp"
}
$EvidenceDirectory = [IO.Path]::GetFullPath($EvidenceDirectory)
if (Test-Path -LiteralPath $EvidenceDirectory) { throw "Evidence directory already exists: $EvidenceDirectory" }
$bin = Join-Path $EvidenceDirectory 'bin'
New-Item -ItemType Directory -Path $bin | Out-Null

$pins = @(
    @{ Name='ffx_api/ffx_api.h'; Path=(Join-Path $ffx 'ffx-api\include\ffx_api\ffx_api.h'); Sha256='408b4203545e8299ed188fbb1d9ddfe0932b73ea6e8b14fe10edcf1f45771561' },
    @{ Name='ffx_api/ffx_api_types.h'; Path=(Join-Path $ffx 'ffx-api\include\ffx_api\ffx_api_types.h'); Sha256='dee2782813d22047c068e0fb27d1347324f8ce79d280a4672848b3b294cb12fe' },
    @{ Name='ffx_api/ffx_upscale.h'; Path=(Join-Path $ffx 'ffx-api\include\ffx_api\ffx_upscale.h'); Sha256='1155bcdcea9c10e6a95507bc9422ca062a853ac84250f0b09a637a66836b3ac1' },
    @{ Name='ffx_api/dx12/ffx_api_dx12.h'; Path=(Join-Path $ffx 'ffx-api\include\ffx_api\dx12\ffx_api_dx12.h'); Sha256='215804fba316acb81d2c463b6bbce36c53cc63465c15b8c9858554c8111fae24' },
    @{ Name='nvidia/include/nvsdk_ngx_helpers.h'; Path=(Join-Path $dlss 'include\nvsdk_ngx_helpers.h'); Sha256='c46628f150c6edee245758c878fc0af2b70f7ce582744729647cdb1969c21895' },
    @{ Name='nvidia/include/nvsdk_ngx.h'; Path=(Join-Path $dlss 'include\nvsdk_ngx.h'); Sha256='35d19dba363786d27d11536a4628a430596477f39dbfed5eae482132ae6e794a' },
    @{ Name='nvidia/include/nvsdk_ngx_params.h'; Path=(Join-Path $dlss 'include\nvsdk_ngx_params.h'); Sha256='51a1f94116cf82f95c63a2c197371ed22e08bddb09918d36cc6e2ac712753c80' },
    @{ Name='nvidia/include/nvsdk_ngx_defs.h'; Path=(Join-Path $dlss 'include\nvsdk_ngx_defs.h'); Sha256='a9481cbc78d53ee56639f0a573ec095c6c1ea1e5f296d139c20c42bf342c2e1b' },
    @{ Name='nvidia/lib/nvsdk_ngx_d.lib'; Path=(Join-Path $dlss 'lib\Windows_x86_64\x64\nvsdk_ngx_d.lib'); Sha256='31e82b4ec3242ec6e5b42c73e1f5f5e260338a6ee213bd64444f6c2fa364aa84' },
    @{ Name='nvidia/runtime/nvngx_dlss.dll'; Path=(Join-Path $dlss 'lib\Windows_x86_64\rel\nvngx_dlss.dll'); Sha256='be6e434a94ca32499515eb62ca0e6c274526055d568d0426e4c652dcdfb6ee6e' }
)
$dependencyHashes = [ordered]@{}
foreach ($entry in $pins) {
    if (-not (Test-Path -LiteralPath $entry.Path -PathType Leaf)) { throw "Missing pinned dependency: $($entry.Path)" }
    $actual = (Get-FileHash -LiteralPath $entry.Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $entry.Sha256) { throw "Pinned dependency hash mismatch: $($entry.Path) expected=$($entry.Sha256) actual=$actual" }
    $dependencyHashes[$entry.Name] = $actual
}
if (-not (Test-Path -LiteralPath $vsdev -PathType Leaf)) { throw "MSVC environment not found: $vsdev" }

$source = Join-Path $repo 'tests\direct_sr_gpu.cpp'
$exe = Join-Path $bin 'direct_sr_gpu.exe'
$compileLog = Join-Path $EvidenceDirectory 'compile.log'
$includeFfx = Join-Path $ffx 'ffx-api\include'
$includeDlss = Join-Path $dlss 'include'
$libNgx = Join-Path $dlss 'lib\Windows_x86_64\x64\nvsdk_ngx_d.lib'
$obj = Join-Path $bin 'direct_sr_gpu.obj'
$command = 'call "{0}" -arch=amd64 -host_arch=amd64 >nul && cl /nologo /std:c++17 /EHsc /MD /W4 /DUNICODE /D_UNICODE /I"{1}" /I"{2}" /I"{3}" "{4}" /Fo:"{5}" /Fe:"{6}" /link d3d12.lib dxgi.lib dxguid.lib advapi32.lib user32.lib "{7}"' -f $vsdev,$repo,$includeFfx,$includeDlss,$source,$obj,$exe,$libNgx
$compileOutput = & cmd.exe /d /s /c $command 2>&1
$compileOutput | Set-Content -LiteralPath $compileLog -Encoding utf8
if ($LASTEXITCODE -ne 0) { throw "Compile failed ($LASTEXITCODE); see $compileLog" }

$runtime = Join-Path $bin 'ngx-runtime'
New-Item -ItemType Directory -Path $runtime | Out-Null
Copy-Item -LiteralPath (Join-Path $dlss 'lib\Windows_x86_64\rel\nvngx_dlss.dll') -Destination $runtime
$sourceCommit = (& git -C $repo rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Unable to record source commit' }
$dirtyLines = @(& git -C $repo status --porcelain --untracked-files=normal)
if ($LASTEXITCODE -ne 0) { throw 'Unable to record source worktree state' }
$productionHeader = Join-Path $repo 'src\interop\nr_fsr31_dlss.h'
$manifest = [ordered]@{
    source_commit = $sourceCommit
    source_dirty = ($dirtyLines.Count -ne 0)
    test_source = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
    production_header = (Get-FileHash -LiteralPath $productionHeader -Algorithm SHA256).Hash.ToLowerInvariant()
    validation_script = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
    executable = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    ffx_commit = 'c6efa6bf7f2027b3ec94f28578bb5965eabb9e55'
    dlss_commit = 'a291cc7d2cc642a51566f3dfd5376f635cd1b284'
    dependency_sha256 = $dependencyHashes
    ffx_consumer_header_derivation = 'none-original-pinned-headers-compiled-directly'
    mode = $Mode
    game_verified = $false
    nr = $false
    fg = $false
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'manifest.json') -Encoding utf8

if ($Mode -in @('Warp','Both')) {
    & $exe --warp *>&1 | Tee-Object -FilePath (Join-Path $EvidenceDirectory 'warp.log')
    if ($LASTEXITCODE -ne 0) { throw "WARP validation failed: $LASTEXITCODE" }
}
if ($Mode -in @('Hardware','Both')) {
    & $exe --hardware $runtime *>&1 | Tee-Object -FilePath (Join-Path $EvidenceDirectory 'hardware.log')
    if ($LASTEXITCODE -ne 0) { throw "Hardware validation failed: $LASTEXITCODE" }
}
Write-Host "PASS evidence=$EvidenceDirectory"
