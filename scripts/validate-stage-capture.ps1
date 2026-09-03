param(
    [string]$Source = (Join-Path $PSScriptRoot '..\src\nr_before_sr.cpp'),
    [string]$CaptureSource = (Join-Path $PSScriptRoot '..\src\nr_stage_capture.inl'),
    [string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([string]::IsNullOrWhiteSpace($ToolchainRoot)) {
    throw 'Provide -ToolchainRoot or set DLSS5_LLVM_MINGW_ROOT.'
}
$compiler = Join-Path $ToolchainRoot 'bin\x86_64-w64-mingw32-clang++.exe'
$sourceText = Get-Content -LiteralPath $Source -Raw
$captureText = Get-Content -LiteralPath $CaptureSource -Raw
function Read-Function([string]$Signature) {
    $start = $captureText.IndexOf($Signature, [StringComparison]::Ordinal)
    if ($start -lt 0) { throw "Function not found: $Signature" }
    $open = $captureText.IndexOf('{', $start)
    $depth = 1
    $end = $open + 1
    while ($end -lt $captureText.Length -and $depth -gt 0) {
        if ($captureText[$end] -eq '{') { ++$depth }
        if ($captureText[$end] -eq '}') { --$depth }
        ++$end
    }
    if ($depth -ne 0) { throw "Unterminated function: $Signature" }
    return $captureText.Substring($start, $end - $start)
}

$types = ''
foreach ($name in @('NrTuningSignature', 'NrDiagnosticIdentity', 'NrStageCaptureInfo')) {
    $match = [regex]::Match($sourceText, "struct $name \{[\s\S]*?\r?\n\};")
    if (-not $match.Success) { throw "Type not found: $name" }
    $types += $match.Value + "`n"
}
$functions = ''
foreach ($signature in @('bool ComputeStageCaptureGrid(', 'bool WriteStageCaptureBytes(',
        'bool WriteStageCapturePfm(', 'bool WriteStageCaptureMetadata(')) {
    $functions += (Read-Function $signature) + "`n"
}
$harness = @'
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <limits>
#include <vector>
constexpr const char *kBuildIdentity = "synthetic-stage-capture-test";
'@ + "`n" + $types + $functions + @'

unsigned int failures = 0, checks = 0;
void Check(bool pass, const char *name) {
    ++checks;
    if (!pass) { ++failures; std::printf("FAIL: %s\n", name); }
}
std::vector<char> Read(const wchar_t *path) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    DWORD size = GetFileSize(file, nullptr), read = 0;
    std::vector<char> bytes(size);
    if (!ReadFile(file, bytes.data(), size, &read, nullptr) || read != size) bytes.clear();
    CloseHandle(file);
    return bytes;
}
void Grid(unsigned int source_width, unsigned int source_height) {
    unsigned int stride = 0, width = 0, height = 0;
    UINT64 bytes = 0;
    const bool ok = ComputeStageCaptureGrid(source_width, source_height,
                                            stride, width, height, bytes);
    Check(ok && stride != 0 && (stride & (stride - 1)) == 0 &&
          width > 0 && height > 0 && width <= 1024 && height <= 1024 &&
          width == (source_width + stride - 1) / stride &&
          height == (source_height + stride - 1) / stride &&
          bytes == static_cast<UINT64>(width) * height * 64 &&
          bytes * 2 <= 64u * 1024u * 1024u,
          "sample grid is exact, bounded and power-of-two");
    Check(ok && ((height - 1u) * stride < source_height) &&
          ((width - 1u) * stride < source_width) &&
          (static_cast<UINT64>(3) * width * height + (height - 1) * width +
           (width - 1)) * 16 + 16 == bytes,
          "last sampled pixel covers exact final plane endpoint");
}
int wmain(int argc, wchar_t **argv) {
    if (argc != 2) return 2;
    Grid(1, 1); Grid(17, 13); Grid(1920, 1080); Grid(3840, 2160);
    Grid(7680, 4320); Grid(16384, 16384); Grid(1, 16384); Grid(16384, 1);
    unsigned int seed = 0x8937u;
    for (unsigned int i = 0; i < 1000; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const unsigned int w = 1 + (seed & 16383u);
        seed = seed * 1664525u + 1013904223u;
        Grid(w, 1 + (seed & 16383u));
    }
    unsigned int stride = 0, width = 0, height = 0;
    UINT64 bytes = 0;
    Check(!ComputeStageCaptureGrid(0, 1, stride, width, height, bytes) &&
          !ComputeStageCaptureGrid(1, 0, stride, width, height, bytes) &&
          !ComputeStageCaptureGrid(16385, 1, stride, width, height, bytes) &&
          !ComputeStageCaptureGrid(UINT_MAX, UINT_MAX, stride, width, height, bytes),
          "invalid extents fail before arithmetic overflow");

    const float pixels[16] = {
        1, 2, 3, 0.1f, 4, 5, 6, 0.2f,
        -10, 200, 300, 0.3f, 7, 8, 9, 0.4f};
    Check(WriteStageCapturePfm(argv[1], L"raw", pixels, 2, 2), "PFM writes raw HDR RGB");
    wchar_t path[MAX_PATH] = {};
    swprintf_s(path, L"%ls\\raw.pfm", argv[1]);
    auto image = Read(path);
    const char *header = "PF\n2 2\n-1.0\n";
    const float expected[12] = {-10, 200, 300, 7, 8, 9, 1, 2, 3, 4, 5, 6};
    Check(image.size() == std::strlen(header) + sizeof(expected) &&
          std::memcmp(image.data(), header, std::strlen(header)) == 0 &&
          std::memcmp(image.data() + std::strlen(header), expected, sizeof(expected)) == 0,
          "PFM preserves signed/bright values, little endian, bottom-up and RGB channel order");
    Check(!WriteStageCapturePfm(argv[1], L"raw", pixels, 2, 2) && Read(path) == image,
          "capture refuses to overwrite existing evidence");
    Check(!WriteStageCapturePfm(argv[1], L"bad", nullptr, 2, 2) &&
          !WriteStageCapturePfm(argv[1], L"bad", pixels, 0, 2) &&
          !WriteStageCapturePfm(argv[1], L"bad", pixels, 1025, 2),
          "PFM rejects invalid dimensions before accessing rows");

    NrStageCaptureInfo info = {};
    info.request_id = 7; info.identity.instance_id = 22; info.identity.frame_id = 333;
    info.identity.config_generation = 4; info.identity.evaluation = 55;
    info.identity.since_reset = 6; info.identity.warmed_up = true;
    info.identity.tuning.style = 2; info.identity.tuning.intensity = 1.5f;
    info.width = 960; info.height = 540; info.stride = 4;
    info.source_width = 3840; info.source_height = 2160; info.mode = 2;
    Check(WriteStageCaptureMetadata(argv[1], info, true), "metadata writes frozen identity");
    swprintf_s(path, L"%ls\\metadata.txt", argv[1]);
    auto metadata = Read(path); metadata.push_back('\0');
    Check(std::strstr(metadata.data(), "instance=22\n") &&
          std::strstr(metadata.data(), "frame=333\n") &&
          std::strstr(metadata.data(), "style=2\n") &&
          std::strstr(metadata.data(), "intensity=1.5\n") &&
          std::strstr(metadata.data(), "stride=4\n") &&
          std::strstr(metadata.data(), "BEFORE destination format quantization") &&
          std::strstr(metadata.data(), "nativeSrOutput=unavailable") &&
          std::strstr(metadata.data(), "NOT same-frame evidence"),
          "metadata states stage domains, sampling and native-SR/display limitations");
    std::printf("Stage capture CPU regression: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
'@
$outputDirectory = Join-Path (Split-Path -Parent $PSScriptRoot) 'build\stage-capture-tests'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$runDirectory = Join-Path $outputDirectory ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runDirectory -Force | Out-Null
$cpp = Join-Path $outputDirectory 'stage_capture.cpp'
$exe = Join-Path $outputDirectory 'stage_capture.exe'
[IO.File]::WriteAllText($cpp, $harness, [Text.UTF8Encoding]::new($false))
& $compiler '-std=c++20' '-O2' '-static' '-Wall' '-Wextra' '-Wpedantic' '-municode' $cpp '-o' $exe
if ($LASTEXITCODE -ne 0) { throw 'Stage capture CPU harness compilation failed.' }
& $exe $runDirectory
if ($LASTEXITCODE -ne 0) { throw 'Stage capture CPU regression failed.' }
