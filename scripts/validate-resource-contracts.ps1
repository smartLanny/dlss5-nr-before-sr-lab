param(
    [string]$Source = (Join-Path $PSScriptRoot '..\src\nr_before_sr.cpp'),
    [string]$ToolchainRoot = $env:DLSS5_LLVM_MINGW_ROOT
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([string]::IsNullOrWhiteSpace($ToolchainRoot)) {
    throw 'Provide -ToolchainRoot or set DLSS5_LLVM_MINGW_ROOT.'
}
$compiler = Join-Path $ToolchainRoot 'bin\x86_64-w64-mingw32-clang++.exe'
$sourceText = Get-Content -LiteralPath $Source -Raw

# Compile the production functions with a synthetic NGX parameter vtable. This
# checks the real dispatch and admission code without a game or runtime DLL.
function Read-Function([string]$Signature) {
    $start = $sourceText.IndexOf($Signature, [StringComparison]::Ordinal)
    if ($start -lt 0) { throw "Function not found: $Signature" }
    $open = $sourceText.IndexOf('{', $start)
    $depth = 1
    $end = $open + 1
    while ($end -lt $sourceText.Length -and $depth -gt 0) {
        if ($sourceText[$end] -eq '{') { ++$depth }
        if ($sourceText[$end] -eq '}') { --$depth }
        ++$end
    }
    if ($depth -ne 0) { throw "Unterminated function: $Signature" }
    return $sourceText.Substring($start, $end - $start)
}

$getterStart = $sourceText.IndexOf('struct NgxParams {', [StringComparison]::Ordinal)
$getterEnd = $sourceText.IndexOf('bool GetInt(', $getterStart, [StringComparison]::Ordinal)
if ($getterStart -lt 0 -or $getterEnd -lt 0) { throw 'NGX getter section not found.' }
$getterCode = $sourceText.Substring($getterStart, $getterEnd - $getterStart)
$floatCode = Read-Function 'bool GetFloat('
$sanitizeCode = Read-Function 'float SanitizeExposure('
$readColorCode = Read-Function 'ColorContract ReadColorContract('
$unityCode = Read-Function 'bool IsUnityExposureContract('
$r8Code = ''
if ($sourceText.Contains('bool IsPreSrBoundedR8Contract(')) {
    $r8Code = Read-Function 'bool IsPreSrBoundedR8Contract('
}
$admission = [regex]::Match($sourceText, 'const bool live_r8_admitted\s*=\s*(?<expression>[^;]+);')
if (-not $admission.Success) { throw 'Live R8 admission expression not found.' }
$admissionCode = $admission.Groups['expression'].Value
$harness = @'
#include <cmath>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
using NgxResult = std::uint32_t;
constexpr NgxResult kNgxSuccess = 1;
struct ID3D12Resource {};
enum DXGI_FORMAT { DXGI_FORMAT_UNKNOWN = 0 };
using UINT64 = std::uint64_t;
struct D3D12_RESOURCE_DESC { DXGI_FORMAT Format; UINT64 Width; unsigned int Height; };
D3D12_RESOURCE_DESC ID3D12Resource_GetDesc(ID3D12Resource *) { return {}; }
'@ + "`n" + $getterCode + @'

struct ColorContract {
    float pre_exposure = 1.0f;
    float exposure_scale = 1.0f;
    float source_to_canonical = 1.0f;
    ID3D12Resource *exposure_texture = nullptr;
    DXGI_FORMAT exposure_texture_format = DXGI_FORMAT_UNKNOWN;
    unsigned int exposure_texture_width = 0;
    unsigned int exposure_texture_height = 0;
    bool has_pre_exposure = false;
    bool has_exposure_scale = false;
};
std::atomic<float> g_last_pre_exposure{1}, g_last_exposure_scale{1}, g_last_source_to_canonical{1};
std::atomic<bool> g_last_exposure_texture_present{false};
'@ + "`n" + $floatCode + "`n" + $sanitizeCode + "`n" + $readColorCode + "`n" + $unityCode + "`n" + $r8Code + @'

bool Admit(const ColorContract &color_contract,
           bool create_declares_sdr, bool live_bounded_output) {
    [[maybe_unused]] const bool unity_exposure = IsUnityExposureContract(color_contract);
    return
'@ + "`n" + $admissionCode + @'
    ;
}

int failures = 0;
int checks = 0;
void Check(bool pass, const char *name) {
    ++checks;
    if (!pass) { ++failures; std::printf("FAIL: %s\n", name); }
}
ID3D12Resource typed_object, void_object, poison_object;
NgxResult typed_result = 0, void_result = 0;
ID3D12Resource *typed_value = nullptr;
void *void_value = nullptr;
int typed_calls = 0, void_calls = 0;
float raw_pre_exposure = 1.0f, raw_exposure_scale = 1.0f;
NgxResult Float(const NgxParams *, const char *key, float *value) {
    *value = std::strcmp(key, "DLSS.Pre.Exposure") == 0
        ? raw_pre_exposure : raw_exposure_scale;
    return kNgxSuccess;
}
NgxResult Typed(const NgxParams *, const char *, ID3D12Resource **value) {
    ++typed_calls; *value = typed_value; return typed_result;
}
NgxResult Untyped(const NgxParams *, const char *, void **value) {
    ++void_calls; *value = void_value; return void_result;
}
void Reset() {
    typed_result = void_result = 0;
    typed_value = nullptr; void_value = nullptr;
    typed_calls = void_calls = 0;
}
int main() {
    void *table[15] = {};
    table[9] = reinterpret_cast<void *>(&Typed);
    table[8] = reinterpret_cast<void *>(&Untyped);
    table[14] = reinterpret_cast<void *>(&Float);
    NgxParams params{table};
    ID3D12Resource *resource = &poison_object;
    Reset(); typed_result = void_result = kNgxSuccess;
    typed_value = &typed_object; void_value = &void_object;
    Check(GetResource(&params, "Color", &resource) && resource == &typed_object &&
          typed_calls == 1 && void_calls == 0, "typed nonnull wins without fallback");
    for (const char *key : {"Color", "Depth", "MotionVectors", "Output", "ExposureTexture"}) {
        Reset(); typed_value = &poison_object; void_result = kNgxSuccess;
        void_value = &void_object; resource = &poison_object;
        Check(GetResource(&params, key, &resource) && resource == &void_object &&
              typed_calls == 1 && void_calls == 1, "recognized resource uses untyped fallback");
    }
    Reset(); typed_result = void_result = kNgxSuccess; void_value = &void_object;
    Check(GetResource(&params, "Color", &resource) && resource == &void_object &&
          void_calls == 1, "typed success with null still uses fallback");
    Reset(); typed_value = &poison_object; void_value = &poison_object;
    resource = &poison_object;
    Check(!GetResource(&params, "Color", &resource) && resource == nullptr,
          "failed getters cannot leak dirty output pointers");
    Reset(); typed_result = void_result = kNgxSuccess; resource = &poison_object;
    Check(!GetResource(&params, "Color", &resource) && resource == nullptr,
          "null results are not accepted as resources");
    Reset(); void_result = kNgxSuccess; void_value = &void_object;
    resource = &poison_object;
    Check(!GetResource(&params, "Unrelated.Pointer", &resource) && resource == nullptr &&
          void_calls == 0, "unknown keys never use untyped fallback");
    Reset(); resource = &poison_object;
    Check(!GetResource(nullptr, "Color", &resource) && resource == nullptr,
          "missing parameter block clears caller output");

    ColorContract contract;
    Check(Admit(contract, true, false), "SDR keeps missing scalar compatibility");
    Check(!Admit(contract, false, true), "HDR R8 cannot assume absent scalars are unity");
    contract.has_pre_exposure = true;
    Check(!Admit(contract, false, true), "HDR R8 requires explicit exposure scale");
    contract.has_pre_exposure = false; contract.has_exposure_scale = true;
    Check(!Admit(contract, false, true), "HDR R8 requires explicit pre-exposure");
    contract.has_pre_exposure = true;
    Check(Admit(contract, false, true), "HDR R8 accepts explicit unity and live R8 output");
    Check(!Admit(contract, false, false), "HDR R8 also needs matching live output");
    contract.pre_exposure = 2;
    Check(!Admit(contract, false, true), "HDR R8 rejects non-unity pre-exposure");
    contract.pre_exposure = 1; contract.exposure_scale = 0.5f;
    Check(!Admit(contract, false, true), "HDR R8 rejects non-unity exposure scale");
    contract.exposure_scale = 1; contract.exposure_texture = &typed_object;
    Check(!Admit(contract, false, true) && !Admit(contract, true, true),
          "live exposure texture prevents bounded R8 admission");
    Reset();
    Check(Admit(ReadColorContract(&params), false, true),
          "explicit raw unity scalars retain HDR R8 admission");
    raw_pre_exposure = 0.0000001f;
    Check(!Admit(ReadColorContract(&params), false, true),
          "invalid tiny pre-exposure cannot sanitize into explicit unity evidence");
    raw_pre_exposure = 1; raw_exposure_scale = 0.0000001f;
    Check(!Admit(ReadColorContract(&params), false, true),
          "invalid tiny exposure scale cannot sanitize into explicit unity evidence");
    raw_exposure_scale = 0;
    Check(!Admit(ReadColorContract(&params), false, true),
          "zero is not explicit unity exposure");
    raw_exposure_scale = std::numeric_limits<float>::quiet_NaN();
    Check(!Admit(ReadColorContract(&params), false, true),
          "NaN is not explicit unity exposure");
    std::printf("Resource contract regression: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
'@
# initializer-list header is deliberately outside the extracted code.
$harness = "#include <initializer_list>`n" + $harness
$outputDirectory = Join-Path (Split-Path -Parent $PSScriptRoot) 'build\resource-contract-tests'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$cpp = Join-Path $outputDirectory 'resource_contracts.cpp'
$exe = Join-Path $outputDirectory 'resource_contracts.exe'
[IO.File]::WriteAllText($cpp, $harness, [Text.UTF8Encoding]::new($false))
& $compiler '-std=c++20' '-O2' '-static' '-Wall' '-Wextra' '-Wpedantic' $cpp '-o' $exe
if ($LASTEXITCODE -ne 0) { throw 'Resource contract harness compilation failed.' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'Resource contract regression failed.' }
