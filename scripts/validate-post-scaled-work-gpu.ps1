param(
    [Parameter(Mandatory = $true)]
    [string]$SourceRoot,
    [string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT,
    [string]$OutputRoot = ""
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($ToolchainRoot)) {
    throw 'ToolchainRoot is required (or set DLSS5_LLVM_MINGW_ROOT).'
}

$root = Split-Path -Parent $PSScriptRoot
$sourceRootResolved = (Resolve-Path -LiteralPath $SourceRoot).Path
$toolchain = (Resolve-Path -LiteralPath $ToolchainRoot).Path
$source = Join-Path $sourceRootResolved 'src/nr_before_sr.cpp'
$descriptors = Join-Path $sourceRootResolved 'src/nr_codec_descriptors.inl'
$motion = Join-Path $sourceRootResolved 'src/interop/nr_motion_access_d3d12.inl'
$template = Join-Path $root 'tests/post_scaled_work_gpu.cpp.in'
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $root 'build/post-scaled-work-gpu'
}
$build = [IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Force -Path $build | Out-Null

function Block([string]$Text, [string]$Signature) {
    $start = $Text.IndexOf($Signature, [StringComparison]::Ordinal)
    if ($start -lt 0) { throw "Production block missing: $Signature" }
    $brace = $Text.IndexOf('{', $start)
    if ($brace -lt 0) { throw "Opening brace missing: $Signature" }
    $depth = 0
    for ($i = $brace; $i -lt $Text.Length; ++$i) {
        if ($Text[$i] -eq '{') { ++$depth }
        if ($Text[$i] -eq '}') {
            --$depth
            if ($depth -eq 0) { return $Text.Substring($start, $i - $start + 1) }
        }
    }
    throw "Unterminated production block: $Signature"
}

# Explicit UTF8: PS 5.1's default ANSI reader would mojibake source/template.
$text = (Get-Content -LiteralPath $source -Raw -Encoding UTF8).Replace("`r`n", "`n")
$templateText = Get-Content -LiteralPath $template -Raw -Encoding UTF8
$descText = Get-Content -LiteralPath $descriptors -Raw -Encoding UTF8
$motionText = Get-Content -LiteralPath $motion -Raw -Encoding UTF8
$shader = [regex]::Match($text, '(?s)const char kCodecShader\[\] = R"HLSL\(.*?\)HLSL";').Value
$constants = [regex]::Match($text, '(?s)struct CodecConstants \{.*?\n\};').Value
$descriptorConstants = [regex]::Match($text, '(?s)constexpr unsigned int kCodecSrvCount.*?constexpr unsigned int kStageCaptureUavIndex[^;]+;').Value
if (-not $shader -or -not $constants -or -not $descriptorConstants) {
    throw 'Production shader/constants extraction failed'
}
$motionFormat = Block $motionText 'DXGI_FORMAT NrMotionReadFormat('
$alignWork = (Block $text 'unsigned int AlignWorkExtent(') + "`n`n" +
    (Block $text 'void SelectAlignedWorkExtent(')
if ($shader -notmatch 'float4 LoadPostArea' -or $shader -notmatch 'const bool scaled = ModelWidth != Width') {
    throw 'SourceRoot does not contain the actual staged post-scaling shader'
}
if ($descText -notmatch 'source_srv_format' -or $descText -notmatch 'NrMotionReadFormat') {
    throw 'SourceRoot descriptor writer is not the typed post-compat version'
}
$generated = $templateText.
    Replace('// @PRODUCTION_DESCRIPTOR_CONSTANTS@', $descriptorConstants).
    Replace('// @PRODUCTION_SHADER@', $shader).
    Replace('// @PRODUCTION_CONSTANTS@', $constants).
    Replace('// @PRODUCTION_MOTION_READ_FORMAT@', $motionFormat).
    Replace('// @PRODUCTION_ALIGN_WORK@', $alignWork).
    Replace('// @PRODUCTION_DESCRIPTOR_WRITER@', $descText)
if ($generated -match '@PRODUCTION_') { throw 'Unexpanded production placeholder' }
$outSource = Join-Path $build 'post_scaled_work_gpu.cpp'
[IO.File]::WriteAllText($outSource, $generated, (New-Object Text.UTF8Encoding($false)))
$compiler = Join-Path $toolchain 'bin/x86_64-w64-mingw32-clang++.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw "Missing compiler: $compiler" }
$exe = Join-Path $build 'post_scaled_work_gpu.exe'
& $compiler '-std=c++17' '-O2' '-static' '-Wall' '-Wextra' '-Werror' $outSource '-o' $exe "-I$(Join-Path $sourceRootResolved 'src/interop')" '-ld3d12' '-ldxgi' '-ld3dcompiler' '-ldxguid' '-lole32'
if ($LASTEXITCODE) { throw "post-scaled-work GPU compile failed: $LASTEXITCODE" }
# PS 5.1 can turn native stderr into terminating NativeCommandError records.
# Capture streams, surface them, and use the process exit code.
$stdout = Join-Path $build 'stdout.txt'
$stderr = Join-Path $build 'stderr.txt'
$process = Start-Process -FilePath $exe -NoNewWindow -Wait -PassThru `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr
if (Test-Path -LiteralPath $stdout) { Get-Content -LiteralPath $stdout -Encoding UTF8 | Write-Output }
if (Test-Path -LiteralPath $stderr) { Get-Content -LiteralPath $stderr -Encoding UTF8 | Write-Output }
if ($process.ExitCode) { throw "post-scaled-work GPU validation failed: $($process.ExitCode)" }
$hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "PASS actual staged post-scaling HLSL + production descriptor writer; source SHA256=$hash"
Write-Host 'Neural step is identity copy or deterministic nonzero perturbation ONLY inside this test. It is NOT NVIDIA NR or game runtime verification.'
