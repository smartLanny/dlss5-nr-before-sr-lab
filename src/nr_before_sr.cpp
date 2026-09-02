#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define CINTERFACE
#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <iterator>

#define ImTextureID ImU64
#include <imgui.h>
#include <reshade.hpp>

#include "MinHook.h"

#ifndef NR_UI_CHINESE
#define NR_UI_CHINESE 0
#endif

namespace {

using NgxResult = std::uint32_t;
constexpr NgxResult kNgxSuccess = 0x00000001u;
constexpr NgxResult kNgxFeatureNotFound = 0xBAD00004u;
constexpr bool kChineseUi = NR_UI_CHINESE != 0;

struct NgxHandle {
    std::uint32_t id;
};

// NVSDK_NGX_Parameter is a Microsoft C++ virtual interface. This project is
// built with a MinGW-targeting compiler, so calling it as a C++ class would use
// the wrong ABI. The slots below are the native MSVC x64 layout, verified from
// the shipping NGX consumer: overload groups are emitted in reverse order.
struct NgxParams {
    void **vtable;
};

template <typename Fn>
Fn ParamSlot(const NgxParams *params, std::size_t index) {
    if (params == nullptr || params->vtable == nullptr) return nullptr;
    return reinterpret_cast<Fn>(params->vtable[index]);
}

using GetVoidFn = NgxResult (*)(const NgxParams *, const char *, void **);
using GetD3D12Fn = NgxResult (*)(const NgxParams *, const char *, ID3D12Resource **);
using GetIntFn = NgxResult (*)(const NgxParams *, const char *, int *);
using GetUIntFn = NgxResult (*)(const NgxParams *, const char *, unsigned int *);
using GetFloatFn = NgxResult (*)(const NgxParams *, const char *, float *);
using SetVoidFn = void (*)(NgxParams *, const char *, void *);
using SetD3D12Fn = void (*)(NgxParams *, const char *, ID3D12Resource *);
using SetIntFn = void (*)(NgxParams *, const char *, int);
using SetUIntFn = void (*)(NgxParams *, const char *, unsigned int);
using SetFloatFn = void (*)(NgxParams *, const char *, float);

bool GetResource(const NgxParams *params, const char *key, ID3D12Resource **value) {
    auto fn = ParamSlot<GetD3D12Fn>(params, 9);
    return fn != nullptr && fn(params, key, value) == kNgxSuccess;
}

bool GetInt(const NgxParams *params, const char *key, int *value) {
    auto fn = ParamSlot<GetIntFn>(params, 11);
    return fn != nullptr && fn(params, key, value) == kNgxSuccess;
}

bool GetUInt(const NgxParams *params, const char *key, unsigned int *value) {
    auto fn = ParamSlot<GetUIntFn>(params, 12);
    return fn != nullptr && fn(params, key, value) == kNgxSuccess;
}

bool GetFloat(const NgxParams *params, const char *key, float *value) {
    auto fn = ParamSlot<GetFloatFn>(params, 14);
    return fn != nullptr && fn(params, key, value) == kNgxSuccess;
}

void SetVoid(NgxParams *params, const char *key, void *value) {
    auto fn = ParamSlot<SetVoidFn>(params, 0);
    if (fn != nullptr) fn(params, key, value);
}

void SetResource(NgxParams *params, const char *key, ID3D12Resource *value) {
    auto fn = ParamSlot<SetD3D12Fn>(params, 1);
    if (fn != nullptr) fn(params, key, value);
}

void SetInt(NgxParams *params, const char *key, int value) {
    auto fn = ParamSlot<SetIntFn>(params, 3);
    if (fn != nullptr) fn(params, key, value);
}

void SetUInt(NgxParams *params, const char *key, unsigned int value) {
    auto fn = ParamSlot<SetUIntFn>(params, 4);
    if (fn != nullptr) fn(params, key, value);
}

void SetFloat(NgxParams *params, const char *key, float value) {
    auto fn = ParamSlot<SetFloatFn>(params, 6);
    if (fn != nullptr) fn(params, key, value);
}

wchar_t g_log_path[MAX_PATH] = {};
wchar_t g_ini_path[MAX_PATH] = {};
wchar_t g_reshade_ini_path[MAX_PATH] = {};
SRWLOCK g_log_lock = SRWLOCK_INIT;

void SetSiblingPath(HMODULE module, const wchar_t *leaf, wchar_t (&out)[MAX_PATH]) {
    GetModuleFileNameW(module, out, MAX_PATH);
    wchar_t *slash = wcsrchr(out, L'\\');
    if (slash != nullptr) {
        *(slash + 1) = L'\0';
        wcsncat_s(out, MAX_PATH, leaf, _TRUNCATE);
    }
}

void TruncateLog() {
    HANDLE file = CreateFileW(g_log_path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
}

void Log(const char *format, ...) {
    char message[3072] = {};
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    SYSTEMTIME now = {};
    GetLocalTime(&now);
    char line[3328] = {};
    const int count = snprintf(line, sizeof(line),
                               "%02u:%02u:%02u.%03u  %s\r\n",
                               now.wHour, now.wMinute, now.wSecond,
                               now.wMilliseconds, message);
    if (count <= 0) return;

    AcquireSRWLockExclusive(&g_log_lock);
    HANDLE file = CreateFileW(g_log_path, FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(count), &written, nullptr);
        CloseHandle(file);
    }
    ReleaseSRWLockExclusive(&g_log_lock);
}

const char *FeatureName(int feature_id) {
    switch (feature_id) {
    case 1: return "SuperSampling";
    case 11: return "FrameGeneration";
    case 13: return "RayReconstruction";
    case 18: return "NeuralRendering";
    default: return "Other";
    }
}

bool IsUpscalingCarrierFeature(int feature_id) {
    // This add-on is deliberately limited to ordinary DLSS Super Resolution.
    // Ray Reconstruction (feature 13) owns a much larger set of denoising
    // guides and is not a compatible carrier for a second Feature 18 pass.
    return feature_id == 1;
}

const char *ResultName(NgxResult result) {
    switch (result) {
    case 0x00000001u: return "Success";
    case 0xBAD00000u: return "Fail";
    case 0xBAD00001u: return "FeatureNotSupported";
    case 0xBAD00002u: return "PlatformError";
    case 0xBAD00003u: return "FeatureAlreadyExists";
    case 0xBAD00004u: return "FeatureNotFound";
    case 0xBAD00005u: return "InvalidParameter";
    case 0xBAD00007u: return "NotInitialized";
    case 0xBAD00008u: return "UnsupportedInputFormat";
    case 0xBAD0000Au: return "MissingInput";
    case 0xBAD0000Bu: return "UnableToInitializeFeature";
    case 0xBAD0000Du: return "OutOfGPUMemory";
    case 0xBAD0000Eu: return "UnsupportedFormat";
    default: return "Unknown";
    }
}

const char *FormatName(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
    case DXGI_FORMAT_R11G11B10_FLOAT: return "R11G11B10_FLOAT";
    case DXGI_FORMAT_R16G16_FLOAT: return "R16G16_FLOAT";
    case DXGI_FORMAT_R32G32_FLOAT: return "R32G32_FLOAT";
    case DXGI_FORMAT_R32_FLOAT: return "R32_FLOAT";
    case DXGI_FORMAT_R32_TYPELESS: return "R32_TYPELESS";
    case DXGI_FORMAT_D32_FLOAT: return "D32_FLOAT";
    case DXGI_FORMAT_R16_FLOAT: return "R16_FLOAT";
    case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
    case DXGI_FORMAT_R10G10B10A2_UNORM: return "R10G10B10A2_UNORM";
    default: return "other";
    }
}

bool IsSupportedSceneColorFormat(DXGI_FORMAT format) {
    return format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
           format == DXGI_FORMAT_R11G11B10_FLOAT;
}

struct FeatureRecord {
    const NgxHandle *handle = nullptr;
    int feature_id = -1;
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int out_width = 0;
    unsigned int out_height = 0;
    int create_flags = 0;
    int perf_quality = 0;
};

FeatureRecord g_features[64] = {};
SRWLOCK g_feature_lock = SRWLOCK_INIT;
// If ownership bookkeeping ever becomes incomplete, fail closed for Feature 1
// rather than risk submitting our Feature 18 beside an untracked RR feature.
std::atomic<bool> g_feature_tracking_uncertain{false};
// A live Feature 13 handle is a stronger and frame-rate-independent RR signal
// than "an RR evaluation happened within N milliseconds".  The conservative
// policy is intentional: if an engine keeps a dormant RR handle alive we may
// temporarily skip our pass, but we never feed a second denoiser into RR.
std::atomic<unsigned int> g_live_rr_handles{0};
// Close the small window between entering a Feature 13 Create call and
// publishing its returned handle in g_features. Feature 1 must not be modified
// while RR ownership is being established.
std::atomic<unsigned int> g_rr_create_in_progress{0};

void RememberFeature(const NgxHandle *handle, int feature_id, const NgxParams *params) {
    // Only these two features participate in this add-on's state machine.
    // Keeping unrelated NGX handles out of the fixed table also prevents frame
    // generation and auxiliary features from exhausting RR bookkeeping.
    if (handle == nullptr || (feature_id != 1 && feature_id != 13)) return;
    FeatureRecord record = {};
    record.handle = handle;
    record.feature_id = feature_id;
    GetUInt(params, "Width", &record.width);
    GetUInt(params, "Height", &record.height);
    GetUInt(params, "OutWidth", &record.out_width);
    GetUInt(params, "OutHeight", &record.out_height);
    GetInt(params, "DLSS.Feature.Create.Flags", &record.create_flags);
    GetInt(params, "PerfQualityValue", &record.perf_quality);

    AcquireSRWLockExclusive(&g_feature_lock);
    std::size_t slot = 64;
    for (std::size_t i = 0; i < 64; ++i) {
        if (g_features[i].handle == handle) {
            slot = i;
            break;
        }
        if (slot == 64 && g_features[i].handle == nullptr) slot = i;
    }
    if (slot < 64) {
        const int previous_id = g_features[slot].feature_id;
        if (previous_id != 13 && feature_id == 13) {
            g_live_rr_handles.fetch_add(1, std::memory_order_release);
        } else if (previous_id == 13 && feature_id != 13) {
            const unsigned int live =
                g_live_rr_handles.load(std::memory_order_relaxed);
            if (live != 0)
                g_live_rr_handles.fetch_sub(1, std::memory_order_release);
        }
        g_features[slot] = record;
    } else {
        const bool was_uncertain =
            g_feature_tracking_uncertain.exchange(true,
                                                   std::memory_order_release);
        if (!was_uncertain) {
            Log("Feature ownership table exhausted; all Feature 1 processing "
                "will fail closed to native SR for this process.");
        }
    }
    ReleaseSRWLockExclusive(&g_feature_lock);
}

FeatureRecord FindFeature(const NgxHandle *handle) {
    FeatureRecord result = {};
    result.handle = nullptr;
    result.feature_id = -1;
    AcquireSRWLockShared(&g_feature_lock);
    for (const auto &record : g_features) {
        if (record.handle == handle) {
            result = record;
            break;
        }
    }
    ReleaseSRWLockShared(&g_feature_lock);
    return result;
}

void ForgetFeature(const NgxHandle *handle) {
    AcquireSRWLockExclusive(&g_feature_lock);
    for (auto &record : g_features) {
        if (record.handle == handle) {
            if (record.feature_id == 13) {
                const unsigned int live =
                    g_live_rr_handles.load(std::memory_order_relaxed);
                if (live != 0)
                    g_live_rr_handles.fetch_sub(1, std::memory_order_release);
            }
            record = {};
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_feature_lock);
}

void LogUInt(const NgxParams *params, const char *key) {
    unsigned int value = 0;
    if (GetUInt(params, key, &value)) Log("    %-42s = %u", key, value);
}

void LogInt(const NgxParams *params, const char *key) {
    int value = 0;
    if (GetInt(params, key, &value)) Log("    %-42s = %d", key, value);
}

void LogFloat(const NgxParams *params, const char *key) {
    float value = 0.0f;
    if (GetFloat(params, key, &value)) Log("    %-42s = %.8g", key, value);
}

void LogResource(const NgxParams *params, const char *key) {
    ID3D12Resource *resource = nullptr;
    if (!GetResource(params, key, &resource)) return;
    if (resource == nullptr) {
        Log("    %-42s = null", key);
        return;
    }
    const D3D12_RESOURCE_DESC desc = ID3D12Resource_GetDesc(resource);
    Log("    %-42s = %p physical=%llux%u format=%u(%s) mips=%u flags=0x%X sample=%u",
        key, static_cast<void *>(resource),
        static_cast<unsigned long long>(desc.Width), desc.Height,
        static_cast<unsigned int>(desc.Format), FormatName(desc.Format),
        static_cast<unsigned int>(desc.MipLevels),
        static_cast<unsigned int>(desc.Flags), desc.SampleDesc.Count);
}

void DumpCreate(const NgxParams *params) {
    LogUInt(params, "Width");
    LogUInt(params, "Height");
    LogUInt(params, "OutWidth");
    LogUInt(params, "OutHeight");
    LogInt(params, "PerfQualityValue");
    LogInt(params, "DLSS.Feature.Create.Flags");
    LogInt(params, "DLSS.Enable.Output.Subrects");
    LogUInt(params, "CreationNodeMask");
    LogUInt(params, "VisibilityNodeMask");
}

void DumpEvaluate(const NgxParams *params, const FeatureRecord &feature,
                  unsigned long long evaluation) {
    Log("--- %s Evaluate #%llu handle=%p create=%ux%u -> %ux%u ---",
        FeatureName(feature.feature_id), evaluation,
        static_cast<const void *>(feature.handle),
        feature.width, feature.height, feature.out_width, feature.out_height);
    LogResource(params, "Color");
    LogResource(params, "MotionVectors");
    LogResource(params, "Depth");
    LogResource(params, "ExposureTexture");
    LogResource(params, "Output");
    LogUInt(params, "DLSS.Input.Color.Subrect.Base.X");
    LogUInt(params, "DLSS.Input.Color.Subrect.Base.Y");
    LogUInt(params, "DLSS.Input.Depth.Subrect.Base.X");
    LogUInt(params, "DLSS.Input.Depth.Subrect.Base.Y");
    LogUInt(params, "DLSS.Input.MV.Subrect.Base.X");
    LogUInt(params, "DLSS.Input.MV.Subrect.Base.Y");
    LogUInt(params, "DLSS.Output.Subrect.Base.X");
    LogUInt(params, "DLSS.Output.Subrect.Base.Y");
    LogUInt(params, "DLSS.Render.Subrect.Dimensions.Width");
    LogUInt(params, "DLSS.Render.Subrect.Dimensions.Height");
    LogFloat(params, "MV.Scale.X");
    LogFloat(params, "MV.Scale.Y");
    LogFloat(params, "Jitter.Offset.X");
    LogFloat(params, "Jitter.Offset.Y");
    LogFloat(params, "DLSS.Pre.Exposure");
    LogFloat(params, "DLSS.Exposure.Scale");
    LogInt(params, "Reset");
}

// -------------------------------------------------------------------------
// AI upscaling chain: render-resolution NR, then the game's original SR
// -------------------------------------------------------------------------

struct ChainConfig {
    bool enabled = false;
    int mode = 1;
    bool identity_use_original = false;
    float transfer_strength = 0.0f;
    float post_transfer_strength = 1.0f;
    float color_strength = 0.0f;
    float max_luma_ratio = 2.0f;
    bool preserve_lighting = false;
    float proxy_scale = 1.0f;
    unsigned int warmup_after_create = 2;
    bool retry_original_on_sr_failure = true;
    int depth_mode = -1;
    float intensity = 1.0f;
    float local_tone = 1.0f;
    float local_structure = 1.0f;
    float skin_structure = -1.0f;
    bool auto_mask = false;
    unsigned int style = 0;
    bool ui_correction = false;
    int perf_epoch = 0;
};

ChainConfig g_config = {};
ULONGLONG g_config_read_tick = 0;
CRITICAL_SECTION g_chain_cs;
std::atomic<bool> g_enabled_fast{false};
std::atomic<ULONGLONG> g_next_config_poll_tick{0};
std::atomic<unsigned int> g_config_generation{1};
std::atomic<unsigned long long> g_nr_successful_evaluations{0};
std::atomic<unsigned int> g_last_nr_width{0};
std::atomic<unsigned int> g_last_nr_height{0};
std::atomic<unsigned int> g_last_dlss_input_width{0};
std::atomic<unsigned int> g_last_dlss_input_height{0};
std::atomic<unsigned int> g_last_dlss_output_width{0};
std::atomic<unsigned int> g_last_dlss_output_height{0};
std::atomic<unsigned int> g_last_pre_sr_color_format{DXGI_FORMAT_UNKNOWN};
std::atomic<float> g_last_pre_exposure{1.0f};
std::atomic<float> g_last_exposure_scale{1.0f};
std::atomic<float> g_last_source_to_canonical{1.0f};
std::atomic<bool> g_last_exposure_texture_present{false};
std::atomic<int> g_last_carrier_feature{-1};
std::atomic<unsigned long long> g_rr_passthrough_evaluations{0};
std::atomic<unsigned long long> g_rr_guarded_sr_evaluations{0};
std::atomic<bool> g_rr_guard_was_active{false};
std::atomic<bool> g_unverified_exposure_texture_bypass{false};
std::atomic<unsigned long long> g_exposure_texture_bypasses{0};
std::atomic<bool> g_pre_sr_runtime_rejected{false};
std::atomic<bool> g_post_sr_runtime_rejected{false};
std::atomic<unsigned int> g_last_nr_create_result{kNgxSuccess};
std::atomic<bool> g_shortfuse_loaded{false};
std::atomic<bool> g_shortfuse_nr_enabled{false};

float ActiveTransferStrength(const ChainConfig &config) {
    return config.transfer_strength;
}

struct ColorContract {
    float pre_exposure = 1.0f;
    float exposure_scale = 1.0f;
    float source_to_canonical = 1.0f;
    ID3D12Resource *exposure_texture = nullptr;
    bool has_pre_exposure = false;
    bool has_exposure_scale = false;
};

float SanitizeExposure(float value) {
    // NGX helpers define zero as "not supplied" and substitute one. Reject
    // NaN/Inf and unreasonable values too so a bad game parameter cannot turn
    // the HDR residual pass into an FP16 overflow.
    if (!std::isfinite(value) || value <= 0.000001f) return 1.0f;
    return std::clamp(value, 1.0f / 1024.0f, 1024.0f);
}

ColorContract ReadColorContract(const NgxParams *sr_params) {
    ColorContract contract = {};
    float value = 0.0f;
    if (GetFloat(sr_params, "DLSS.Pre.Exposure", &value)) {
        contract.pre_exposure = SanitizeExposure(value);
        contract.has_pre_exposure = value > 0.0f && std::isfinite(value);
    }
    value = 0.0f;
    if (GetFloat(sr_params, "DLSS.Exposure.Scale", &value)) {
        contract.exposure_scale = SanitizeExposure(value);
        contract.has_exposure_scale = value > 0.0f && std::isfinite(value);
    }
    GetResource(sr_params, "ExposureTexture", &contract.exposure_texture);

    // This is the exposure transform required by the NGX/DirectSR color
    // contract: canonical = source * ExposureScale / PreExposure. The neural
    // proxy is built from canonical color and its residual is transformed back
    // by the reciprocal before it is added to the game's HDR SceneColor.
    contract.source_to_canonical = std::clamp(
        contract.exposure_scale / contract.pre_exposure,
        1.0f / 1024.0f, 1024.0f);

    g_last_pre_exposure.store(contract.pre_exposure, std::memory_order_relaxed);
    g_last_exposure_scale.store(contract.exposure_scale,
                                std::memory_order_relaxed);
    g_last_source_to_canonical.store(contract.source_to_canonical,
                                     std::memory_order_relaxed);
    g_last_exposure_texture_present.store(
        contract.exposure_texture != nullptr, std::memory_order_relaxed);
    return contract;
}

bool MustBypassUnverifiedExposureTexture(const ColorContract &contract,
                                         const FeatureRecord &feature,
                                         int mode) {
    // The public SR contract exposes ExposureTexture separately from the two
    // scalar exposure values, but Feature 18 has no public rule describing how
    // they combine.  Until we take a same-command-list GPU snapshot and use it
    // symmetrically in Encode/Combine, applying a non-zero HDR residual would
    // be a colour/exposure guess.  Fail closed instead of silently changing the
    // game's lighting.  Diagnostic identity mode remains safe and is allowed.
    const bool blocked = (feature.create_flags & 1) != 0 &&
        contract.exposure_texture != nullptr &&
        !g_config.identity_use_original &&
        ActiveTransferStrength(g_config) > 0.000001f;
    g_unverified_exposure_texture_bypass.store(blocked,
                                                std::memory_order_relaxed);
    if (blocked) {
        const unsigned long long count =
            g_exposure_texture_bypasses.fetch_add(1,
                                                   std::memory_order_relaxed) + 1;
        if (count <= 3 || (count % 600) == 0) {
            Log("Mode %d bypassed because HDR ExposureTexture is present but "
                "has no verified same-frame Feature 18 contract; original SR "
                "is preserved (count=%llu).", mode, count);
        }
    }
    return blocked;
}

float ReadIniFloat(const wchar_t *key, float fallback) {
    wchar_t fallback_text[64] = {};
    wchar_t value[64] = {};
    swprintf_s(fallback_text, L"%.9g", static_cast<double>(fallback));
    GetPrivateProfileStringW(L"NRBeforeSR", key, fallback_text, value,
                             static_cast<DWORD>(std::size(value)), g_ini_path);
    wchar_t *end = nullptr;
    const float parsed = wcstof(value, &end);
    return end != value ? parsed : fallback;
}

void NormalizeConfig(ChainConfig &config) {
    config.mode = config.mode == 2 ? 2 : 1;
    config.transfer_strength = std::clamp(config.transfer_strength, 0.0f, 1.0f);
    // One visible blend control owns both chains. Keeping the legacy field in
    // sync preserves compatibility with existing INI files without allowing a
    // mode switch to silently select a stale zero-strength value.
    config.post_transfer_strength = config.transfer_strength;
    config.color_strength = std::clamp(config.color_strength, 0.0f, 1.0f);
    config.max_luma_ratio = std::clamp(config.max_luma_ratio, 1.0f, 8.0f);
    config.proxy_scale = std::clamp(config.proxy_scale, 0.25f, 2.0f);
    config.warmup_after_create = std::min(config.warmup_after_create, 60u);
    config.depth_mode = std::clamp(config.depth_mode, -1, 1);
    config.intensity = std::clamp(config.intensity, 0.0f, 2.0f);
    config.local_tone = std::clamp(config.local_tone, 0.0f, 2.0f);
    config.local_structure = std::clamp(config.local_structure, 0.0f, 2.0f);
    config.skin_structure = std::clamp(config.skin_structure, -1.0f, 2.0f);
    config.style = std::min(config.style, 2u);
}

bool SameConfig(const ChainConfig &a, const ChainConfig &b) {
    return a.enabled == b.enabled &&
           a.mode == b.mode &&
           a.identity_use_original == b.identity_use_original &&
           a.transfer_strength == b.transfer_strength &&
           a.post_transfer_strength == b.post_transfer_strength &&
           a.color_strength == b.color_strength &&
           a.max_luma_ratio == b.max_luma_ratio &&
           a.preserve_lighting == b.preserve_lighting &&
           a.proxy_scale == b.proxy_scale &&
           a.warmup_after_create == b.warmup_after_create &&
           a.retry_original_on_sr_failure == b.retry_original_on_sr_failure &&
           a.depth_mode == b.depth_mode &&
           a.intensity == b.intensity &&
           a.local_tone == b.local_tone &&
           a.local_structure == b.local_structure &&
           a.skin_structure == b.skin_structure &&
           a.auto_mask == b.auto_mask &&
           a.style == b.style &&
           a.ui_correction == b.ui_correction &&
           a.perf_epoch == b.perf_epoch;
}

void ReloadConfig(bool force = false) {
    const ULONGLONG now = GetTickCount64();
    if (!force && now - g_config_read_tick < 500) return;
    g_config_read_tick = now;
    g_next_config_poll_tick.store(now + 500, std::memory_order_relaxed);

    ChainConfig next = {};
    next.enabled = GetPrivateProfileIntW(L"NRBeforeSR", L"Enabled", 0, g_ini_path) != 0;
    next.mode = GetPrivateProfileIntW(L"NRBeforeSR", L"Mode", 1, g_ini_path);
    next.identity_use_original = GetPrivateProfileIntW(
        L"NRBeforeSR", L"IdentityUseOriginalColor", 0, g_ini_path) != 0;
    const float legacy_pre_transfer = ReadIniFloat(L"TransferStrength", 1.0f);
    const float legacy_post_transfer = ReadIniFloat(L"PostTransferStrength", 1.0f);
    next.transfer_strength = std::max(legacy_pre_transfer, legacy_post_transfer);
    next.post_transfer_strength = next.transfer_strength;
    next.color_strength = ReadIniFloat(L"ColorStrength", 0.0f);
    next.max_luma_ratio = ReadIniFloat(L"MaxLumaRatio", 2.0f);
    next.preserve_lighting = GetPrivateProfileIntW(
        L"NRBeforeSR", L"PreserveLighting", 0, g_ini_path) != 0;
    next.proxy_scale = ReadIniFloat(L"ProxyScale", 1.0f);
    if (next.proxy_scale < 0.001f) next.proxy_scale = 0.001f;
    int warmup = GetPrivateProfileIntW(L"NRBeforeSR", L"WarmupAfterCreate", 2, g_ini_path);
    next.warmup_after_create = warmup > 0 ? static_cast<unsigned int>(warmup) : 0;
    next.retry_original_on_sr_failure = GetPrivateProfileIntW(
        L"NRBeforeSR", L"RetryOriginalOnSRFailure", 1, g_ini_path) != 0;
    next.depth_mode = GetPrivateProfileIntW(L"NRBeforeSR", L"DepthMode", -1, g_ini_path);
    next.intensity = ReadIniFloat(L"Intensity", 1.0f);
    next.local_tone = ReadIniFloat(L"LocalToneStrength", 1.0f);
    next.local_structure = ReadIniFloat(L"LocalStructureStrength", 1.0f);
    next.skin_structure = ReadIniFloat(L"SkinStructureStrength", -1.0f);
    next.auto_mask = GetPrivateProfileIntW(
        L"NRBeforeSR", L"AutoMask", 0, g_ini_path) != 0;
    next.style = static_cast<unsigned int>(GetPrivateProfileIntW(
        L"NRBeforeSR", L"Style", 0, g_ini_path));
    next.ui_correction = GetPrivateProfileIntW(
        L"NRBeforeSR", L"UICorrection", 0, g_ini_path) != 0;
    next.perf_epoch = GetPrivateProfileIntW(
        L"NRBeforeSR", L"PerfEpoch", 0, g_ini_path);
    NormalizeConfig(next);

    const bool shortfuse_loaded =
        GetModuleHandleW(L"renodx-dlss.addon64") != nullptr;
    const bool shortfuse_enabled_setting =
        GetPrivateProfileIntW(L"RENODX-DLSS",
                              L"DirectNeuralRenderingEnabled", 0,
                              g_reshade_ini_path) != 0;
    const int shortfuse_hook_point =
        GetPrivateProfileIntW(L"RENODX-DLSS",
                              L"DirectNeuralRenderingHookPoint", 0,
                              g_reshade_ini_path);
    // ShortFuse v0.3 may initialize and evaluate its direct NR backend when a
    // non-zero hook point is selected even if the separately persisted master
    // value still reads as zero. Treat either signal as ownership of NR so the
    // two add-ons can never submit feature 18 on the same frame.
    const bool shortfuse_nr_enabled = shortfuse_loaded &&
        (shortfuse_enabled_setting || shortfuse_hook_point != 0);
    const bool previous_shortfuse_loaded =
        g_shortfuse_loaded.exchange(shortfuse_loaded, std::memory_order_relaxed);
    const bool previous_shortfuse_nr_enabled =
        g_shortfuse_nr_enabled.exchange(shortfuse_nr_enabled,
                                        std::memory_order_relaxed);
    if (force || shortfuse_loaded != previous_shortfuse_loaded ||
        shortfuse_nr_enabled != previous_shortfuse_nr_enabled) {
        Log("ShortFuse sidecar: loaded=%d enabledSetting=%d hookPoint=%d "
            "ownsNR=%d; AI upscaling NR %s.",
            shortfuse_loaded ? 1 : 0, shortfuse_enabled_setting ? 1 : 0,
            shortfuse_hook_point, shortfuse_nr_enabled ? 1 : 0,
            shortfuse_nr_enabled ? "bypassed to prevent a double NR pass"
                                  : "available");
    }

    const bool changed = !SameConfig(next, g_config);
    g_config = next;
    g_enabled_fast.store(g_config.enabled, std::memory_order_release);
    if (changed || force) {
        g_config_generation.fetch_add(1, std::memory_order_relaxed);
    }
    if (changed || force) {
        Log("Config: enabled=%d mode=%d identity=%d preTransfer=%.3f "
            "postTransfer=%.3f preserveLighting=%d proxyScale=%.3f "
            "warmup=%u autoMask=%d style=%u perfEpoch=%d",
            g_config.enabled ? 1 : 0, g_config.mode,
            g_config.identity_use_original ? 1 : 0,
            static_cast<double>(g_config.transfer_strength),
            static_cast<double>(g_config.post_transfer_strength),
            g_config.preserve_lighting ? 1 : 0,
            static_cast<double>(g_config.proxy_scale),
            g_config.warmup_after_create, g_config.auto_mask ? 1 : 0,
            g_config.style, g_config.perf_epoch);
    }
}

void WriteIniInt(const wchar_t *key, int value) {
    wchar_t text[32] = {};
    swprintf_s(text, L"%d", value);
    WritePrivateProfileStringW(L"NRBeforeSR", key, text, g_ini_path);
}

void WriteIniFloat(const wchar_t *key, float value) {
    wchar_t text[64] = {};
    swprintf_s(text, L"%.6g", static_cast<double>(value));
    WritePrivateProfileStringW(L"NRBeforeSR", key, text, g_ini_path);
}

void SaveConfig(const ChainConfig &config) {
    WriteIniInt(L"Enabled", config.enabled ? 1 : 0);
    WriteIniInt(L"Mode", config.mode);
    WriteIniInt(L"IdentityUseOriginalColor", config.identity_use_original ? 1 : 0);
    WriteIniFloat(L"TransferStrength", config.transfer_strength);
    WriteIniFloat(L"PostTransferStrength", config.post_transfer_strength);
    WriteIniFloat(L"ColorStrength", config.color_strength);
    WriteIniFloat(L"MaxLumaRatio", config.max_luma_ratio);
    WriteIniInt(L"PreserveLighting", config.preserve_lighting ? 1 : 0);
    WriteIniFloat(L"ProxyScale", config.proxy_scale);
    WriteIniInt(L"WarmupAfterCreate", static_cast<int>(config.warmup_after_create));
    WriteIniInt(L"RetryOriginalOnSRFailure", config.retry_original_on_sr_failure ? 1 : 0);
    WriteIniInt(L"DepthMode", config.depth_mode);
    WriteIniFloat(L"Intensity", config.intensity);
    WriteIniFloat(L"LocalToneStrength", config.local_tone);
    WriteIniFloat(L"LocalStructureStrength", config.local_structure);
    WriteIniFloat(L"SkinStructureStrength", config.skin_structure);
    WriteIniInt(L"AutoMask", config.auto_mask ? 1 : 0);
    WriteIniInt(L"Style", static_cast<int>(config.style));
    WriteIniInt(L"UICorrection", config.ui_correction ? 1 : 0);
    WriteIniInt(L"PerfEpoch", config.perf_epoch);
    // Remove the obsolete option from configs created by older builds.
    WritePrivateProfileStringW(L"NRBeforeSR", L"NRPreset", nullptr, g_ini_path);
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, g_ini_path);
}

ChainConfig SnapshotConfig() {
    EnterCriticalSection(&g_chain_cs);
    ReloadConfig();
    const ChainConfig snapshot = g_config;
    LeaveCriticalSection(&g_chain_cs);
    return snapshot;
}

void PollConfigFromPresent() {
    const ULONGLONG now = GetTickCount64();
    ULONGLONG next = g_next_config_poll_tick.load(std::memory_order_relaxed);
    if (now < next ||
        !g_next_config_poll_tick.compare_exchange_strong(
            next, now + 500, std::memory_order_relaxed)) {
        return;
    }
    EnterCriticalSection(&g_chain_cs);
    ReloadConfig();
    LeaveCriticalSection(&g_chain_cs);
}

bool ApplyConfig(ChainConfig next, const char *source) {
    NormalizeConfig(next);
    EnterCriticalSection(&g_chain_cs);
    if (SameConfig(next, g_config)) {
        LeaveCriticalSection(&g_chain_cs);
        return false;
    }
    next.perf_epoch = g_config.perf_epoch + 1;
    g_config = next;
    g_config_read_tick = GetTickCount64();
    g_next_config_poll_tick.store(g_config_read_tick + 500,
                                  std::memory_order_relaxed);
    g_enabled_fast.store(g_config.enabled, std::memory_order_release);
    g_config_generation.fetch_add(1, std::memory_order_relaxed);
    SaveConfig(g_config);
    const ChainConfig applied = g_config;
    LeaveCriticalSection(&g_chain_cs);
    Log("Config updated from %s: enabled=%d nrBeforeSr=%d transfer=%.3f epoch=%d",
        source, applied.enabled ? 1 : 0, applied.mode == 2 ? 1 : 0,
        static_cast<double>(ActiveTransferStrength(applied)), applied.perf_epoch);
    return true;
}

struct PerfSignature {
    bool enabled = false;
    bool shortfuse = false;
    int mode = 0;
    bool identity = true;
    float transfer = 0.0f;
    int epoch = 0;
};

struct FrameMeter {
    LARGE_INTEGER frequency = {};
    long long last_qpc = 0;
    PerfSignature signature = {};
    bool signature_valid = false;
    bool skip_next = false;
    unsigned int warmup_remaining = 0;
    unsigned int count = 0;
    double sum_ms = 0.0;
    double samples_ms[600] = {};
};

FrameMeter g_frame_meter = {};

bool SamePerfSignature(const PerfSignature &a, const PerfSignature &b) {
    return a.enabled == b.enabled && a.shortfuse == b.shortfuse &&
           a.mode == b.mode &&
           a.identity == b.identity && a.transfer == b.transfer &&
           a.epoch == b.epoch;
}

const char *PerfStageName(const PerfSignature &signature) {
    if (signature.shortfuse) return "D-ShortFuse-NR";
    if (!signature.enabled) return "A-SR-only";
    if (signature.mode == 1) return "B-post-SR-NR";
    if (signature.identity) return "B1-NR-original-Color";
    if (signature.transfer == 0.0f) return "B2-zero-transfer";
    return "C-NR-transfer";
}

void ObserveFrameCadence() {
    if (g_frame_meter.frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&g_frame_meter.frequency);
    }
    LARGE_INTEGER now = {};
    QueryPerformanceCounter(&now);
    const PerfSignature current = {
        g_config.enabled,
        g_shortfuse_nr_enabled.load(std::memory_order_relaxed),
        g_config.mode, g_config.identity_use_original,
        ActiveTransferStrength(g_config), g_config.perf_epoch};
    if (!g_frame_meter.signature_valid ||
        !SamePerfSignature(current, g_frame_meter.signature)) {
        g_frame_meter.signature = current;
        g_frame_meter.signature_valid = true;
        g_frame_meter.last_qpc = now.QuadPart;
        g_frame_meter.skip_next = false;
        g_frame_meter.warmup_remaining = 180;
        g_frame_meter.count = 0;
        g_frame_meter.sum_ms = 0.0;
        Log("[perf] reset stage=%s epoch=%d transfer=%.3f; warming 180 frames",
            PerfStageName(current), current.epoch,
            static_cast<double>(current.transfer));
        return;
    }

    const long long elapsed_qpc = now.QuadPart - g_frame_meter.last_qpc;
    g_frame_meter.last_qpc = now.QuadPart;
    if (g_frame_meter.skip_next) {
        g_frame_meter.skip_next = false;
        return;
    }
    if (g_frame_meter.warmup_remaining != 0) {
        --g_frame_meter.warmup_remaining;
        return;
    }
    if (elapsed_qpc <= 0 || g_frame_meter.frequency.QuadPart <= 0) return;
    const double frame_ms = static_cast<double>(elapsed_qpc) * 1000.0 /
                            static_cast<double>(g_frame_meter.frequency.QuadPart);
    if (frame_ms < 0.05 || frame_ms > 500.0) return;

    g_frame_meter.samples_ms[g_frame_meter.count++] = frame_ms;
    g_frame_meter.sum_ms += frame_ms;
    if (g_frame_meter.count != 600) return;

    std::sort(std::begin(g_frame_meter.samples_ms),
              std::end(g_frame_meter.samples_ms));
    const double average_ms = g_frame_meter.sum_ms / 600.0;
    const double average_fps = 1000.0 / average_ms;
    const double p95_ms = g_frame_meter.samples_ms[569];
    const double p99_ms = g_frame_meter.samples_ms[593];
    double worst_one_percent_ms = 0.0;
    for (unsigned int i = 594; i < 600; ++i)
        worst_one_percent_ms += g_frame_meter.samples_ms[i];
    worst_one_percent_ms /= 6.0;
    const double one_percent_low_fps = 1000.0 / worst_one_percent_ms;
    Log("[perf] stage=%s epoch=%d transfer=%.3f samples=600 avgFPS=%.3f "
        "avgMs=%.4f p95Ms=%.4f p99Ms=%.4f onePercentLowFPS=%.3f",
        PerfStageName(current), current.epoch,
        static_cast<double>(current.transfer), average_fps, average_ms,
        p95_ms, p99_ms, one_percent_low_fps);
    g_frame_meter.count = 0;
    g_frame_meter.sum_ms = 0.0;
    g_frame_meter.skip_next = true;
}

template <typename T>
void ReleaseCom(T *&object) {
    if (object != nullptr) {
        object->lpVtbl->Release(object);
        object = nullptr;
    }
}

struct NrApi {
    HMODULE bridge_module = nullptr;
    HMODULE signed_module = nullptr;
    void *signed_init = nullptr;
    void *signed_create = nullptr;
    void *signed_evaluate = nullptr;
    void *signed_release = nullptr;
    using AllocateFn = NgxResult (*)(NgxParams **);
    using DestroyFn = NgxResult (*)(NgxParams *);
    AllocateFn allocate = nullptr;
    DestroyFn destroy = nullptr;
    using BridgeInitFn = NgxResult (*)(void *, unsigned long long, const wchar_t *,
                                       ID3D12Device *, std::uint32_t, const void *);
    using BridgeCreateFn = NgxResult (*)(void *, ID3D12GraphicsCommandList *, int,
                                         void *, void **);
    using BridgeEvaluateFn = NgxResult (*)(void *, ID3D12GraphicsCommandList *,
                                           const void *, const void *, void *);
    using BridgeReleaseFn = NgxResult (*)(void *, void *);
    BridgeInitFn bridge_init = nullptr;
    BridgeCreateFn bridge_create = nullptr;
    BridgeEvaluateFn bridge_evaluate = nullptr;
    BridgeReleaseFn bridge_release = nullptr;
    bool initialized = false;
    bool failed = false;
};

NrApi g_nr_api = {};

void AddonDirectory(wchar_t (&directory)[MAX_PATH]) {
    wcsncpy_s(directory, g_log_path, _TRUNCATE);
    wchar_t *slash = wcsrchr(directory, L'\\');
    if (slash != nullptr) *(slash + 1) = L'\0';
}

bool EnsureNrApi(ID3D12Device *device) {
    if (g_nr_api.initialized) return true;
    if (g_nr_api.failed || device == nullptr) return false;

    wchar_t directory[MAX_PATH] = {};
    wchar_t bridge_path[MAX_PATH] = {};
    wchar_t signed_path[MAX_PATH] = {};
    AddonDirectory(directory);
    swprintf_s(bridge_path, L"%lsnrchain_nvngx.dll", directory);
    swprintf_s(signed_path, L"%lsnvngx_dlssnr.dll", directory);

    g_nr_api.bridge_module = LoadLibraryExW(
        bridge_path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    g_nr_api.signed_module = LoadLibraryExW(
        signed_path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE core = GetModuleHandleW(L"_nvngx.dll");
    if (g_nr_api.bridge_module == nullptr || g_nr_api.signed_module == nullptr || core == nullptr) {
        Log("NR runtime load failed: bridge=%p signed=%p core=%p error=%lu",
            static_cast<void *>(g_nr_api.bridge_module),
            static_cast<void *>(g_nr_api.signed_module), static_cast<void *>(core),
            GetLastError());
        g_nr_api.failed = true;
        return false;
    }

    g_nr_api.signed_init = reinterpret_cast<void *>(GetProcAddress(
        g_nr_api.signed_module, "NVSDK_NGX_D3D12_Init_Ext"));
    g_nr_api.signed_create = reinterpret_cast<void *>(GetProcAddress(
        g_nr_api.signed_module, "NVSDK_NGX_D3D12_CreateFeature"));
    g_nr_api.signed_evaluate = reinterpret_cast<void *>(GetProcAddress(
        g_nr_api.signed_module, "NVSDK_NGX_D3D12_EvaluateFeature"));
    g_nr_api.signed_release = reinterpret_cast<void *>(GetProcAddress(
        g_nr_api.signed_module, "NVSDK_NGX_D3D12_ReleaseFeature"));
    g_nr_api.allocate = reinterpret_cast<NrApi::AllocateFn>(GetProcAddress(
        core, "NVSDK_NGX_D3D12_AllocateParameters"));
    g_nr_api.destroy = reinterpret_cast<NrApi::DestroyFn>(GetProcAddress(
        core, "NVSDK_NGX_D3D12_DestroyParameters"));
    g_nr_api.bridge_init = reinterpret_cast<NrApi::BridgeInitFn>(GetProcAddress(
        g_nr_api.bridge_module, "NRChain_Init"));
    g_nr_api.bridge_create = reinterpret_cast<NrApi::BridgeCreateFn>(GetProcAddress(
        g_nr_api.bridge_module, "NRChain_Create"));
    g_nr_api.bridge_evaluate = reinterpret_cast<NrApi::BridgeEvaluateFn>(GetProcAddress(
        g_nr_api.bridge_module, "NRChain_Evaluate"));
    g_nr_api.bridge_release = reinterpret_cast<NrApi::BridgeReleaseFn>(GetProcAddress(
        g_nr_api.bridge_module, "NRChain_Release"));

    if (g_nr_api.signed_init == nullptr || g_nr_api.signed_create == nullptr ||
        g_nr_api.signed_evaluate == nullptr || g_nr_api.signed_release == nullptr ||
        g_nr_api.allocate == nullptr || g_nr_api.destroy == nullptr ||
        g_nr_api.bridge_init == nullptr || g_nr_api.bridge_create == nullptr ||
        g_nr_api.bridge_evaluate == nullptr || g_nr_api.bridge_release == nullptr) {
        Log("NR runtime exports incomplete; signed feature 18 disabled.");
        g_nr_api.failed = true;
        return false;
    }

    constexpr unsigned long long kApplicationId = 0x000000000E658703ull;
    constexpr std::uint32_t kNgxSdkVersion = 19u;
    const NgxResult result = g_nr_api.bridge_init(
        g_nr_api.signed_init, kApplicationId, directory, device,
        kNgxSdkVersion, nullptr);
    if (result != kNgxSuccess && result != 0xBAD00003u) {
        Log("signed DLSSNR Init_Ext failed: 0x%08X (%s)", result, ResultName(result));
        g_nr_api.failed = true;
        return false;
    }

    g_nr_api.initialized = true;
    Log("signed DLSSNR D3D12 runtime initialized through nrchain_nvngx.dll "
        "(result=0x%08X).", result);
    return true;
}

struct Codec {
    ID3D12Device *device = nullptr;
    ID3D12RootSignature *root = nullptr;
    ID3D12PipelineState *encode = nullptr;
    ID3D12PipelineState *combine = nullptr;
    ID3D12PipelineState *copy = nullptr;
    ID3D12PipelineState *motion_resample = nullptr;
    UINT descriptor_size = 0;
    bool failed = false;
};

Codec g_codec = {};

const char kCodecShader[] = R"HLSL(
// The HDR proxy keeps a linear mid-range and applies a luminance-preserving
// shoulder only to highlights. Composition is a conservative source-domain
// delta graft: the game HDR image remains the base and only the model's change
// relative to its own proxy input is transferred. The AP1 gamut constraint is
// adapted from RenoDX's DLSS 5 Neural Rendering addon by clshortfuse (MIT).
// See THIRD_PARTY_NOTICES.txt.
Texture2D<float4> OriginalColor : register(t0);
Texture2D<float4> ProxyColor    : register(t1);
Texture2D<float4> NeuralColor   : register(t2);
Texture2D<float2> MotionSource  : register(t3);
RWTexture2D<float4> ProxyOut    : register(u0);
RWTexture2D<float4> ResultOut   : register(u1);
RWTexture2D<float2> MotionOut   : register(u2);

cbuffer Constants : register(b0) {
    uint Width;
    uint Height;
    float ProxyScale;
    float TransferStrength;
    float SourceToCanonical;
    float ColorStrength;
    float MaxLumaRatio;
    uint Passthrough;
    uint SourceBaseX;
    uint SourceBaseY;
    uint DestinationBaseX;
    uint DestinationBaseY;
    uint MotionSourceWidth;
    uint MotionSourceHeight;
    uint MotionSourceBaseX;
    uint MotionSourceBaseY;
};

static const float3 kLuma = float3(0.2126, 0.7152, 0.0722);

float3 LinearToSRGB(float3 x) {
    x = saturate(x);
    return lerp(12.92 * x, 1.055 * pow(x, 1.0 / 2.4) - 0.055,
                step(0.0031308.xxx, x));
}

float3 SRGBToLinear(float3 x) {
    x = saturate(x);
    return lerp(x / 12.92, pow((x + 0.055) / 1.055, 2.4),
                step(0.04045.xxx, x));
}

float3 ClampAP1(float3 color) {
    const float3x3 bt709ToAp1 = {
        0.613097, 0.339523, 0.047379,
        0.070194, 0.916354, 0.013452,
        0.020616, 0.109570, 0.869815};
    const float3x3 ap1ToBt709 = {
        1.705051, -0.621792, -0.083259,
        -0.130256, 1.140805, -0.010548,
        -0.024003, -0.128969, 1.152972};
    return mul(ap1ToBt709, max(mul(bt709ToAp1, color), 0.0.xxx));
}

[numthreads(8, 8, 1)]
void EncodeCS(uint3 tid : SV_DispatchThreadID) {
    if (tid.x >= Width || tid.y >= Height) return;
    uint2 sourceCoord = tid.xy + uint2(SourceBaseX, SourceBaseY);
    float4 source = OriginalColor.Load(int3(sourceCoord, 0));
    float3 frame = max(source.rgb, 0.0.xxx);
    if ((Passthrough & 1u) != 0u) {
        ProxyOut[tid.xy] = float4(frame, source.a);
        return;
    }

    float colorScale = max(SourceToCanonical, 0.0009765625);
    float3 display = frame * (max(ProxyScale, 0.001) * colorScale);
    float displayLuma = dot(display, kLuma);
    if (displayLuma > 0.75) {
        float rolled = 0.75 + 0.25 *
            (1.0 - exp(-(displayLuma - 0.75) / 0.25));
        display *= rolled / max(displayLuma, 0.000001);
    }
    // The luminance shoulder can still leave a highly saturated channel above
    // one. LinearToSRGB would clamp each channel independently and rotate its
    // hue. Scale all channels together so the peak reaches one instead.
    float displayPeak = max(display.r, max(display.g, display.b));
    display *= rcp(max(displayPeak, 1.0));
    ProxyOut[tid.xy] = float4(LinearToSRGB(display), source.a);
}

[numthreads(8, 8, 1)]
void CombineCS(uint3 tid : SV_DispatchThreadID) {
    if (tid.x >= Width || tid.y >= Height) return;
    uint2 sourceCoord = tid.xy + uint2(SourceBaseX, SourceBaseY);
    uint2 destinationCoord = tid.xy +
        uint2(DestinationBaseX, DestinationBaseY);
    float4 original = OriginalColor.Load(int3(sourceCoord, 0));
    if (TransferStrength <= 0.000001) {
        ResultOut[destinationCoord] = original;
        return;
    }

    float3 proxy = ProxyColor.Load(int3(tid.xy, 0)).rgb;
    float3 neural = NeuralColor.Load(int3(tid.xy, 0)).rgb;
    bool finiteInputs = all(original.rgb == original.rgb) &&
        all(proxy == proxy) && all(neural == neural) &&
        all(abs(original.rgb) <= 65504.0.xxx) &&
        all(abs(proxy) <= 65504.0.xxx) &&
        all(abs(neural) <= 65504.0.xxx);
    if (!finiteInputs) {
        // A malformed temporal sample must not poison the game's HDR target.
        ResultOut[destinationCoord] = original;
        return;
    }
    if ((Passthrough & 1u) == 0u) {
        proxy = SRGBToLinear(proxy);
        neural = SRGBToLinear(neural);
    }

    float3 modelDelta = neural - proxy;
    if (max(abs(modelDelta.r), max(abs(modelDelta.g), abs(modelDelta.b))) <=
        0.000001) {
        // A no-op neural result must be a strict per-pixel identity. Avoiding
        // any colour-space round-trip here also makes the diagnostic contract
        // independent of FP16 matrix precision.
        ResultOut[destinationCoord] = original;
        return;
    }

    // Render-resolution NR can introduce a broad luminance bias that the
    // following SR pass then magnifies. When requested, estimate that slow
    // component from a sparse 9-tap neighbourhood and remove it from the
    // neural result. Fine structure remains in the neural-minus-proxy delta,
    // while the game's own HDR illumination remains the low-frequency base.
    if ((Passthrough & 2u) != 0u) {
        float proxyMean = 0.0;
        float neuralMean = 0.0;
        int2 center = int2(tid.xy);
        int2 upper = int2(Width - 1u, Height - 1u);
        [unroll]
        for (int oy = -1; oy <= 1; ++oy) {
            [unroll]
            for (int ox = -1; ox <= 1; ++ox) {
                int2 sampleCoord = clamp(
                    center + int2(ox, oy) * 4, int2(0, 0), upper);
                float3 proxySample =
                    ProxyColor.Load(int3(sampleCoord, 0)).rgb;
                float3 neuralSample =
                    NeuralColor.Load(int3(sampleCoord, 0)).rgb;
                if ((Passthrough & 1u) == 0u) {
                    proxySample = SRGBToLinear(proxySample);
                    neuralSample = SRGBToLinear(neuralSample);
                }
                proxyMean += dot(proxySample, kLuma);
                neuralMean += dot(neuralSample, kLuma);
            }
        }
        if (proxyMean > 0.0001 && neuralMean > 0.0001) {
            neural *= clamp(proxyMean / neuralMean, 0.5, 2.0);
            modelDelta = neural - proxy;
        }
    }

    float normalizer = (Passthrough & 1u) != 0u
        ? 1.0
        : max(ProxyScale * SourceToCanonical, 0.000001);
    float3 source = original.rgb * normalizer;
    float sourceLuma = dot(source, kLuma);
    float proxyLuma = dot(proxy, kLuma);
    float neuralLuma = dot(neural, kLuma);
    float maxRatio = max(MaxLumaRatio, 1.0);
    float targetLuma = max(
        sourceLuma + (neuralLuma - proxyLuma) * TransferStrength, 0.0);
    float lumaRatio = sourceLuma > 0.000001
        ? clamp(targetLuma / sourceLuma, 1.0 / maxRatio, maxRatio)
        : 1.0;
    float3 lumaOnly = source * lumaRatio;

    // Full colour following is optional. Even at one, transfer only the NR
    // delta; never replace the game's base illumination with the model proxy.
    float3 fullColour = ClampAP1(max(
        source + modelDelta * TransferStrength, 0.0.xxx));
    float3 result = lerp(lumaOnly, fullColour, saturate(ColorStrength));
    float resultLuma = dot(result, kLuma);
    if (sourceLuma > 0.000001 && resultLuma > 0.000001) {
        float boundedLuma = clamp(
            resultLuma, sourceLuma / maxRatio, sourceLuma * maxRatio);
        result *= boundedLuma / resultLuma;
    }
    result = clamp(result / normalizer, -65504.0.xxx, 65504.0.xxx);
    ResultOut[destinationCoord] = float4(result, original.a);
}

[numthreads(8, 8, 1)]
void CopyCS(uint3 tid : SV_DispatchThreadID) {
    if (tid.x >= Width || tid.y >= Height) return;
    uint2 sourceCoord = tid.xy + uint2(SourceBaseX, SourceBaseY);
    uint2 destinationCoord = tid.xy +
        uint2(DestinationBaseX, DestinationBaseY);
    ResultOut[destinationCoord] = OriginalColor.Load(int3(sourceCoord, 0));
}

[numthreads(8, 8, 1)]
void ResampleMotionCS(uint3 tid : SV_DispatchThreadID) {
    if (tid.x >= Width || tid.y >= Height ||
        MotionSourceWidth == 0 || MotionSourceHeight == 0) return;
    // Motion vectors contain discontinuities at object boundaries, so select
    // the nearest source pixel instead of blending unrelated surfaces.
    // Map destination pixel centres to source pixel centres.  The previous
    // expression added half the *destination* width, which is only correct at
    // 1:1 and introduced an approximately half-source-pixel bias at 3x scales.
    uint2 sourceCoord = uint2(
        min(((tid.x * 2u + 1u) * MotionSourceWidth) / (Width * 2u),
            MotionSourceWidth - 1),
        min(((tid.y * 2u + 1u) * MotionSourceHeight) / (Height * 2u),
            MotionSourceHeight - 1));
    sourceCoord += uint2(MotionSourceBaseX, MotionSourceBaseY);
    MotionOut[tid.xy] = MotionSource.Load(int3(sourceCoord, 0));
}
)HLSL";

ID3DBlob *CompileCodecShader(const char *entry) {
    ID3DBlob *shader = nullptr;
    ID3DBlob *errors = nullptr;
    const HRESULT hr = D3DCompile(
        kCodecShader, sizeof(kCodecShader) - 1, "NRBeforeSR.Codec", nullptr,
        nullptr, entry, "cs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
        &shader, &errors);
    if (FAILED(hr)) {
        const char *message = errors != nullptr
            ? static_cast<const char *>(errors->lpVtbl->GetBufferPointer(errors))
            : "no compiler message";
        Log("D3DCompile %s failed: 0x%08X %s", entry,
            static_cast<unsigned int>(hr), message);
    }
    ReleaseCom(errors);
    return SUCCEEDED(hr) ? shader : nullptr;
}

bool EnsureCodec(ID3D12Device *device) {
    if (g_codec.device == device && g_codec.root != nullptr) return true;
    if (g_codec.failed || device == nullptr || g_codec.device != nullptr) return false;

    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 4;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].RegisterSpace = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 3;
    ranges[1].BaseShaderRegister = 0;
    ranges[1].RegisterSpace = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = 4;

    D3D12_ROOT_PARAMETER parameters[2] = {};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges = 2;
    parameters[0].DescriptorTable.pDescriptorRanges = ranges;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants.ShaderRegister = 0;
    parameters[1].Constants.RegisterSpace = 0;
    parameters[1].Constants.Num32BitValues = 16;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC root_desc = {};
    root_desc.NumParameters = 2;
    root_desc.pParameters = parameters;
    root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    ID3DBlob *serialized = nullptr;
    ID3DBlob *errors = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(
        &root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
    if (FAILED(hr)) {
        const char *message = errors != nullptr
            ? static_cast<const char *>(errors->lpVtbl->GetBufferPointer(errors))
            : "no serializer message";
        Log("D3D12SerializeRootSignature failed: 0x%08X %s",
            static_cast<unsigned int>(hr), message);
        ReleaseCom(errors);
        ReleaseCom(serialized);
        g_codec.failed = true;
        return false;
    }
    ReleaseCom(errors);

    hr = ID3D12Device_CreateRootSignature(
        device, 0, serialized->lpVtbl->GetBufferPointer(serialized),
        serialized->lpVtbl->GetBufferSize(serialized), IID_ID3D12RootSignature,
        reinterpret_cast<void **>(&g_codec.root));
    ReleaseCom(serialized);
    if (FAILED(hr)) {
        Log("CreateRootSignature failed: 0x%08X", static_cast<unsigned int>(hr));
        g_codec.failed = true;
        return false;
    }

    ID3DBlob *encode = CompileCodecShader("EncodeCS");
    ID3DBlob *combine = CompileCodecShader("CombineCS");
    ID3DBlob *copy = CompileCodecShader("CopyCS");
    ID3DBlob *motion_resample = CompileCodecShader("ResampleMotionCS");
    if (encode == nullptr || combine == nullptr || copy == nullptr ||
        motion_resample == nullptr) {
        ReleaseCom(encode);
        ReleaseCom(combine);
        ReleaseCom(copy);
        ReleaseCom(motion_resample);
        g_codec.failed = true;
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline = {};
    pipeline.pRootSignature = g_codec.root;
    pipeline.CS.pShaderBytecode = encode->lpVtbl->GetBufferPointer(encode);
    pipeline.CS.BytecodeLength = encode->lpVtbl->GetBufferSize(encode);
    hr = ID3D12Device_CreateComputePipelineState(
        device, &pipeline, IID_ID3D12PipelineState,
        reinterpret_cast<void **>(&g_codec.encode));
    if (SUCCEEDED(hr)) {
        pipeline.CS.pShaderBytecode = combine->lpVtbl->GetBufferPointer(combine);
        pipeline.CS.BytecodeLength = combine->lpVtbl->GetBufferSize(combine);
        hr = ID3D12Device_CreateComputePipelineState(
            device, &pipeline, IID_ID3D12PipelineState,
            reinterpret_cast<void **>(&g_codec.combine));
    }
    if (SUCCEEDED(hr)) {
        pipeline.CS.pShaderBytecode = copy->lpVtbl->GetBufferPointer(copy);
        pipeline.CS.BytecodeLength = copy->lpVtbl->GetBufferSize(copy);
        hr = ID3D12Device_CreateComputePipelineState(
            device, &pipeline, IID_ID3D12PipelineState,
            reinterpret_cast<void **>(&g_codec.copy));
    }
    if (SUCCEEDED(hr)) {
        pipeline.CS.pShaderBytecode =
            motion_resample->lpVtbl->GetBufferPointer(motion_resample);
        pipeline.CS.BytecodeLength =
            motion_resample->lpVtbl->GetBufferSize(motion_resample);
        hr = ID3D12Device_CreateComputePipelineState(
            device, &pipeline, IID_ID3D12PipelineState,
            reinterpret_cast<void **>(&g_codec.motion_resample));
    }
    ReleaseCom(encode);
    ReleaseCom(combine);
    ReleaseCom(copy);
    ReleaseCom(motion_resample);
    if (FAILED(hr)) {
        Log("CreateComputePipelineState failed: 0x%08X", static_cast<unsigned int>(hr));
        g_codec.failed = true;
        return false;
    }

    ID3D12Device_AddRef(device);
    g_codec.device = device;
    g_codec.descriptor_size = ID3D12Device_GetDescriptorHandleIncrementSize(
        device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    Log("HDR proxy/source-domain delta composition codec created.");
    return true;
}

struct LifecycleQueue;

struct NrSlot {
    ID3D12Resource *key = nullptr;
    ID3D12Resource *source = nullptr;
    ID3D12Resource *proxy = nullptr;
    ID3D12Resource *neural = nullptr;
    ID3D12Resource *result = nullptr;
    ID3D12Resource *motion_source = nullptr;
    ID3D12Resource *motion_proxy = nullptr;
    ID3D12DescriptorHeap *heap = nullptr;
    DXGI_FORMAT source_format = DXGI_FORMAT_UNKNOWN;
    bool converts_source_format = false;
    bool owns_source = false;
    bool owns_result = true;
    D3D12_RESOURCE_STATES source_state = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES proxy_state = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES neural_state = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES result_state = D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES motion_proxy_state = D3D12_RESOURCE_STATE_COMMON;
    unsigned long long last_used_serial = 0;
    unsigned int recording_command_lists = 0;
    LifecycleQueue *last_submission_queue = nullptr;
    unsigned long long completion_fence_value = 0;
    bool awaiting_fence_signal = false;
};

struct NrContext {
    const NgxHandle *sr_handle = nullptr;
    NgxHandle *nr_handle = nullptr;
    NgxParams *params = nullptr;
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int evaluations = 0;
    unsigned int config_generation = 0;
    unsigned long long slot_serial = 0;
    unsigned int slot_recycles = 0;
    float last_source_to_canonical = 1.0f;
    bool color_contract_initialized = false;
    bool last_has_pre_exposure = false;
    bool last_has_exposure_scale = false;
    bool last_has_exposure_texture = false;
    unsigned int color_contract_resets = 0;
    bool create_contract_logged = false;
    bool guide_contract_logged = false;
    bool motion_conversion_logged = false;
    bool motion_scale_fallback_logged = false;
    unsigned int create_failures = 0;
    unsigned int frames_seen = 0;
    unsigned int next_create_retry_frame = 0;
    int bound_create_flags = 0;
    int bound_perf_quality = 5;
    bool create_variant_selected = false;
    bool retired = false;
    int mode = 0;
    bool failed = false;
    NrSlot slots[8] = {};
};

NrContext g_contexts[16] = {};

// GPU lifetime tracking is deliberately fixed-size and fail-closed.  The
// command-list marker survives the ReShade proxy and can therefore be read
// from the native command list after the real ExecuteCommandLists call.
constexpr GUID kLifecycleCommandListGuid = {
    0x8c113f2d, 0xe73b, 0x48dd,
    {0x99, 0x21, 0x9f, 0x0b, 0x47, 0xa4, 0xd5, 0x6c}};
constexpr std::size_t kLifecycleCommandListCount = 128;
constexpr std::size_t kLifecycleUsesPerCommandList = 16;
constexpr std::size_t kLifecycleQueueCount = 8;

struct LifecycleCommandList {
    unsigned long long token = 0;
    NrSlot *slots[kLifecycleUsesPerCommandList] = {};
    unsigned int slot_count = 0;
    unsigned int pending_execute_callbacks = 0;
};

struct LifecycleQueue {
    ID3D12CommandQueue *queue = nullptr;
    ID3D12Fence *fence = nullptr;
    unsigned long long next_fence_value = 1;
};

LifecycleCommandList g_lifecycle_command_lists[kLifecycleCommandListCount] = {};
LifecycleQueue g_lifecycle_queues[kLifecycleQueueCount] = {};
std::atomic<unsigned long long> g_lifecycle_token{1};
std::atomic<bool> g_lifecycle_tracking_uncertain{false};
// Avoid installing or traversing the native queue hook in a pure-SR session.
// This count is raised only after an NR slot is attached to a command list and
// remains non-zero until that list is reset or destroyed.
std::atomic<unsigned int> g_lifecycle_active_command_lists{0};

void MarkLifecycleUncertain(const char *reason) {
    if (!g_lifecycle_tracking_uncertain.exchange(true,
                                                  std::memory_order_acq_rel)) {
        Log("GPU lifecycle tracking became uncertain (%s). Feature 18 is now "
            "fail-closed; existing GPU objects are quarantined until process exit.",
            reason != nullptr ? reason : "unknown reason");
    }
}

void DestroySlot(NrSlot &slot) {
    ReleaseCom(slot.heap);
    ReleaseCom(slot.result);
    ReleaseCom(slot.neural);
    ReleaseCom(slot.proxy);
    ReleaseCom(slot.motion_source);
    ReleaseCom(slot.motion_proxy);
    ReleaseCom(slot.source);
    slot = {};
}

void DestroyContext(NrContext &context) {
    if (context.nr_handle != nullptr && g_nr_api.initialized &&
        g_nr_api.bridge_release != nullptr) {
        const NgxResult result = g_nr_api.bridge_release(
            g_nr_api.signed_release, context.nr_handle);
        Log("NR ReleaseFeature handle=%p result=0x%08X", context.nr_handle, result);
    }
    context.nr_handle = nullptr;
    if (context.params != nullptr && g_nr_api.destroy != nullptr) {
        g_nr_api.destroy(context.params);
        context.params = nullptr;
    }
    for (auto &slot : context.slots) DestroySlot(slot);
    context = {};
}

bool IsSlotFenceCompleteLocked(const NrSlot &slot) {
    if (slot.completion_fence_value == 0) return true;
    if (slot.last_submission_queue == nullptr ||
        slot.last_submission_queue->fence == nullptr) return false;
    const unsigned long long completed = ID3D12Fence_GetCompletedValue(
        slot.last_submission_queue->fence);
    // D3D12 returns UINT64_MAX when the device was removed.  Treat that as an
    // unknown lifetime rather than permission to release objects.
    return completed != UINT64_MAX && completed >= slot.completion_fence_value;
}

bool CanMutateSlotLocked(const NrSlot &slot) {
    return !g_lifecycle_tracking_uncertain.load(std::memory_order_acquire) &&
           slot.recording_command_lists == 0 &&
           !slot.awaiting_fence_signal && IsSlotFenceCompleteLocked(slot);
}

bool CanDestroyContextLocked(const NrContext &context) {
    if (g_lifecycle_tracking_uncertain.load(std::memory_order_acquire))
        return false;
    for (const auto &slot : context.slots) {
        if (slot.key != nullptr && !CanMutateSlotLocked(slot)) return false;
    }
    return true;
}

void CollectRetiredContextsLocked() {
    if (g_lifecycle_tracking_uncertain.load(std::memory_order_acquire)) return;
    for (auto &context : g_contexts) {
        if (context.sr_handle != nullptr && context.retired &&
            CanDestroyContextLocked(context)) {
            Log("GPU retirement fence completed for SR handle=%p mode=%d "
                "extent=%ux%u; releasing Feature 18 context.",
                static_cast<const void *>(context.sr_handle), context.mode,
                context.width, context.height);
            DestroyContext(context);
        }
    }
}

void RetireContextForHandle(const NgxHandle *sr_handle) {
    for (auto &context : g_contexts) {
        if (context.sr_handle == sr_handle && !context.retired) {
            context.retired = true;
            Log("Deferred Feature 18 retirement for SR handle=%p mode=%d "
                "extent=%ux%u until recorded command lists and GPU fences complete.",
                static_cast<const void *>(sr_handle), context.mode,
                context.width, context.height);
        }
    }
}

LifecycleCommandList *FindLifecycleCommandListLocked(
    unsigned long long token) {
    if (token == 0) return nullptr;
    for (auto &record : g_lifecycle_command_lists) {
        if (record.token == token) return &record;
    }
    return nullptr;
}

bool ReadLifecycleToken(ID3D12GraphicsCommandList *command_list,
                        unsigned long long &token) {
    if (command_list == nullptr) return false;
    UINT size = sizeof(token);
    token = 0;
    return SUCCEEDED(ID3D12GraphicsCommandList_GetPrivateData(
        command_list, kLifecycleCommandListGuid, &size, &token)) &&
        size == sizeof(token) && token != 0;
}

void ClearLifecycleToken(ID3D12GraphicsCommandList *command_list) {
    if (command_list != nullptr) {
        ID3D12GraphicsCommandList_SetPrivateData(
            command_list, kLifecycleCommandListGuid, 0, nullptr);
    }
}

bool RecordSlotUseLocked(ID3D12GraphicsCommandList *command_list,
                         NrSlot &slot) {
    if (g_lifecycle_tracking_uncertain.load(std::memory_order_acquire))
        return false;

    unsigned long long token = 0;
    LifecycleCommandList *record = nullptr;
    if (ReadLifecycleToken(command_list, token)) {
        record = FindLifecycleCommandListLocked(token);
        if (record == nullptr) {
            MarkLifecycleUncertain("stale command-list lifetime marker");
            return false;
        }
    } else {
        for (auto &candidate : g_lifecycle_command_lists) {
            if (candidate.token == 0) {
                record = &candidate;
                break;
            }
        }
        if (record == nullptr) {
            MarkLifecycleUncertain("command-list lifetime table exhausted");
            return false;
        }
        token = g_lifecycle_token.fetch_add(1, std::memory_order_relaxed);
        if (token == 0)
            token = g_lifecycle_token.fetch_add(1, std::memory_order_relaxed);
        const HRESULT marked = ID3D12GraphicsCommandList_SetPrivateData(
            command_list, kLifecycleCommandListGuid, sizeof(token), &token);
        if (FAILED(marked)) {
            MarkLifecycleUncertain("could not mark D3D12 command list");
            return false;
        }
        record->token = token;
        g_lifecycle_active_command_lists.fetch_add(1,
                                                   std::memory_order_release);
    }

    for (unsigned int i = 0; i < record->slot_count; ++i) {
        if (record->slots[i] == &slot) return true;
    }
    if (record->slot_count >= kLifecycleUsesPerCommandList) {
        MarkLifecycleUncertain("too many NR resources on one command list");
        return false;
    }
    record->slots[record->slot_count++] = &slot;
    ++slot.recording_command_lists;
    return true;
}

void DiscardLifecycleCommandListLocked(
    ID3D12GraphicsCommandList *command_list) {
    unsigned long long token = 0;
    if (!ReadLifecycleToken(command_list, token)) return;
    LifecycleCommandList *record = FindLifecycleCommandListLocked(token);
    if (record == nullptr) {
        MarkLifecycleUncertain("unknown command-list marker during reset");
        return;
    }
    if (record->pending_execute_callbacks != 0) {
        MarkLifecycleUncertain("command list reset while submission was in progress");
        return;
    }
    for (unsigned int i = 0; i < record->slot_count; ++i) {
        NrSlot *slot = record->slots[i];
        if (slot == nullptr || slot->recording_command_lists == 0) {
            MarkLifecycleUncertain("command-list lifetime reference underflow");
            continue;
        }
        --slot->recording_command_lists;
    }
    ClearLifecycleToken(command_list);
    *record = {};
    const unsigned int active =
        g_lifecycle_active_command_lists.load(std::memory_order_relaxed);
    if (active == 0) {
        MarkLifecycleUncertain("active command-list lifetime count underflow");
    } else {
        g_lifecycle_active_command_lists.fetch_sub(1,
                                                   std::memory_order_release);
    }
}

LifecycleQueue *FindOrCreateLifecycleQueueLocked(
    ID3D12CommandQueue *queue) {
    LifecycleQueue *empty = nullptr;
    for (auto &candidate : g_lifecycle_queues) {
        if (candidate.queue == queue) return &candidate;
        if (empty == nullptr && candidate.queue == nullptr) empty = &candidate;
    }
    if (empty == nullptr) {
        MarkLifecycleUncertain("command-queue fence table exhausted");
        return nullptr;
    }

    ID3D12Device *device = nullptr;
    const HRESULT got_device = ID3D12CommandQueue_GetDevice(
        queue, IID_ID3D12Device, reinterpret_cast<void **>(&device));
    if (FAILED(got_device) || device == nullptr) {
        MarkLifecycleUncertain("could not get D3D12 device from submission queue");
        return nullptr;
    }
    ID3D12Fence *fence = nullptr;
    const HRESULT created = ID3D12Device_CreateFence(
        device, 0, D3D12_FENCE_FLAG_NONE, IID_ID3D12Fence,
        reinterpret_cast<void **>(&fence));
    ReleaseCom(device);
    if (FAILED(created) || fence == nullptr) {
        MarkLifecycleUncertain("could not create D3D12 retirement fence");
        return nullptr;
    }
    ID3D12CommandQueue_AddRef(queue);
    empty->queue = queue;
    empty->fence = fence;
    empty->next_fence_value = 1;
    Log("Created GPU retirement fence for D3D12 queue=%p.",
        static_cast<void *>(queue));
    return empty;
}

void MarkLifecycleCommandListSubmittedLocked(
    ID3D12CommandQueue *queue, ID3D12GraphicsCommandList *command_list) {
    unsigned long long token = 0;
    if (!ReadLifecycleToken(command_list, token)) return;
    LifecycleCommandList *record = FindLifecycleCommandListLocked(token);
    if (record == nullptr) {
        MarkLifecycleUncertain("unknown command-list marker after submission");
        return;
    }
    // A direct native submission may bypass ReShade's pre-submit event while
    // still reaching this post-submit hook.  The post-call observation is the
    // actual ordering proof, so the pre-event count is only used to catch a
    // concurrent Reset race when it is available.
    if (record->pending_execute_callbacks != 0)
        --record->pending_execute_callbacks;
    LifecycleQueue *queue_state = FindOrCreateLifecycleQueueLocked(queue);
    if (queue_state == nullptr) return;

    for (unsigned int i = 0; i < record->slot_count; ++i) {
        NrSlot *slot = record->slots[i];
        if (slot == nullptr) continue;
        if (slot->last_submission_queue != nullptr &&
            slot->last_submission_queue != queue_state &&
            (!IsSlotFenceCompleteLocked(*slot) ||
             slot->awaiting_fence_signal)) {
            MarkLifecycleUncertain(
                "an NR descriptor set crossed queues before completion");
            continue;
        }
        slot->last_submission_queue = queue_state;
        slot->awaiting_fence_signal = true;
    }
}

void SignalLifecycleQueueAndCollectLocked(ID3D12CommandQueue *queue) {
    LifecycleQueue *queue_state = nullptr;
    for (auto &candidate : g_lifecycle_queues) {
        if (candidate.queue == queue) {
            queue_state = &candidate;
            break;
        }
    }
    if (queue_state != nullptr &&
        !g_lifecycle_tracking_uncertain.load(std::memory_order_acquire)) {
        bool has_pending = false;
        for (const auto &context : g_contexts) {
            for (const auto &slot : context.slots) {
                if (slot.last_submission_queue == queue_state &&
                    slot.awaiting_fence_signal) {
                    has_pending = true;
                    break;
                }
            }
            if (has_pending) break;
        }
        if (has_pending) {
            const unsigned long long value = queue_state->next_fence_value++;
            const HRESULT signaled = ID3D12CommandQueue_Signal(
                queue_state->queue, queue_state->fence, value);
            if (FAILED(signaled)) {
                MarkLifecycleUncertain("D3D12 retirement fence signal failed");
            } else {
                for (auto &context : g_contexts) {
                    for (auto &slot : context.slots) {
                        if (slot.last_submission_queue == queue_state &&
                            slot.awaiting_fence_signal) {
                            slot.completion_fence_value = value;
                            slot.awaiting_fence_signal = false;
                        }
                    }
                }
            }
        }
    }
    CollectRetiredContextsLocked();
}

NrContext *FindOrCreateContext(const NgxHandle *sr_handle, int mode,
                               unsigned int width, unsigned int height) {
    CollectRetiredContextsLocked();
    if (g_lifecycle_tracking_uncertain.load(std::memory_order_acquire))
        return nullptr;
    NrContext *empty = nullptr;
    for (auto &context : g_contexts) {
        if (context.sr_handle == sr_handle && context.retired) return nullptr;
        if (context.sr_handle == sr_handle && context.mode == mode &&
            context.width == width && context.height == height) {
            return &context;
        }
        if (empty == nullptr && context.sr_handle == nullptr) empty = &context;
    }
    if (empty != nullptr) {
        empty->sr_handle = sr_handle;
        empty->mode = mode;
        empty->width = width;
        empty->height = height;
    }
    return empty;
}

bool CreateTexture(ID3D12Device *device, unsigned int width, unsigned int height,
                   const wchar_t *name, ID3D12Resource **out,
                   DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT) {
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    const HRESULT hr = ID3D12Device_CreateCommittedResource(
        device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_ID3D12Resource,
        reinterpret_cast<void **>(out));
    if (FAILED(hr)) {
        Log("CreateCommittedResource %ls %ux%u failed: 0x%08X", name,
            width, height, static_cast<unsigned int>(hr));
        return false;
    }
    ID3D12Resource_SetName(*out, name);
    return true;
}

bool CreateSlot(ID3D12Device *device, ID3D12Resource *external,
                unsigned int width, unsigned int height, bool post_sr,
                NrSlot &slot) {
    slot.key = external;
    slot.owns_source = post_sr;
    slot.owns_result = !post_sr;
    if (post_sr) {
        ID3D12Resource_AddRef(external);
        slot.result = external;
        // This is the game's DLSS Output resource at the Evaluate boundary.
        // The NGX D3D12 contract presents it as UAV; pretending its prior state
        // was COMMON would make our first explicit barrier's StateBefore false.
        slot.result_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        if (!CreateTexture(device, width, height, L"NRChain.SRIntermediate",
                           &slot.source)) {
            DestroySlot(slot);
            return false;
        }
    } else {
        ID3D12Resource_AddRef(external);
        slot.source = external;
        // This is the game's DLSS Color input. It is already prepared for
        // non-pixel shader reads (or may promote from COMMON implicitly), so do
        // not emit a fabricated COMMON -> SRV barrier against an external
        // resource whose exact previous state we do not own.
        slot.source_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }
    const D3D12_RESOURCE_DESC source_desc =
        ID3D12Resource_GetDesc(slot.source);
    slot.source_format = source_desc.Format;
    slot.converts_source_format =
        slot.source_format == DXGI_FORMAT_R11G11B10_FLOAT;
    if (!IsSupportedSceneColorFormat(slot.source_format)) {
        Log("CreateSlot rejected unsupported source format=%u (%s).",
            static_cast<unsigned int>(slot.source_format),
            FormatName(slot.source_format));
        DestroySlot(slot);
        return false;
    }
    if (!CreateTexture(device, width, height, L"NRChain.Proxy", &slot.proxy) ||
        !CreateTexture(device, width, height, L"NRChain.NeuralOutput", &slot.neural) ||
        (!post_sr && !CreateTexture(device, width, height, L"NRChain.Result",
                                    &slot.result))) {
        DestroySlot(slot);
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap_desc.NumDescriptors = 7;
    heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HRESULT hr = ID3D12Device_CreateDescriptorHeap(
        device, &heap_desc, IID_ID3D12DescriptorHeap,
        reinterpret_cast<void **>(&slot.heap));
    if (FAILED(hr)) {
        Log("CreateDescriptorHeap failed: 0x%08X", static_cast<unsigned int>(hr));
        DestroySlot(slot);
        return false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = slot.source_format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

    D3D12_CPU_DESCRIPTOR_HANDLE cpu =
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(slot.heap);
    ID3D12Device_CreateShaderResourceView(device, slot.source, &srv, cpu);
    srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    cpu.ptr += g_codec.descriptor_size;
    ID3D12Device_CreateShaderResourceView(device, slot.proxy, &srv, cpu);
    cpu.ptr += g_codec.descriptor_size;
    ID3D12Device_CreateShaderResourceView(device, slot.neural, &srv, cpu);
    cpu.ptr += g_codec.descriptor_size;
    D3D12_SHADER_RESOURCE_VIEW_DESC motion_srv = {};
    motion_srv.Format = DXGI_FORMAT_R16G16_FLOAT;
    motion_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    motion_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    motion_srv.Texture2D.MipLevels = 1;
    ID3D12Device_CreateShaderResourceView(device, nullptr, &motion_srv, cpu);
    cpu.ptr += g_codec.descriptor_size;
    ID3D12Device_CreateUnorderedAccessView(device, slot.proxy, nullptr, &uav, cpu);
    cpu.ptr += g_codec.descriptor_size;
    ID3D12Device_CreateUnorderedAccessView(device, slot.result, nullptr, &uav, cpu);
    cpu.ptr += g_codec.descriptor_size;
    D3D12_UNORDERED_ACCESS_VIEW_DESC motion_uav = {};
    motion_uav.Format = DXGI_FORMAT_R16G16_FLOAT;
    motion_uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    ID3D12Device_CreateUnorderedAccessView(device, nullptr, nullptr,
                                           &motion_uav, cpu);
    if (slot.converts_source_format) {
        Log("SceneColor format conversion enabled: %s -> "
            "R16G16B16A16_FLOAT (typed SRV + FP16 codec pass).",
            FormatName(slot.source_format));
    }
    return true;
}

bool EnsureMotionProxy(ID3D12Device *device, NrSlot &slot,
                       ID3D12Resource *motion, unsigned int width,
                       unsigned int height) {
    if (device == nullptr || slot.heap == nullptr || motion == nullptr ||
        width == 0 || height == 0) return false;
    const D3D12_RESOURCE_DESC motion_desc = ID3D12Resource_GetDesc(motion);
    if ((motion_desc.Format != DXGI_FORMAT_R16G16_FLOAT &&
         motion_desc.Format != DXGI_FORMAT_R32G32_FLOAT) ||
        (motion_desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) != 0) {
        Log("High-resolution motion conversion rejected format=%u (%s) "
            "flags=0x%X; original SR retained.",
            static_cast<unsigned int>(motion_desc.Format),
            FormatName(motion_desc.Format),
            static_cast<unsigned int>(motion_desc.Flags));
        return false;
    }
    const bool mutates_descriptors =
        slot.motion_proxy == nullptr || slot.motion_source != motion;
    if (mutates_descriptors && !CanMutateSlotLocked(slot)) {
        Log("Motion descriptor change deferred because its previous GPU use "
            "has not completed; original SR retained.");
        return false;
    }
    if (slot.motion_proxy == nullptr) {
        if (!CreateTexture(device, width, height, L"NRChain.MotionProxy",
                           &slot.motion_proxy, DXGI_FORMAT_R16G16_FLOAT))
            return false;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu =
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(slot.heap);
        cpu.ptr += static_cast<SIZE_T>(g_codec.descriptor_size) * 6;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = DXGI_FORMAT_R16G16_FLOAT;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        ID3D12Device_CreateUnorderedAccessView(
            device, slot.motion_proxy, nullptr, &uav, cpu);
    }
    if (slot.motion_source != motion) {
        ID3D12Resource_AddRef(motion);
        ReleaseCom(slot.motion_source);
        slot.motion_source = motion;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu =
            ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(slot.heap);
        cpu.ptr += static_cast<SIZE_T>(g_codec.descriptor_size) * 3;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = motion_desc.Format;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        ID3D12Device_CreateShaderResourceView(device, motion, &srv, cpu);
    }
    return true;
}

bool RebindSlotExternal(ID3D12Device *device, ID3D12Resource *external,
                        bool post_sr, NrSlot &slot) {
    if (device == nullptr || external == nullptr || slot.heap == nullptr)
        return false;
    if (!CanMutateSlotLocked(slot)) return false;

    const D3D12_RESOURCE_DESC external_desc =
        ID3D12Resource_GetDesc(external);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu =
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(slot.heap);

    if (post_sr) {
        if (!slot.owns_source || slot.owns_result ||
            external_desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT ||
            (external_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0)
            return false;

        ID3D12Resource_AddRef(external);
        ReleaseCom(slot.result);
        slot.key = external;
        slot.result = external;
        slot.result_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        cpu.ptr += static_cast<SIZE_T>(g_codec.descriptor_size) * 5;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        ID3D12Device_CreateUnorderedAccessView(
            device, slot.result, nullptr, &uav, cpu);
        slot.last_submission_queue = nullptr;
        slot.completion_fence_value = 0;
        slot.awaiting_fence_signal = false;
        return true;
    }

    if (slot.owns_source || !slot.owns_result ||
        !IsSupportedSceneColorFormat(external_desc.Format))
        return false;

    ID3D12Resource_AddRef(external);
    ReleaseCom(slot.source);
    slot.key = external;
    slot.source = external;
    slot.source_format = external_desc.Format;
    slot.converts_source_format =
        slot.source_format == DXGI_FORMAT_R11G11B10_FLOAT;
    slot.source_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = slot.source_format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    ID3D12Device_CreateShaderResourceView(device, slot.source, &srv, cpu);
    slot.last_submission_queue = nullptr;
    slot.completion_fence_value = 0;
    slot.awaiting_fence_signal = false;
    return true;
}

NrSlot *FindOrCreateSlot(NrContext &context, ID3D12Device *device,
                         ID3D12Resource *external, bool post_sr = false) {
    const unsigned long long serial = ++context.slot_serial;
    NrSlot *empty = nullptr;
    for (auto &slot : context.slots) {
        if (slot.key == external) {
            slot.last_used_serial = serial;
            return &slot;
        }
        if (empty == nullptr && slot.key == nullptr) empty = &slot;
    }
    if (empty != nullptr) {
        if (!CreateSlot(device, external, context.width, context.height,
                        post_sr, *empty))
            return nullptr;
        empty->last_used_serial = serial;
        return empty;
    }

    // UE titles may hand NGX new transient resource objects indefinitely.
    // Descriptor contents are only changed after the command list that used
    // them was reset/destroyed and the actual submission queue fence completed.
    NrSlot *oldest = nullptr;
    for (auto &slot : context.slots) {
        if (CanMutateSlotLocked(slot) &&
            (oldest == nullptr ||
             slot.last_used_serial < oldest->last_used_serial)) {
            oldest = &slot;
        }
    }
    const unsigned long long age = oldest != nullptr
        ? serial - oldest->last_used_serial : 0;
    if (oldest == nullptr ||
        !RebindSlotExternal(device, external, post_sr, *oldest)) {
        static unsigned int rejected_reuses = 0;
        ++rejected_reuses;
        if (rejected_reuses <= 3 || (rejected_reuses % 600) == 0) {
            Log("Rotating chain resource is still referenced by a recorded or "
                "in-flight command list: age=%llu mode=%d; original SR retained.",
                age, context.mode);
        }
        return nullptr;
    }
    oldest->last_used_serial = serial;
    ++context.slot_recycles;
    if (context.slot_recycles <= 3 || (context.slot_recycles % 600) == 0) {
        Log("Recycled rotating chain slot: count=%u age=%llu mode=%d extent=%ux%u.",
            context.slot_recycles, age, context.mode,
            context.width, context.height);
    }
    return oldest;
}

void Transition(ID3D12GraphicsCommandList *command_list, ID3D12Resource *resource,
                D3D12_RESOURCE_STATES &state, D3D12_RESOURCE_STATES target) {
    if (state == target) return;
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = state;
    barrier.Transition.StateAfter = target;
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barrier);
    state = target;
}

void UavBarrier(ID3D12GraphicsCommandList *command_list, ID3D12Resource *resource) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    ID3D12GraphicsCommandList_ResourceBarrier(command_list, 1, &barrier);
}

struct CodecConstants {
    unsigned int width;
    unsigned int height;
    float proxy_scale;
    float transfer_strength;
    float source_to_canonical;
    float color_strength;
    float max_luma_ratio;
    unsigned int passthrough;
    unsigned int source_base_x;
    unsigned int source_base_y;
    unsigned int destination_base_x;
    unsigned int destination_base_y;
    unsigned int motion_source_width;
    unsigned int motion_source_height;
    unsigned int motion_source_base_x;
    unsigned int motion_source_base_y;
};

void DispatchCodec(ID3D12GraphicsCommandList *command_list, NrSlot &slot,
                   ID3D12PipelineState *pipeline, unsigned int width,
                   unsigned int height, const ColorContract &color_contract,
                   bool hdr_input, unsigned int source_base_x = 0,
                   unsigned int source_base_y = 0,
                   unsigned int destination_base_x = 0,
                   unsigned int destination_base_y = 0) {
    ID3D12DescriptorHeap *heaps[] = {slot.heap};
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, heaps);
    ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, g_codec.root);
    const D3D12_GPU_DESCRIPTOR_HANDLE gpu =
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(slot.heap);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list, 0, gpu);
    const unsigned int codec_flags = (hdr_input ? 0u : 1u) |
        ((g_config.mode == 2 && g_config.preserve_lighting) ? 2u : 0u);
    const CodecConstants constants = {
        width, height, g_config.proxy_scale, ActiveTransferStrength(g_config),
        color_contract.source_to_canonical, g_config.color_strength,
        g_config.max_luma_ratio, codec_flags,
        source_base_x, source_base_y,
        destination_base_x, destination_base_y,
        0, 0, 0, 0};
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(
        command_list, 1, 16, &constants, 0);
    ID3D12GraphicsCommandList_SetPipelineState(command_list, pipeline);
    ID3D12GraphicsCommandList_Dispatch(
        command_list, (width + 7) / 8, (height + 7) / 8, 1);
}

void DispatchMotionResample(ID3D12GraphicsCommandList *command_list,
                            NrSlot &slot, unsigned int width,
                            unsigned int height,
                            unsigned int source_width,
                            unsigned int source_height,
                            unsigned int source_base_x,
                            unsigned int source_base_y) {
    ID3D12DescriptorHeap *heaps[] = {slot.heap};
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, 1, heaps);
    ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, g_codec.root);
    const D3D12_GPU_DESCRIPTOR_HANDLE gpu =
        ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(slot.heap);
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list, 0, gpu);
    const CodecConstants constants = {
        width, height, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0,
        0, 0, 0, 0,
        source_width, source_height, source_base_x, source_base_y};
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(
        command_list, 1, 16, &constants, 0);
    ID3D12GraphicsCommandList_SetPipelineState(
        command_list, g_codec.motion_resample);
    ID3D12GraphicsCommandList_Dispatch(
        command_list, (width + 7) / 8, (height + 7) / 8, 1);
}

bool TextureContainsRegion(ID3D12Resource *resource, unsigned int base_x,
                           unsigned int base_y, unsigned int width,
                           unsigned int height) {
    if (resource == nullptr || width == 0 || height == 0) return false;
    const D3D12_RESOURCE_DESC desc = ID3D12Resource_GetDesc(resource);
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        desc.SampleDesc.Count != 1) return false;
    const std::uint64_t right = static_cast<std::uint64_t>(base_x) + width;
    const std::uint64_t bottom = static_cast<std::uint64_t>(base_y) + height;
    return right <= desc.Width && bottom <= desc.Height;
}

bool ResolveSrOutputExtent(const FeatureRecord &feature,
                           unsigned int render_width,
                           unsigned int render_height,
                           unsigned int available_width,
                           unsigned int available_height,
                           unsigned int &output_width,
                           unsigned int &output_height) {
    output_width = 0;
    output_height = 0;
    const auto fits = [&](unsigned int width, unsigned int height) {
        return width != 0 && height != 0 &&
               width <= available_width && height <= available_height;
    };
    const bool first_is_render = feature.width == render_width &&
                                 feature.height == render_height;
    const bool second_is_render = feature.out_width == render_width &&
                                  feature.out_height == render_height;
    if (first_is_render && !second_is_render &&
        fits(feature.out_width, feature.out_height)) {
        output_width = feature.out_width;
        output_height = feature.out_height;
        return true;
    }
    if (second_is_render && !first_is_render &&
        fits(feature.width, feature.height)) {
        output_width = feature.width;
        output_height = feature.height;
        return true;
    }
    if (first_is_render && second_is_render &&
        fits(render_width, render_height)) {
        output_width = render_width;
        output_height = render_height;
        return true;
    }

    const bool first_is_physical = feature.width == available_width &&
                                   feature.height == available_height;
    const bool second_is_physical = feature.out_width == available_width &&
                                    feature.out_height == available_height;
    if (first_is_physical != second_is_physical) {
        output_width = first_is_physical ? feature.width : feature.out_width;
        output_height = first_is_physical ? feature.height : feature.out_height;
        return fits(output_width, output_height);
    }
    if (first_is_physical && second_is_physical) {
        output_width = available_width;
        output_height = available_height;
        return true;
    }
    return false;
}

NgxResult ComputeScalingRatioCallback(NgxParams *params) {
    if (params == nullptr) return 0xBAD00005u;
    int upscaling = 0;
    unsigned int input_width = 0;
    unsigned int output_width = 0;
    GetInt(params, "DLSSNR.Upscaling", &upscaling);
    GetUInt(params, "DLSSNR.InputWidth", &input_width);
    GetUInt(params, "DLSSNR.OutputWidth", &output_width);
    float ratio = 1.0f;
    if (upscaling != 0 && input_width != 0 && output_width != 0)
        ratio = static_cast<float>(input_width) / static_cast<float>(output_width);
    SetFloat(params, "DLSSNR.ScalingRatio", ratio);
    return kNgxSuccess;
}

bool BindNrParameters(NrContext &context, NrSlot &slot,
                      const NgxParams *sr_params, const FeatureRecord &feature,
                      unsigned int width, unsigned int height,
                      unsigned int guide_width, unsigned int guide_height,
                      const ColorContract &color_contract, bool reset,
                      ID3D12Resource *motion_override = nullptr,
                      unsigned int motion_override_width = 0,
                      unsigned int motion_override_height = 0,
                      float motion_scale_multiplier_x = 1.0f,
                      float motion_scale_multiplier_y = 1.0f) {
    ID3D12Resource *motion = nullptr;
    ID3D12Resource *depth = nullptr;
    if (!GetResource(sr_params, "MotionVectors", &motion) || motion == nullptr ||
        !GetResource(sr_params, "Depth", &depth) || depth == nullptr) return false;

    unsigned int depth_x = 0, depth_y = 0, mv_x = 0, mv_y = 0;
    GetUInt(sr_params, "DLSS.Input.Depth.Subrect.Base.X", &depth_x);
    GetUInt(sr_params, "DLSS.Input.Depth.Subrect.Base.Y", &depth_y);
    GetUInt(sr_params, "DLSS.Input.MV.Subrect.Base.X", &mv_x);
    GetUInt(sr_params, "DLSS.Input.MV.Subrect.Base.Y", &mv_y);
    if (motion_override != nullptr) {
        motion = motion_override;
        mv_x = 0;
        mv_y = 0;
    }
    if (guide_width == 0) guide_width = width;
    if (guide_height == 0) guide_height = height;
    unsigned int motion_width = guide_width;
    unsigned int motion_height = guide_height;
    if (motion_override != nullptr) {
        motion_width = motion_override_width;
        motion_height = motion_override_height;
    } else if ((feature.create_flags & 2) == 0) {
        const D3D12_RESOURCE_DESC motion_desc = ID3D12Resource_GetDesc(motion);
        const unsigned int available_motion_width = motion_desc.Width > mv_x
            ? static_cast<unsigned int>(motion_desc.Width - mv_x) : 0;
        const unsigned int available_motion_height = motion_desc.Height > mv_y
            ? motion_desc.Height - mv_y : 0;
        // With MVLowRes clear, NGX expects motion at the feature output
        // resolution. This is the native contract for Wuchang's post-SR path.
        if (available_motion_width >= width &&
            available_motion_height >= height) {
            motion_width = width;
            motion_height = height;
        }
    }
    if (motion_width == 0 || motion_height == 0) return false;
    if (!TextureContainsRegion(depth, depth_x, depth_y,
                               guide_width, guide_height) ||
        !TextureContainsRegion(motion, mv_x, mv_y,
                               motion_width, motion_height)) {
        const D3D12_RESOURCE_DESC depth_desc = ID3D12Resource_GetDesc(depth);
        const D3D12_RESOURCE_DESC motion_desc = ID3D12Resource_GetDesc(motion);
        Log("NR guide contract rejected: depthActive=%ux%u "
            "depth physical=%llux%u base=%u,%u; motionActive=%ux%u "
            "motion physical=%llux%u base=%u,%u. "
            "Original game SR retained.",
            guide_width, guide_height,
            static_cast<unsigned long long>(depth_desc.Width), depth_desc.Height,
            depth_x, depth_y,
            motion_width, motion_height,
            static_cast<unsigned long long>(motion_desc.Width), motion_desc.Height,
            mv_x, mv_y);
        return false;
    }
    if (!context.guide_contract_logged) {
        context.guide_contract_logged = true;
        const D3D12_RESOURCE_DESC depth_desc = ID3D12Resource_GetDesc(depth);
        const D3D12_RESOURCE_DESC motion_desc = ID3D12Resource_GetDesc(motion);
        Log("NR guide contract accepted: depthActive=%ux%u "
            "depth=%llux%u@%u,%u motionActive=%ux%u "
            "motion=%llux%u@%u,%u%s.",
            guide_width, guide_height,
            static_cast<unsigned long long>(depth_desc.Width), depth_desc.Height,
            depth_x, depth_y,
            motion_width, motion_height,
            static_cast<unsigned long long>(motion_desc.Width), motion_desc.Height,
            mv_x, mv_y, motion_override != nullptr ? " converted" : "");
    }
    // NGX defines MV.Scale as the conversion from the resource's stored
    // vectors to pixel displacement. A missing scale is 1, not the guide
    // extent: using width/height here turned an omitted optional parameter
    // into a hundreds- or thousands-fold temporal offset.
    float mv_scale_x = 1.0f;
    float mv_scale_y = 1.0f;
    const bool has_mv_scale_x = GetFloat(sr_params, "MV.Scale.X", &mv_scale_x);
    const bool has_mv_scale_y = GetFloat(sr_params, "MV.Scale.Y", &mv_scale_y);
    const bool valid_mv_scale_x = has_mv_scale_x && std::isfinite(mv_scale_x) &&
                                  std::abs(mv_scale_x) > 0.000001f;
    const bool valid_mv_scale_y = has_mv_scale_y && std::isfinite(mv_scale_y) &&
                                  std::abs(mv_scale_y) > 0.000001f;
    if (!valid_mv_scale_x) mv_scale_x = 1.0f;
    if (!valid_mv_scale_y) mv_scale_y = 1.0f;
    if ((!valid_mv_scale_x || !valid_mv_scale_y) &&
        !context.motion_scale_fallback_logged) {
        context.motion_scale_fallback_logged = true;
        Log("MV.Scale missing, zero, or non-finite; using safe 1:1 default "
            "(present=%d,%d).", has_mv_scale_x ? 1 : 0,
            has_mv_scale_y ? 1 : 0);
    }
    mv_scale_x *= motion_scale_multiplier_x;
    mv_scale_y *= motion_scale_multiplier_y;
    int game_reset = 0;
    GetInt(sr_params, "Reset", &game_reset);

    NgxParams *params = context.params;
    SetResource(params, "DLSSNR.Color", slot.proxy);
    SetResource(params, "DLSSNR.Output", slot.neural);
    SetResource(params, "DLSSNR.MVec", motion);
    SetResource(params, "DLSSNR.Depth", depth);
    SetUInt(params, "Width", width);
    SetUInt(params, "Height", height);
    SetUInt(params, "OutWidth", width);
    SetUInt(params, "OutHeight", height);
    SetUInt(params, "DLSSNR.Width", width);
    SetUInt(params, "DLSSNR.Height", height);
    SetUInt(params, "DLSSNR.InputWidth", width);
    SetUInt(params, "DLSSNR.InputHeight", height);
    SetUInt(params, "DLSSNR.OutputWidth", width);
    SetUInt(params, "DLSSNR.OutputHeight", height);
    SetUInt(params, "DLSSNR.Output.Width", width);
    SetUInt(params, "DLSSNR.Output.Height", height);

    SetUInt(params, "DLSSNR.ColorSubrectBaseX", 0);
    SetUInt(params, "DLSSNR.ColorSubrectBaseY", 0);
    SetUInt(params, "DLSSNR.ColorSubrectWidth", width);
    SetUInt(params, "DLSSNR.ColorSubrectHeight", height);
    SetUInt(params, "DLSSNR.DepthSubrectBaseX", depth_x);
    SetUInt(params, "DLSSNR.DepthSubrectBaseY", depth_y);
    SetUInt(params, "DLSSNR.DepthSubrectWidth", guide_width);
    SetUInt(params, "DLSSNR.DepthSubrectHeight", guide_height);
    SetUInt(params, "DLSSNR.MVecSubrectBaseX", mv_x);
    SetUInt(params, "DLSSNR.MVecSubrectBaseY", mv_y);
    SetUInt(params, "DLSSNR.MVecSubrectWidth", motion_width);
    SetUInt(params, "DLSSNR.MVecSubrectHeight", motion_height);
    SetUInt(params, "DLSSNR.OutputSubrectBaseX", 0);
    SetUInt(params, "DLSSNR.OutputSubrectBaseY", 0);
    SetUInt(params, "DLSSNR.OutputSubrectWidth", width);
    SetUInt(params, "DLSSNR.OutputSubrectHeight", height);

    SetFloat(params, "DLSSNR.ScalingRatio", 1.0f);
    SetFloat(params, "DLSSNR.Scale", 1.0f);
    SetInt(params, "DLSSNR.Upscaling", 0);
    int nr_perf_quality = 5;
    int nr_create_flags = feature.create_flags;
    // Bit 1 is the NGX "low-resolution motion vectors" flag. Normalize it to
    // this Feature 18 output, rather than blindly forwarding the game's SR
    // relationship. Depth and motion may legitimately have different extents.
    if (motion_width == width && motion_height == height)
        nr_create_flags &= ~2;
    else
        nr_create_flags |= 2;
    if (context.create_variant_selected) {
        nr_create_flags = context.bound_create_flags;
        nr_perf_quality = context.bound_perf_quality;
    } else {
        context.bound_create_flags = nr_create_flags;
        context.bound_perf_quality = nr_perf_quality;
    }
    SetInt(params, "PerfQualityValue", nr_perf_quality);
    SetInt(params, "DLSS.Feature.Create.Flags", nr_create_flags);
    if (!context.create_contract_logged) {
        context.create_contract_logged = true;
        Log("NR create contract: mode=%d extent=%ux%u depth=%ux%u mv=%ux%u "
            "gameFlags=%d normalizedFlags=%d perfQuality=5.",
            context.mode, width, height, guide_width, guide_height,
            motion_width, motion_height,
            feature.create_flags, nr_create_flags);
    }
    SetUInt(params, "CreationNodeMask", 1);
    SetUInt(params, "VisibilityNodeMask", 1);
    SetVoid(params, "DLSSNRComputeScalingRatioCallback",
            reinterpret_cast<void *>(&ComputeScalingRatioCallback));
    SetFloat(params, "DLSSNR.MVecScaleX", mv_scale_x);
    SetFloat(params, "DLSSNR.MVecScaleY", mv_scale_y);

    int depth_inverted = (feature.create_flags & 8) != 0 ? 1 : 0;
    if (g_config.depth_mode == 0) depth_inverted = 0;
    else if (g_config.depth_mode == 1) depth_inverted = 1;
    const unsigned int config_generation =
        g_config_generation.load(std::memory_order_relaxed);
    const bool config_changed = context.config_generation != config_generation;
    context.config_generation = config_generation;

    bool color_contract_reset = false;
    const bool has_exposure_texture = color_contract.exposure_texture != nullptr;
    if (!context.color_contract_initialized) {
        context.color_contract_initialized = true;
        Log("HDR exposure contract adapted: source PreExposure=%.6f "
            "ExposureScale=%.6f sourceToCanonical=%.6f texture=%d; "
            "the NR proxy uses canonical exposure 1.0.",
            static_cast<double>(color_contract.pre_exposure),
            static_cast<double>(color_contract.exposure_scale),
            static_cast<double>(color_contract.source_to_canonical),
            has_exposure_texture ? 1 : 0);
    } else {
        const float previous = std::max(context.last_source_to_canonical,
                                        1.0f / 1024.0f);
        const float ratio = color_contract.source_to_canonical / previous;
        const bool source_contract_changed =
            context.last_has_pre_exposure != color_contract.has_pre_exposure ||
            context.last_has_exposure_scale != color_contract.has_exposure_scale ||
            context.last_has_exposure_texture != has_exposure_texture;
        // Ordinary auto-exposure changes are normalized by the codec. A large
        // single-frame EV jump, however, invalidates temporal history even if
        // the parameter names themselves did not change.
        const float ev_delta = std::fabs(std::log2(
            std::max(ratio, 1.0f / 1024.0f)));
        color_contract_reset = source_contract_changed || ev_delta > 0.5f;
        if (color_contract_reset) {
            ++context.color_contract_resets;
            if (context.color_contract_resets <= 3 ||
                (context.color_contract_resets % 120) == 0) {
                Log("HDR exposure contract transition: previous=%.6f "
                    "current=%.6f preExposure=%.6f exposureScale=%.6f "
                    "texture=%d evDelta=%.3f; resetting NR history (count=%u).",
                    static_cast<double>(previous),
                    static_cast<double>(color_contract.source_to_canonical),
                    static_cast<double>(color_contract.pre_exposure),
                    static_cast<double>(color_contract.exposure_scale),
                    has_exposure_texture ? 1 : 0,
                    static_cast<double>(ev_delta),
                    context.color_contract_resets);
            }
        }
    }
    context.last_source_to_canonical = color_contract.source_to_canonical;
    context.last_has_pre_exposure = color_contract.has_pre_exposure;
    context.last_has_exposure_scale = color_contract.has_exposure_scale;
    context.last_has_exposure_texture = has_exposure_texture;

    // The proxy has already been transformed into canonical exposure. Keep
    // standard exposure keys neutral if this feature build happens to consume
    // them, while leaving the game's original SR parameter block untouched.
    SetFloat(params, "DLSS.Pre.Exposure", 1.0f);
    SetFloat(params, "DLSS.Exposure.Scale", 1.0f);
    SetInt(params, "DLSSNR.DepthInverted", depth_inverted);
    SetInt(params, "DLSSNR.Enabled", 1);
    SetInt(params, "DLSSNR.Reset",
           (reset || game_reset != 0 || config_changed || color_contract_reset) ? 1 : 0);
    SetFloat(params, "DLSSNR.Intensity", g_config.intensity);
    SetFloat(params, "DLSSNR.LocalToneStrength", g_config.local_tone);
    SetFloat(params, "DLSSNR.LocalStructureStrength", g_config.local_structure);
    SetFloat(params, "DLSSNR.SkinStructureStrength", g_config.skin_structure);
    SetInt(params, "DLSSNR.UseAutoMask", g_config.auto_mask ? 1 : 0);
    SetUInt(params, "DLSSNR.Style", g_config.style);
    SetInt(params, "DLSSNR.UICorrection", g_config.ui_correction ? 1 : 0);
    SetInt(params, "DLSS.Indicator.Invert.X.Axis", 0);
    SetInt(params, "DLSS.Indicator.Invert.Y.Axis", 0);
    return true;
}

NgxResult CreateSignedNrFeature(ID3D12GraphicsCommandList *command_list,
                                NrContext &context,
                                const FeatureRecord &feature,
                                void **out_handle) {
    (void)feature;
    *out_handle = nullptr;
    SetInt(context.params, "DLSS.Feature.Create.Flags",
           context.bound_create_flags);
    SetInt(context.params, "PerfQualityValue", context.bound_perf_quality);
    void *handle = nullptr;
    const NgxResult result = g_nr_api.bridge_create(
        g_nr_api.signed_create, command_list, 18, context.params, &handle);
    if (result == kNgxSuccess && handle != nullptr) {
        context.create_variant_selected = true;
        *out_handle = handle;
    } else if (handle != nullptr) {
        // A failed Create may still have recorded initialization commands on
        // this command list. Releasing the returned object immediately is not
        // fence-safe. This exceptional handle is deliberately quarantined
        // until process exit rather than risking a GPU use-after-free.
        Log("Feature 18 Create failed but returned handle=%p; quarantined until "
            "process exit instead of releasing before command-list completion.",
            handle);
    }
    return result;
}

struct Substitution {
    bool active = false;
    NgxParams *params = nullptr;
    ID3D12Resource *original_color = nullptr;
    unsigned int base_x = 0;
    unsigned int base_y = 0;
};

Substitution PrepareBeforeSr(ID3D12GraphicsCommandList *command_list,
                             const NgxHandle *sr_handle,
                             const NgxParams *sr_params,
                             const FeatureRecord &feature) {
    Substitution substitution = {};
    if (!g_config.enabled || g_config.mode != 2 || feature.feature_id != 1 ||
        command_list == nullptr || sr_params == nullptr) return substitution;

    ID3D12Resource *color = nullptr;
    if (!GetResource(sr_params, "Color", &color) || color == nullptr) return substitution;
    unsigned int width = 0, height = 0;
    GetUInt(sr_params, "DLSS.Render.Subrect.Dimensions.Width", &width);
    GetUInt(sr_params, "DLSS.Render.Subrect.Dimensions.Height", &height);
    if (width == 0) width = feature.width;
    if (height == 0) height = feature.height;
    unsigned int color_x = 0, color_y = 0;
    GetUInt(sr_params, "DLSS.Input.Color.Subrect.Base.X", &color_x);
    GetUInt(sr_params, "DLSS.Input.Color.Subrect.Base.Y", &color_y);
    const D3D12_RESOURCE_DESC color_desc = ID3D12Resource_GetDesc(color);
    g_last_pre_sr_color_format.store(
        static_cast<unsigned int>(color_desc.Format), std::memory_order_relaxed);
    if (width == 0 || height == 0 ||
        !TextureContainsRegion(color, color_x, color_y, width, height) ||
        !IsSupportedSceneColorFormat(color_desc.Format)) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            Log("AI upscaling chain rejected incompatible Color contract: physical=%llux%u "
                "active=%ux%u base=%u,%u format=%u. Original SR retained.",
                static_cast<unsigned long long>(color_desc.Width), color_desc.Height,
                width, height, color_x, color_y,
                static_cast<unsigned int>(color_desc.Format));
        }
        return substitution;
    }
    if (color_x != 0 || color_y != 0 ||
        color_desc.Width != width || color_desc.Height != height) {
        static bool logged_padding = false;
        if (!logged_padding) {
            logged_padding = true;
            Log("AI upscaling chain accepted Color subrect: "
                "physical=%llux%u active=%ux%u base=%u,%u. "
                "Only the active region is processed.",
                static_cast<unsigned long long>(color_desc.Width), color_desc.Height,
                width, height, color_x, color_y);
        }
    }

    const ColorContract color_contract = ReadColorContract(sr_params);
    if (MustBypassUnverifiedExposureTexture(color_contract, feature, 2))
        return substitution;

    ID3D12Device *device = nullptr;
    HRESULT hr = ID3D12GraphicsCommandList_GetDevice(
        command_list, IID_ID3D12Device, reinterpret_cast<void **>(&device));
    if (FAILED(hr) || device == nullptr) return substitution;
    const bool ready = EnsureNrApi(device) && EnsureCodec(device);
    if (!ready) {
        ReleaseCom(device);
        return substitution;
    }

    NrContext *context = FindOrCreateContext(sr_handle, 2, width, height);
    if (context == nullptr) {
        Log("No free NR context for the Before-SR chain; original SR retained.");
        ReleaseCom(device);
        return substitution;
    }
    ++context->frames_seen;
    const unsigned int current_generation =
        g_config_generation.load(std::memory_order_relaxed);
    if (context->nr_handle == nullptr &&
        context->frames_seen < context->next_create_retry_frame) {
        // A user toggle or setting change is an explicit request to retry now.
        if (context->config_generation == current_generation) {
            ReleaseCom(device);
            return substitution;
        }
        context->next_create_retry_frame = 0;
    }

    NrSlot *slot = FindOrCreateSlot(*context, device, color);
    if (slot == nullptr || context->failed) {
        ReleaseCom(device);
        return substitution;
    }
    bool lifecycle_recorded = false;

    ID3D12Resource *motion_override = nullptr;
    unsigned int motion_override_width = 0;
    unsigned int motion_override_height = 0;
    float motion_scale_multiplier_x = 1.0f;
    float motion_scale_multiplier_y = 1.0f;
    ID3D12Resource *game_motion = nullptr;
    if ((feature.create_flags & 2) == 0 &&
        GetResource(sr_params, "MotionVectors", &game_motion) &&
        game_motion != nullptr) {
        unsigned int mv_x = 0, mv_y = 0;
        GetUInt(sr_params, "DLSS.Input.MV.Subrect.Base.X", &mv_x);
        GetUInt(sr_params, "DLSS.Input.MV.Subrect.Base.Y", &mv_y);
        const D3D12_RESOURCE_DESC motion_desc =
            ID3D12Resource_GetDesc(game_motion);
        unsigned int source_width = motion_desc.Width > mv_x
            ? static_cast<unsigned int>(motion_desc.Width - mv_x) : 0;
        unsigned int source_height = motion_desc.Height > mv_y
            ? motion_desc.Height - mv_y : 0;
        // Resolve the logical output rather than assuming OutWidth is always
        // the output side. Some integrations publish Width/OutWidth reversed.
        // An ambiguous contract is safer to bypass than to crop the top-left
        // render-sized portion of a full-resolution motion field.
        ID3D12Resource *game_output = nullptr;
        if (GetResource(sr_params, "Output", &game_output) &&
            game_output != nullptr) {
            unsigned int output_x = 0, output_y = 0;
            GetUInt(sr_params, "DLSS.Output.Subrect.Base.X", &output_x);
            GetUInt(sr_params, "DLSS.Output.Subrect.Base.Y", &output_y);
            const D3D12_RESOURCE_DESC game_output_desc =
                ID3D12Resource_GetDesc(game_output);
            const unsigned int output_width = game_output_desc.Width > output_x
                ? static_cast<unsigned int>(game_output_desc.Width - output_x) : 0;
            const unsigned int output_height = game_output_desc.Height > output_y
                ? game_output_desc.Height - output_y : 0;
            unsigned int logical_output_width = 0;
            unsigned int logical_output_height = 0;
            if (!ResolveSrOutputExtent(feature, width, height,
                                       output_width, output_height,
                                       logical_output_width,
                                       logical_output_height)) {
                static unsigned int ambiguous_motion_extents = 0;
                ++ambiguous_motion_extents;
                if (ambiguous_motion_extents <= 3 ||
                    (ambiguous_motion_extents % 600) == 0) {
                    Log("High-resolution motion extent is ambiguous: create=%ux%u "
                        "-> %ux%u render=%ux%u outputRegion=%ux%u; original SR retained.",
                        feature.width, feature.height,
                        feature.out_width, feature.out_height,
                        width, height, output_width, output_height);
                }
                ReleaseCom(device);
                return substitution;
            }
            source_width = std::min(source_width, logical_output_width);
            source_height = std::min(source_height, logical_output_height);
        } else {
            static unsigned int missing_motion_output_extents = 0;
            ++missing_motion_output_extents;
            if (missing_motion_output_extents <= 3 ||
                (missing_motion_output_extents % 600) == 0) {
                Log("High-resolution motion conversion requires an unambiguous "
                    "game Output extent; original SR retained.");
            }
            ReleaseCom(device);
            return substitution;
        }
        if ((source_width != width || source_height != height) &&
            source_width >= width && source_height >= height) {
            if (!TextureContainsRegion(game_motion, mv_x, mv_y,
                                       source_width, source_height) ||
                !EnsureMotionProxy(device, *slot, game_motion, width, height)) {
                ReleaseCom(device);
                return substitution;
            }
            if (!RecordSlotUseLocked(command_list, *slot)) {
                ReleaseCom(device);
                return substitution;
            }
            lifecycle_recorded = true;
            Transition(command_list, slot->motion_proxy,
                       slot->motion_proxy_state,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            DispatchMotionResample(command_list, *slot, width, height,
                                   source_width, source_height, mv_x, mv_y);
            UavBarrier(command_list, slot->motion_proxy);
            Transition(command_list, slot->motion_proxy,
                       slot->motion_proxy_state,
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            motion_override = slot->motion_proxy;
            motion_override_width = width;
            motion_override_height = height;
            // Spatial resampling and vector value units are independent. The
            // game-provided MV.Scale already defines the conversion to pixel
            // displacement; resource dimensions alone do not prove that the
            // vector magnitude needs another render/output ratio.
            motion_scale_multiplier_x = 1.0f;
            motion_scale_multiplier_y = 1.0f;
            if (!context->motion_conversion_logged) {
                context->motion_conversion_logged = true;
                Log("High-resolution motion conversion enabled: %ux%u@%u,%u "
                    "-> %ux%u, preserving game MV.Scale %.6f,%.6f.",
                    source_width, source_height, mv_x, mv_y, width, height,
                    static_cast<double>(motion_scale_multiplier_x),
                    static_cast<double>(motion_scale_multiplier_y));
            }
        }
    }
    if (!lifecycle_recorded && !RecordSlotUseLocked(command_list, *slot)) {
        ReleaseCom(device);
        return substitution;
    }
    ReleaseCom(device);

    Transition(command_list, slot->proxy, slot->proxy_state,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    DispatchCodec(command_list, *slot, g_codec.encode, width, height,
                  color_contract, (feature.create_flags & 1) != 0,
                  color_x, color_y);
    UavBarrier(command_list, slot->proxy);
    Transition(command_list, slot->proxy, slot->proxy_state,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(command_list, slot->neural, slot->neural_state,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    if (context->params == nullptr) {
        const NgxResult allocated = g_nr_api.allocate(&context->params);
        if (allocated != kNgxSuccess || context->params == nullptr) {
            Log("AllocateParameters for NR failed: 0x%08X", allocated);
            context->failed = true;
            return substitution;
        }
    }
    const bool first = context->nr_handle == nullptr;
    if (!BindNrParameters(*context, *slot, sr_params, feature,
                          width, height, width, height, color_contract, first,
                          motion_override, motion_override_width,
                          motion_override_height, motion_scale_multiplier_x,
                          motion_scale_multiplier_y))
        return substitution;

    if (first) {
        void *handle = nullptr;
        const NgxResult created = CreateSignedNrFeature(
            command_list, *context, feature, &handle);
        g_last_nr_create_result.store(created, std::memory_order_relaxed);
        context->nr_handle = static_cast<NgxHandle *>(handle);
        Log("signed feature 18 create %ux%u -> %ux%u result=0x%08X(%s) handle=%p",
            width, height, width, height, created, ResultName(created), handle);
        if (created != kNgxSuccess || handle == nullptr) {
            g_pre_sr_runtime_rejected.store(
                created == 0xBAD00001u, std::memory_order_relaxed);
            context->nr_handle = nullptr;
            ++context->create_failures;
            context->next_create_retry_frame = context->frames_seen + 300;
            Log("Feature 18 create rejected; preserving game SR and retrying "
                "after 300 frames or immediately after a setting toggle "
                "(failure count=%u).", context->create_failures);
            return substitution;
        }
        g_pre_sr_runtime_rejected.store(false, std::memory_order_relaxed);
        context->next_create_retry_frame = 0;
    }

    const NgxResult evaluated = g_nr_api.bridge_evaluate(
        g_nr_api.signed_evaluate, command_list, context->nr_handle,
        context->params, nullptr);
    if (evaluated != kNgxSuccess) {
        if (context->evaluations < 3)
            Log("signed feature 18 evaluate failed: 0x%08X (%s)",
                evaluated, ResultName(evaluated));
        return substitution;
    }
    ++context->evaluations;
    g_nr_successful_evaluations.fetch_add(1, std::memory_order_relaxed);
    g_last_nr_width.store(width, std::memory_order_relaxed);
    g_last_nr_height.store(height, std::memory_order_relaxed);
    UavBarrier(command_list, slot->neural);
    Transition(command_list, slot->neural, slot->neural_state,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (context->evaluations <= 3 || (context->evaluations % 600) == 0) {
        Log("NR-before-SR evaluate succeeded: count=%u extent=%ux%u identity=%d "
            "transfer=%.3f", context->evaluations, width, height,
            g_config.identity_use_original ? 1 : 0,
            static_cast<double>(g_config.transfer_strength));
    }

    if (g_config.identity_use_original ||
        context->evaluations <= g_config.warmup_after_create) return substitution;

    Transition(command_list, slot->result, slot->result_state,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    DispatchCodec(command_list, *slot, g_codec.combine, width, height,
                  color_contract, (feature.create_flags & 1) != 0,
                  color_x, color_y);
    UavBarrier(command_list, slot->result);
    Transition(command_list, slot->result, slot->result_state,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    substitution.active = true;
    substitution.params = const_cast<NgxParams *>(sr_params);
    substitution.original_color = color;
    substitution.base_x = color_x;
    substitution.base_y = color_y;
    SetResource(substitution.params, "Color", slot->result);
    SetUInt(substitution.params, "DLSS.Input.Color.Subrect.Base.X", 0);
    SetUInt(substitution.params, "DLSS.Input.Color.Subrect.Base.Y", 0);
    return substitution;
}

void RestoreSubstitution(Substitution &substitution) {
    if (!substitution.active || substitution.params == nullptr) return;
    SetResource(substitution.params, "Color", substitution.original_color);
    SetUInt(substitution.params, "DLSS.Input.Color.Subrect.Base.X", substitution.base_x);
    SetUInt(substitution.params, "DLSS.Input.Color.Subrect.Base.Y", substitution.base_y);
    substitution.active = false;
}

struct PostSrSubstitution {
    bool active = false;
    NgxParams *params = nullptr;
    ID3D12Resource *original_output = nullptr;
    NrContext *context = nullptr;
    NrSlot *slot = nullptr;
    unsigned int output_base_x = 0;
    unsigned int output_base_y = 0;
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int guide_width = 0;
    unsigned int guide_height = 0;
    ColorContract color_contract = {};
};

PostSrSubstitution PrepareAfterSr(ID3D12GraphicsCommandList *command_list,
                                  const NgxHandle *sr_handle,
                                  const NgxParams *sr_params,
                                  const FeatureRecord &feature) {
    PostSrSubstitution substitution = {};
    if (!g_config.enabled || g_config.mode != 1 || feature.feature_id != 1 ||
        command_list == nullptr || sr_params == nullptr) return substitution;

    ID3D12Resource *output = nullptr;
    if (!GetResource(sr_params, "Output", &output) || output == nullptr)
        return substitution;
    const D3D12_RESOURCE_DESC output_desc = ID3D12Resource_GetDesc(output);
    unsigned int output_x = 0, output_y = 0;
    GetUInt(sr_params, "DLSS.Output.Subrect.Base.X", &output_x);
    GetUInt(sr_params, "DLSS.Output.Subrect.Base.Y", &output_y);
    unsigned int guide_width = 0, guide_height = 0;
    GetUInt(sr_params, "DLSS.Render.Subrect.Dimensions.Width", &guide_width);
    GetUInt(sr_params, "DLSS.Render.Subrect.Dimensions.Height", &guide_height);
    if (guide_width == 0 || guide_height == 0) {
        ID3D12Resource *color = nullptr;
        if (GetResource(sr_params, "Color", &color) && color != nullptr) {
            unsigned int color_x = 0, color_y = 0;
            GetUInt(sr_params, "DLSS.Input.Color.Subrect.Base.X", &color_x);
            GetUInt(sr_params, "DLSS.Input.Color.Subrect.Base.Y", &color_y);
            const D3D12_RESOURCE_DESC color_desc = ID3D12Resource_GetDesc(color);
            if (guide_width == 0 && color_desc.Width > color_x)
                guide_width = static_cast<unsigned int>(color_desc.Width - color_x);
            if (guide_height == 0 && color_desc.Height > color_y)
                guide_height = color_desc.Height - color_y;
        }
    }
    if (guide_width == 0 || guide_height == 0) return substitution;

    // Most NGX integrations use Width/Height for the render extent and
    // OutWidth/OutHeight for the output extent. Witcher 3's wrapper stores the
    // same two extents in the opposite fields. Resolve the pair by matching the
    // per-frame render subrect, then require the selected output to fit the
    // actual resource. Ambiguous contracts fail closed instead of guessing.
    const unsigned int available_width = output_desc.Width > output_x
        ? static_cast<unsigned int>(output_desc.Width - output_x) : 0;
    const unsigned int available_height = output_desc.Height > output_y
        ? output_desc.Height - output_y : 0;
    unsigned int width = 0;
    unsigned int height = 0;
    ResolveSrOutputExtent(feature, guide_width, guide_height,
                          available_width, available_height, width, height);
    if (width == 0 || height == 0) {
        static unsigned int ambiguous_contracts = 0;
        ++ambiguous_contracts;
        if (ambiguous_contracts <= 3 || (ambiguous_contracts % 600) == 0) {
            Log("Post-SR output extent is ambiguous: create=%ux%u -> %ux%u "
                "render=%ux%u outputRegion=%ux%u@%u,%u; original SR retained.",
                feature.width, feature.height,
                feature.out_width, feature.out_height,
                guide_width, guide_height,
                available_width, available_height, output_x, output_y);
        }
        return substitution;
    }
    if (width == 0 || height == 0 ||
        !TextureContainsRegion(output, output_x, output_y, width, height) ||
        output_desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT ||
        (output_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            Log("Post-SR chain rejected incompatible Output contract: "
                "physical=%llux%u active=%ux%u base=%u,%u format=%u flags=0x%X. "
                "Original SR retained.",
                static_cast<unsigned long long>(output_desc.Width),
                output_desc.Height, width, height, output_x, output_y,
                static_cast<unsigned int>(output_desc.Format),
                static_cast<unsigned int>(output_desc.Flags));
        }
        return substitution;
    }

    const ColorContract color_contract = ReadColorContract(sr_params);
    if (MustBypassUnverifiedExposureTexture(color_contract, feature, 1))
        return substitution;

    ID3D12Device *device = nullptr;
    const HRESULT hr = ID3D12GraphicsCommandList_GetDevice(
        command_list, IID_ID3D12Device, reinterpret_cast<void **>(&device));
    if (FAILED(hr) || device == nullptr) return substitution;
    const bool ready = EnsureNrApi(device) && EnsureCodec(device);
    if (!ready) {
        ReleaseCom(device);
        return substitution;
    }

    NrContext *context = FindOrCreateContext(sr_handle, 1, width, height);
    if (context == nullptr) {
        Log("No free NR context for the post-SR chain; original SR retained.");
        ReleaseCom(device);
        return substitution;
    }
    ++context->frames_seen;
    const unsigned int current_generation =
        g_config_generation.load(std::memory_order_relaxed);
    if (context->nr_handle == nullptr &&
        context->frames_seen < context->next_create_retry_frame) {
        if (context->config_generation == current_generation) {
            ReleaseCom(device);
            return substitution;
        }
        context->next_create_retry_frame = 0;
    }

    NrSlot *slot = FindOrCreateSlot(*context, device, output, true);
    if (slot == nullptr || context->failed) {
        ReleaseCom(device);
        return substitution;
    }
    if (!RecordSlotUseLocked(command_list, *slot)) {
        ReleaseCom(device);
        return substitution;
    }
    ReleaseCom(device);

    Transition(command_list, slot->source, slot->source_state,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    substitution.active = true;
    substitution.params = const_cast<NgxParams *>(sr_params);
    substitution.original_output = output;
    substitution.context = context;
    substitution.slot = slot;
    substitution.output_base_x = output_x;
    substitution.output_base_y = output_y;
    substitution.width = width;
    substitution.height = height;
    substitution.guide_width = guide_width;
    substitution.guide_height = guide_height;
    substitution.color_contract = color_contract;
    SetResource(substitution.params, "Output", slot->source);
    SetUInt(substitution.params, "DLSS.Output.Subrect.Base.X", 0);
    SetUInt(substitution.params, "DLSS.Output.Subrect.Base.Y", 0);
    return substitution;
}

void RestorePostSrParameters(PostSrSubstitution &substitution) {
    if (!substitution.active || substitution.params == nullptr) return;
    SetResource(substitution.params, "Output", substitution.original_output);
    SetUInt(substitution.params, "DLSS.Output.Subrect.Base.X",
            substitution.output_base_x);
    SetUInt(substitution.params, "DLSS.Output.Subrect.Base.Y",
            substitution.output_base_y);
}

void DispatchPostSrCopy(ID3D12GraphicsCommandList *command_list,
                        PostSrSubstitution &substitution) {
    NrSlot &slot = *substitution.slot;
    Transition(command_list, slot.source, slot.source_state,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    DispatchCodec(command_list, slot, g_codec.copy,
                  substitution.width, substitution.height, ColorContract{}, false,
                  0, 0, substitution.output_base_x,
                  substitution.output_base_y);
    UavBarrier(command_list, slot.result);
}

bool FinishAfterSr(ID3D12GraphicsCommandList *command_list,
                   const NgxParams *sr_params, const FeatureRecord &feature,
                   PostSrSubstitution &substitution) {
    NrContext &context = *substitution.context;
    NrSlot &slot = *substitution.slot;
    const unsigned int width = substitution.width;
    const unsigned int height = substitution.height;
    // Use the exact scalar exposure snapshot captured before the game's SR
    // call.  Re-reading mutable parameters afterwards could otherwise encode
    // and combine with different exposure generations.
    const ColorContract color_contract = substitution.color_contract;

    Transition(command_list, slot.source, slot.source_state,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(command_list, slot.proxy, slot.proxy_state,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    DispatchCodec(command_list, slot, g_codec.encode, width, height,
                  color_contract, (feature.create_flags & 1) != 0);
    UavBarrier(command_list, slot.proxy);
    Transition(command_list, slot.proxy, slot.proxy_state,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(command_list, slot.neural, slot.neural_state,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    if (context.params == nullptr) {
        const NgxResult allocated = g_nr_api.allocate(&context.params);
        if (allocated != kNgxSuccess || context.params == nullptr) {
            Log("AllocateParameters for post-SR NR failed: 0x%08X", allocated);
            context.failed = true;
            DispatchPostSrCopy(command_list, substitution);
            return false;
        }
    }
    const bool first = context.nr_handle == nullptr;
    if (!BindNrParameters(context, slot, sr_params, feature, width, height,
                          substitution.guide_width, substitution.guide_height,
                          color_contract, first)) {
        DispatchPostSrCopy(command_list, substitution);
        return false;
    }

    if (first) {
        void *handle = nullptr;
        const NgxResult created = CreateSignedNrFeature(
            command_list, context, feature, &handle);
        g_last_nr_create_result.store(created, std::memory_order_relaxed);
        context.nr_handle = static_cast<NgxHandle *>(handle);
        Log("post-SR signed feature 18 create %ux%u guides=%ux%u "
            "result=0x%08X(%s) handle=%p",
            width, height, substitution.guide_width, substitution.guide_height,
            created, ResultName(created), handle);
        if (created != kNgxSuccess || handle == nullptr) {
            g_post_sr_runtime_rejected.store(
                created == 0xBAD00001u, std::memory_order_relaxed);
            context.nr_handle = nullptr;
            ++context.create_failures;
            context.next_create_retry_frame = context.frames_seen + 300;
            Log("Post-SR Feature 18 create rejected; preserving game SR and "
                "retrying after 300 frames or immediately after a setting "
                "toggle (failure count=%u).", context.create_failures);
            DispatchPostSrCopy(command_list, substitution);
            return false;
        }
        g_post_sr_runtime_rejected.store(false, std::memory_order_relaxed);
        context.next_create_retry_frame = 0;
    }

    const NgxResult evaluated = g_nr_api.bridge_evaluate(
        g_nr_api.signed_evaluate, command_list, context.nr_handle,
        context.params, nullptr);
    if (evaluated != kNgxSuccess) {
        if (evaluated == 0xBAD00001u) {
            g_post_sr_runtime_rejected.store(true, std::memory_order_relaxed);
        }
        if (context.evaluations < 3)
            Log("post-SR feature 18 evaluate failed: 0x%08X (%s)",
                evaluated, ResultName(evaluated));
        DispatchPostSrCopy(command_list, substitution);
        return false;
    }

    ++context.evaluations;
    g_nr_successful_evaluations.fetch_add(1, std::memory_order_relaxed);
    g_last_nr_width.store(width, std::memory_order_relaxed);
    g_last_nr_height.store(height, std::memory_order_relaxed);
    UavBarrier(command_list, slot.neural);
    Transition(command_list, slot.neural, slot.neural_state,
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (context.evaluations <= 3 || (context.evaluations % 600) == 0) {
        Log("NR-after-SR evaluate succeeded: count=%u extent=%ux%u guides=%ux%u "
            "transfer=%.3f", context.evaluations, width, height,
            substitution.guide_width, substitution.guide_height,
            static_cast<double>(g_config.post_transfer_strength));
    }

    if (g_config.identity_use_original ||
        context.evaluations <= g_config.warmup_after_create) {
        DispatchPostSrCopy(command_list, substitution);
        return false;
    }
    DispatchCodec(command_list, slot, g_codec.combine, width, height,
                  color_contract, (feature.create_flags & 1) != 0,
                  0, 0, substitution.output_base_x,
                  substitution.output_base_y);
    UavBarrier(command_list, slot.result);
    return true;
}

using CreateFeatureFn = NgxResult (*)(ID3D12GraphicsCommandList *, int,
                                      NgxParams *, NgxHandle **);
using EvaluateFeatureFn = NgxResult (*)(ID3D12GraphicsCommandList *,
                                        const NgxHandle *, const NgxParams *, void *);
using ReleaseFeatureFn = NgxResult (*)(NgxHandle *);
using ExecuteCommandListsFn = void (STDMETHODCALLTYPE *)(
    ID3D12CommandQueue *, UINT, ID3D12CommandList *const *);

CreateFeatureFn g_original_create = nullptr;
CreateFeatureFn g_original_core_create = nullptr;
EvaluateFeatureFn g_original_evaluate = nullptr;
EvaluateFeatureFn g_original_evaluate_c = nullptr;
EvaluateFeatureFn g_original_core_evaluate = nullptr;
ReleaseFeatureFn g_original_release = nullptr;
ReleaseFeatureFn g_original_core_release = nullptr;
std::atomic<unsigned long long> g_carrier_evaluations{0};
std::atomic<unsigned long long> g_all_evaluations{0};
std::atomic<unsigned int> g_wrapper_calls{0};
std::atomic<unsigned int> g_core_calls{0};
thread_local bool g_inside_ngx_evaluate = false;
// The public nvngx_dlss wrapper may call the identically named export in
// _nvngx.dll. Both exports are hooked for games that call either layer, but
// ownership must be recorded exactly once for a nested wrapper -> core call.
thread_local unsigned int g_create_hook_depth = 0;
thread_local unsigned int g_release_hook_depth = 0;
void *g_evaluate_hook_targets[4] = {};
std::size_t g_evaluate_hook_target_count = 0;
SRWLOCK g_evaluate_hook_lock = SRWLOCK_INIT;
std::atomic<bool> g_evaluate_hooks_ready{false};
std::atomic<bool> g_evaluate_hooks_enabled{false};
std::atomic<bool> g_evaluate_target_overflow{false};
ExecuteCommandListsFn g_original_queue_execute[4] = {};
void *g_queue_execute_hook_targets[4] = {};
std::size_t g_queue_execute_hook_count = 0;
SRWLOCK g_queue_execute_hook_lock = SRWLOCK_INIT;

void ProcessSubmittedCommandLists(std::size_t hook_index,
                                  ID3D12CommandQueue *queue,
                                  UINT count,
                                  ID3D12CommandList *const *command_lists) {
    ExecuteCommandListsFn original = hook_index < std::size(g_original_queue_execute)
        ? g_original_queue_execute[hook_index] : nullptr;
    if (original == nullptr) {
        MarkLifecycleUncertain("missing ExecuteCommandLists trampoline");
        return;
    }

    // Snapshot this before the native call. A different command list may be
    // reset immediately after submission and drop the global active count to
    // zero while this hook is still returning. Re-reading the count only after
    // ExecuteCommandLists would then skip the submitted list whose token and
    // GPU work still need a retirement fence.
    const bool may_have_lifecycle_work =
        g_lifecycle_active_command_lists.load(std::memory_order_acquire) != 0;

    // This callback is intentionally after the native call.  ReShade's
    // execute_command_list event fires before the real submission and cannot
    // by itself prove that a later queue fence orders after this work.
    original(queue, count, command_lists);
    if (g_lifecycle_tracking_uncertain.load(std::memory_order_acquire) ||
        !may_have_lifecycle_work)
        return;

    EnterCriticalSection(&g_chain_cs);
    for (UINT i = 0; i < count; ++i) {
        if (command_lists == nullptr || command_lists[i] == nullptr) continue;
        ID3D12GraphicsCommandList *graphics = nullptr;
        if (SUCCEEDED(ID3D12CommandList_QueryInterface(
                command_lists[i], IID_ID3D12GraphicsCommandList,
                reinterpret_cast<void **>(&graphics))) &&
            graphics != nullptr) {
            MarkLifecycleCommandListSubmittedLocked(queue, graphics);
            ReleaseCom(graphics);
        }
        if (g_lifecycle_tracking_uncertain.load(std::memory_order_relaxed))
            break;
    }
    // Fence the actual submission queue immediately. Waiting for a Present on
    // that queue is unnecessary and can quarantine resources forever in games
    // that run DLSS on a non-present queue.
    SignalLifecycleQueueAndCollectLocked(queue);
    LeaveCriticalSection(&g_chain_cs);
}

void STDMETHODCALLTYPE HookQueueExecute0(
    ID3D12CommandQueue *queue, UINT count,
    ID3D12CommandList *const *command_lists) {
    ProcessSubmittedCommandLists(0, queue, count, command_lists);
}
void STDMETHODCALLTYPE HookQueueExecute1(
    ID3D12CommandQueue *queue, UINT count,
    ID3D12CommandList *const *command_lists) {
    ProcessSubmittedCommandLists(1, queue, count, command_lists);
}
void STDMETHODCALLTYPE HookQueueExecute2(
    ID3D12CommandQueue *queue, UINT count,
    ID3D12CommandList *const *command_lists) {
    ProcessSubmittedCommandLists(2, queue, count, command_lists);
}
void STDMETHODCALLTYPE HookQueueExecute3(
    ID3D12CommandQueue *queue, UINT count,
    ID3D12CommandList *const *command_lists) {
    ProcessSubmittedCommandLists(3, queue, count, command_lists);
}

void *const g_queue_execute_detours[4] = {
    reinterpret_cast<void *>(&HookQueueExecute0),
    reinterpret_cast<void *>(&HookQueueExecute1),
    reinterpret_cast<void *>(&HookQueueExecute2),
    reinterpret_cast<void *>(&HookQueueExecute3),
};

void TrackDlssResolution(const NgxParams *params, const FeatureRecord &feature) {
    unsigned int input_width = 0, input_height = 0;
    GetUInt(params, "DLSS.Render.Subrect.Dimensions.Width", &input_width);
    GetUInt(params, "DLSS.Render.Subrect.Dimensions.Height", &input_height);
    if (input_width == 0) input_width = feature.width;
    if (input_height == 0) input_height = feature.height;
    if (input_width == 0 || input_height == 0) {
        ID3D12Resource *color = nullptr;
        if (GetResource(params, "Color", &color) && color != nullptr) {
            const D3D12_RESOURCE_DESC desc = ID3D12Resource_GetDesc(color);
            if (input_width == 0) input_width = static_cast<unsigned int>(desc.Width);
            if (input_height == 0) input_height = desc.Height;
        }
    }

    unsigned int output_width = feature.out_width;
    unsigned int output_height = feature.out_height;
    ID3D12Resource *output = nullptr;
    if (GetResource(params, "Output", &output) && output != nullptr) {
        const D3D12_RESOURCE_DESC desc = ID3D12Resource_GetDesc(output);
        output_width = static_cast<unsigned int>(desc.Width);
        output_height = desc.Height;
    }

    if (input_width != 0 && input_height != 0) {
        g_last_dlss_input_width.store(input_width, std::memory_order_relaxed);
        g_last_dlss_input_height.store(input_height, std::memory_order_relaxed);
    }
    if (output_width != 0 && output_height != 0) {
        g_last_dlss_output_width.store(output_width, std::memory_order_relaxed);
        g_last_dlss_output_height.store(output_height, std::memory_order_relaxed);
    }
}

NgxResult HookCreate(ID3D12GraphicsCommandList *command_list, int feature_id,
                     NgxParams *params, NgxHandle **out_handle) {
    const bool outermost = g_create_hook_depth++ == 0;
    const bool creating_rr = outermost && feature_id == 13;
    if (creating_rr)
        g_rr_create_in_progress.fetch_add(1, std::memory_order_acq_rel);
    const NgxResult result = g_original_create(command_list, feature_id, params, out_handle);
    --g_create_hook_depth;
    if (!outermost) return result;
    NgxHandle *handle = out_handle != nullptr ? *out_handle : nullptr;
    Log("CreateFeature id=%d (%s) result=0x%08X(%s) handle=%p",
        feature_id, FeatureName(feature_id), result, ResultName(result),
        static_cast<void *>(handle));
    if (params != nullptr && (feature_id == 1 || feature_id == 11 ||
                              feature_id == 13 || feature_id == 18 ||
                              result != kNgxSuccess)) {
        DumpCreate(params);
    }
    if (result == kNgxSuccess && handle != nullptr) {
        RememberFeature(handle, feature_id, params);
    }
    if (creating_rr) {
        const unsigned int previous =
            g_rr_create_in_progress.fetch_sub(1, std::memory_order_acq_rel);
        if (previous == 0) {
            g_rr_create_in_progress.store(0, std::memory_order_release);
            g_feature_tracking_uncertain.store(true, std::memory_order_release);
            Log("RR Create in-progress counter underflow; Feature 1 will fail "
                "closed for this process.");
        }
    }
    return result;
}

NgxResult HookCoreCreate(ID3D12GraphicsCommandList *command_list, int feature_id,
                         NgxParams *params, NgxHandle **out_handle) {
    const bool outermost = g_create_hook_depth++ == 0;
    const bool creating_rr = outermost && feature_id == 13;
    if (creating_rr)
        g_rr_create_in_progress.fetch_add(1, std::memory_order_acq_rel);
    const NgxResult result = g_original_core_create(command_list, feature_id, params, out_handle);
    --g_create_hook_depth;
    if (!outermost) return result;
    NgxHandle *handle = out_handle != nullptr ? *out_handle : nullptr;
    Log("core CreateFeature id=%d (%s) result=0x%08X(%s) handle=%p",
        feature_id, FeatureName(feature_id), result, ResultName(result),
        static_cast<void *>(handle));
    if (params != nullptr && (IsUpscalingCarrierFeature(feature_id) ||
                              feature_id == 13 ||
                              feature_id == 18 || result != kNgxSuccess)) {
        DumpCreate(params);
    }
    if (result == kNgxSuccess && handle != nullptr) {
        RememberFeature(handle, feature_id, params);
    }
    if (creating_rr) {
        const unsigned int previous =
            g_rr_create_in_progress.fetch_sub(1, std::memory_order_acq_rel);
        if (previous == 0) {
            g_rr_create_in_progress.store(0, std::memory_order_release);
            g_feature_tracking_uncertain.store(true, std::memory_order_release);
            Log("RR core Create in-progress counter underflow; Feature 1 will "
                "fail closed for this process.");
        }
    }
    return result;
}

NgxResult ProcessEvaluate(EvaluateFeatureFn original,
                          ID3D12GraphicsCommandList *command_list,
                          const NgxHandle *handle, const NgxParams *params,
                          void *callback) {
    // A disabled add-on must be indistinguishable from an unloaded add-on on
    // the hot NGX path. Configuration polling happens from Present, so this
    // branch avoids parameter lookups, locks, counters and timing work.
    if (!g_enabled_fast.load(std::memory_order_acquire) ||
        g_shortfuse_nr_enabled.load(std::memory_order_relaxed) ||
        g_evaluate_target_overflow.load(std::memory_order_acquire) ||
        g_rr_create_in_progress.load(std::memory_order_acquire) != 0 ||
        g_lifecycle_tracking_uncertain.load(std::memory_order_acquire)) {
        return original(command_list, handle, params, callback);
    }

    const unsigned long long all = ++g_all_evaluations;
    if (g_feature_tracking_uncertain.load(std::memory_order_acquire)) {
        return original(command_list, handle, params, callback);
    }
    const FeatureRecord feature = FindFeature(handle);
    if (feature.feature_id == 13) {
        g_last_carrier_feature.store(13, std::memory_order_relaxed);
        g_rr_guard_was_active.store(true, std::memory_order_relaxed);
        const unsigned long long rr = ++g_rr_passthrough_evaluations;
        if (rr <= 3 || (rr % 600) == 0) {
            Log("Ray Reconstruction Feature 13 evaluate #%llu passed through "
                "strictly unmodified; Feature 18 and texture substitution are "
                "disabled for the RR path.", rr);
        }
        return original(command_list, handle, params, callback);
    }
    if (!IsUpscalingCarrierFeature(feature.feature_id) || params == nullptr) {
        return original(command_list, handle, params, callback);
    }

    // Some engines evaluate an ordinary Feature 1 before Feature 13 in the
    // same frame.  A wall-clock timeout cannot protect that ordering and can
    // span anywhere from a few to hundreds of frames.  Use Feature 13 handle
    // lifetime instead: a false-positive bypass is recoverable, while a missed
    // RR frame would feed altered colour into a second reconstruction network.
    const unsigned int live_rr_handles =
        g_live_rr_handles.load(std::memory_order_acquire);
    if (live_rr_handles != 0) {
        g_last_carrier_feature.store(13, std::memory_order_relaxed);
        g_rr_guard_was_active.store(true, std::memory_order_relaxed);
        const unsigned long long guarded = ++g_rr_guarded_sr_evaluations;
        if (guarded <= 3 || (guarded % 600) == 0) {
            Log("SuperSampling evaluate #%llu passed through unmodified "
                "because %u live Ray Reconstruction handle(s) own the "
                "viewport conservatively.", guarded, live_rr_handles);
        }
        return original(command_list, handle, params, callback);
    }
    if (g_rr_guard_was_active.exchange(false, std::memory_order_relaxed)) {
        // The last live RR handle has gone away. Any Feature 18 history from
        // before RR is stale, so reset on the first resumed ordinary-SR frame.
        g_config_generation.fetch_add(1, std::memory_order_relaxed);
        Log("Ray Reconstruction handle ownership ended; ordinary SR processing "
            "resumed with a fresh NR history.");
    }

    g_last_carrier_feature.store(feature.feature_id, std::memory_order_relaxed);
    TrackDlssResolution(params, feature);
    const unsigned long long carrier = ++g_carrier_evaluations;
    if (carrier <= 12 || (carrier % 600) == 0)
        DumpEvaluate(params, feature, carrier);
    if ((carrier % 600) == 0) {
        Log("[stats] all NGX evaluations=%llu, upscaling-carrier evaluations=%llu "
            "feature=%d(%s)", all, carrier, feature.feature_id,
            FeatureName(feature.feature_id));
    }

    EnterCriticalSection(&g_chain_cs);
    // Re-check RR ownership after taking the chain lock. Feature 13 may have
    // completed Create after the earlier lock-free check but before this frame
    // starts recording substitutions.
    if (g_feature_tracking_uncertain.load(std::memory_order_acquire) ||
        g_rr_create_in_progress.load(std::memory_order_acquire) != 0 ||
        g_live_rr_handles.load(std::memory_order_acquire) != 0) {
        g_last_carrier_feature.store(13, std::memory_order_relaxed);
        g_rr_guard_was_active.store(true, std::memory_order_relaxed);
        LeaveCriticalSection(&g_chain_cs);
        return original(command_list, handle, params, callback);
    }
    // A zero visible transfer is the user's pure-game-SR baseline.  Previous
    // builds still created/evaluated Feature 18 and paid its full GPU cost even
    // though the combine shader copied the original image.  Keep the explicit
    // diagnostic identity mode as the opt-in way to measure chain overhead.
    const bool zero_transfer_native =
        !g_config.identity_use_original &&
        ActiveTransferStrength(g_config) <= 0.000001f;
    if (!g_config.enabled || zero_transfer_native ||
        g_shortfuse_nr_enabled.load(std::memory_order_relaxed)) {
        if (zero_transfer_native)
            g_unverified_exposure_texture_bypass.store(false,
                                                        std::memory_order_relaxed);
        const NgxResult result = original(command_list, handle, params, callback);
        LeaveCriticalSection(&g_chain_cs);
        return result;
    }
    Substitution before_sr = PrepareBeforeSr(
        command_list, handle, params, feature);
    PostSrSubstitution after_sr = PrepareAfterSr(
        command_list, handle, params, feature);
    NgxResult result = original(command_list, handle, params, callback);
    const bool used_before_sr = before_sr.active;
    const bool used_after_sr = after_sr.active;
    RestoreSubstitution(before_sr);
    RestorePostSrParameters(after_sr);
    if (used_before_sr && result != kNgxSuccess &&
        g_config.retry_original_on_sr_failure) {
        Log("%s failed with NR-derived Color: 0x%08X (%s); retrying original Color.",
            FeatureName(feature.feature_id), result, ResultName(result));
        result = original(command_list, handle, params, callback);
        Log("Original-Color %s retry result=0x%08X (%s).",
            FeatureName(feature.feature_id), result, ResultName(result));
    }
    if (used_after_sr) {
        if (result == kNgxSuccess) {
            FinishAfterSr(command_list, params, feature, after_sr);
        } else if (g_config.retry_original_on_sr_failure) {
            Log("%s failed while targeting the post-upscaling intermediate: "
                "0x%08X (%s); retrying the original Output.",
                FeatureName(feature.feature_id), result, ResultName(result));
            result = original(command_list, handle, params, callback);
            Log("Original-Output %s retry result=0x%08X (%s).",
                FeatureName(feature.feature_id), result, ResultName(result));
        }
    }
    LeaveCriticalSection(&g_chain_cs);
    return result;
}

NgxResult DispatchEvaluate(EvaluateFeatureFn original,
                           ID3D12GraphicsCommandList *command_list,
                           const NgxHandle *handle, const NgxParams *params,
                           void *callback) {
    if (g_inside_ngx_evaluate) {
        return original(command_list, handle, params, callback);
    }
    g_inside_ngx_evaluate = true;
    const NgxResult result = ProcessEvaluate(
        original, command_list, handle, params, callback);
    g_inside_ngx_evaluate = false;
    return result;
}

NgxResult HookEvaluate(ID3D12GraphicsCommandList *command_list,
                       const NgxHandle *handle, const NgxParams *params,
                       void *callback) {
    if (!g_enabled_fast.load(std::memory_order_acquire) ||
        g_shortfuse_nr_enabled.load(std::memory_order_relaxed) ||
        g_evaluate_target_overflow.load(std::memory_order_acquire)) {
        return g_original_evaluate(command_list, handle, params, callback);
    }
    const unsigned int call = ++g_wrapper_calls;
    if (call <= 3) Log("nvngx_dlss EvaluateFeature reached: call=%u handle=%p", call,
                       static_cast<const void *>(handle));
    return DispatchEvaluate(g_original_evaluate, command_list, handle, params, callback);
}

NgxResult HookEvaluateC(ID3D12GraphicsCommandList *command_list,
                        const NgxHandle *handle, const NgxParams *params,
                        void *callback) {
    if (!g_enabled_fast.load(std::memory_order_acquire) ||
        g_shortfuse_nr_enabled.load(std::memory_order_relaxed) ||
        g_evaluate_target_overflow.load(std::memory_order_acquire)) {
        return g_original_evaluate_c(command_list, handle, params, callback);
    }
    return DispatchEvaluate(g_original_evaluate_c, command_list, handle, params, callback);
}

NgxResult HookCoreEvaluate(ID3D12GraphicsCommandList *command_list,
                           const NgxHandle *handle, const NgxParams *params,
                           void *callback) {
    if (!g_enabled_fast.load(std::memory_order_acquire) ||
        g_shortfuse_nr_enabled.load(std::memory_order_relaxed) ||
        g_evaluate_target_overflow.load(std::memory_order_acquire)) {
        return g_original_core_evaluate(command_list, handle, params, callback);
    }
    const unsigned int call = ++g_core_calls;
    if (call <= 3) Log("_nvngx core EvaluateFeature reached: call=%u handle=%p", call,
                       static_cast<const void *>(handle));
    return DispatchEvaluate(g_original_core_evaluate, command_list, handle, params, callback);
}

NgxResult HookRelease(NgxHandle *handle) {
    const bool outermost = g_release_hook_depth++ == 0;
    const FeatureRecord feature = FindFeature(handle);
    const NgxResult result = g_original_release(handle);
    --g_release_hook_depth;
    if (!outermost) return result;
    if (feature.handle != nullptr) {
        Log("ReleaseFeature handle=%p id=%d (%s)", static_cast<void *>(handle),
            feature.feature_id, FeatureName(feature.feature_id));
    }
    if (result == kNgxSuccess || result == kNgxFeatureNotFound) {
        EnterCriticalSection(&g_chain_cs);
        RetireContextForHandle(handle);
        LeaveCriticalSection(&g_chain_cs);
        ForgetFeature(handle);
    } else {
        Log("ReleaseFeature failed for tracked handle=%p result=0x%08X (%s); "
            "ownership and derived resources retained.",
            static_cast<void *>(handle), result, ResultName(result));
        if (feature.feature_id == 13) {
            g_feature_tracking_uncertain.store(true, std::memory_order_release);
            Log("RR handle release failed; all Feature 1 processing will remain "
                "fail-closed for this process.");
        }
    }
    return result;
}

NgxResult HookCoreRelease(NgxHandle *handle) {
    const bool outermost = g_release_hook_depth++ == 0;
    const FeatureRecord feature = FindFeature(handle);
    const NgxResult result = g_original_core_release(handle);
    --g_release_hook_depth;
    if (!outermost) return result;
    if (feature.handle != nullptr) {
        Log("core ReleaseFeature handle=%p id=%d (%s)", static_cast<void *>(handle),
            feature.feature_id, FeatureName(feature.feature_id));
    }
    if (result == kNgxSuccess || result == kNgxFeatureNotFound) {
        EnterCriticalSection(&g_chain_cs);
        RetireContextForHandle(handle);
        LeaveCriticalSection(&g_chain_cs);
        ForgetFeature(handle);
    } else {
        Log("core ReleaseFeature failed for tracked handle=%p result=0x%08X (%s); "
            "ownership and derived resources retained.",
            static_cast<void *>(handle), result, ResultName(result));
        if (feature.feature_id == 13) {
            g_feature_tracking_uncertain.store(true, std::memory_order_release);
            Log("RR handle release failed; all Feature 1 processing will remain "
                "fail-closed for this process.");
        }
    }
    return result;
}

bool CreateHook(void *target, void *detour, void **original, const char *name) {
    if (target == nullptr) {
        Log("Hook target absent: %s", name);
        return false;
    }
    const MH_STATUS status = MH_CreateHook(target, detour, original);
    Log("MH_CreateHook %-38s target=%p status=%d", name, target,
        static_cast<int>(status));
    return status == MH_OK;
}

bool CreateOptionalHook(void *target, void *detour, void **original,
                        const char *name) {
    if (target == nullptr) {
        Log("Optional hook target absent: %s; continuing.", name);
        return false;
    }
    if (!CreateHook(target, detour, original, name)) {
        Log("Optional hook failed: %s; continuing with the primary NGX hooks.",
            name);
        return false;
    }
    return true;
}

bool EnsureNativeQueueExecuteHook(reshade::api::command_queue *queue) {
    if (queue == nullptr || queue->get_device() == nullptr ||
        queue->get_device()->get_api() != reshade::api::device_api::d3d12)
        return true;
    auto *native_queue = reinterpret_cast<ID3D12CommandQueue *>(
        queue->get_native());
    if (native_queue == nullptr) return false;
    void **vtable = *reinterpret_cast<void ***>(native_queue);
    void *target = vtable != nullptr ? vtable[10] : nullptr;
    if (target == nullptr) return false;

    AcquireSRWLockExclusive(&g_queue_execute_hook_lock);
    for (std::size_t i = 0; i < g_queue_execute_hook_count; ++i) {
        if (g_queue_execute_hook_targets[i] == target) {
            ReleaseSRWLockExclusive(&g_queue_execute_hook_lock);
            return true;
        }
    }
    if (g_queue_execute_hook_count >= std::size(g_queue_execute_hook_targets)) {
        ReleaseSRWLockExclusive(&g_queue_execute_hook_lock);
        return false;
    }

    const MH_STATUS initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) {
        Log("MH_Initialize for queue lifetime hook failed: status=%d",
            static_cast<int>(initialized));
        ReleaseSRWLockExclusive(&g_queue_execute_hook_lock);
        return false;
    }
    const std::size_t index = g_queue_execute_hook_count;
    const MH_STATUS created = MH_CreateHook(
        target, g_queue_execute_detours[index],
        reinterpret_cast<void **>(&g_original_queue_execute[index]));
    if (created != MH_OK) {
        Log("MH_CreateHook D3D12 ExecuteCommandLists target=%p failed: status=%d",
            target, static_cast<int>(created));
        ReleaseSRWLockExclusive(&g_queue_execute_hook_lock);
        return false;
    }
    g_queue_execute_hook_targets[index] = target;
    const MH_STATUS enabled = MH_EnableHook(target);
    if (enabled != MH_OK && enabled != MH_ERROR_ENABLED) {
        Log("MH_EnableHook D3D12 ExecuteCommandLists target=%p failed: status=%d",
            target, static_cast<int>(enabled));
        ReleaseSRWLockExclusive(&g_queue_execute_hook_lock);
        return false;
    }
    ++g_queue_execute_hook_count;
    Log("D3D12 ExecuteCommandLists post-submit lifetime hook active: target=%p "
        "slot=%zu.", target, index);
    ReleaseSRWLockExclusive(&g_queue_execute_hook_lock);
    return true;
}

void OnExecuteCommandList(reshade::api::command_queue *queue,
                          reshade::api::command_list *command_list) {
    if (queue == nullptr || command_list == nullptr ||
        queue->get_device() == nullptr ||
        queue->get_device()->get_api() != reshade::api::device_api::d3d12)
        return;
    if (g_lifecycle_tracking_uncertain.load(std::memory_order_acquire) ||
        g_lifecycle_active_command_lists.load(std::memory_order_acquire) == 0)
        return;

    auto *native_command_list = reinterpret_cast<ID3D12GraphicsCommandList *>(
        command_list->get_native());
    unsigned long long token = 0;
    if (!ReadLifecycleToken(native_command_list, token)) return;
    // Pin the concrete record before doing any hook-management work. A Reset
    // callback on another thread must see pending_execute_callbacks != 0 and
    // quarantine rather than erase a record whose commands are about to submit.
    EnterCriticalSection(&g_chain_cs);
    LifecycleCommandList *record = FindLifecycleCommandListLocked(token);
    if (record == nullptr) {
        MarkLifecycleUncertain("unknown command-list marker before submission");
        LeaveCriticalSection(&g_chain_cs);
        return;
    }
    ++record->pending_execute_callbacks;
    LeaveCriticalSection(&g_chain_cs);

    // The ReShade event is before the native submission, so this is the last
    // safe point to install the post-submit proof hook. Do it only for a list
    // that actually contains our GPU work; pure SR never pays this hook cost.
    if (!EnsureNativeQueueExecuteHook(queue)) {
        MarkLifecycleUncertain("could not install post-submit queue hook");
        return;
    }
}

void OnResetOrDestroyCommandList(reshade::api::command_list *command_list) {
    if (command_list == nullptr || command_list->get_device() == nullptr ||
        command_list->get_device()->get_api() !=
            reshade::api::device_api::d3d12 ||
        g_lifecycle_tracking_uncertain.load(std::memory_order_acquire))
        return;
    auto *native_command_list = reinterpret_cast<ID3D12GraphicsCommandList *>(
        command_list->get_native());
    EnterCriticalSection(&g_chain_cs);
    DiscardLifecycleCommandListLocked(native_command_list);
    CollectRetiredContextsLocked();
    LeaveCriticalSection(&g_chain_cs);
}

void RememberEvaluateHookTarget(void *target) {
    if (target == nullptr) return;
    for (std::size_t i = 0; i < g_evaluate_hook_target_count; ++i) {
        if (g_evaluate_hook_targets[i] == target) return;
    }
    if (g_evaluate_hook_target_count < 4) {
        g_evaluate_hook_targets[g_evaluate_hook_target_count++] = target;
    } else if (!g_evaluate_target_overflow.exchange(
                   true, std::memory_order_release)) {
        Log("Evaluate hook target table exhausted; add-on will fail closed "
            "to native SR for this process.");
    }
}

bool SetEvaluateHooksEnabled(bool enable) {
    if (!g_evaluate_hooks_ready.load(std::memory_order_acquire)) return false;
    // Present calls this every frame. Avoid taking an exclusive lock when the
    // requested state is already active; in particular, F6-off now has only an
    // atomic read on this path.
    if (g_evaluate_hooks_enabled.load(std::memory_order_acquire) == enable)
        return true;
    AcquireSRWLockExclusive(&g_evaluate_hook_lock);
    if (g_evaluate_hooks_enabled.load(std::memory_order_relaxed) == enable) {
        ReleaseSRWLockExclusive(&g_evaluate_hook_lock);
        return true;
    }

    bool okay = true;
    for (std::size_t i = 0; i < g_evaluate_hook_target_count; ++i) {
        const MH_STATUS status = enable
            ? MH_QueueEnableHook(g_evaluate_hook_targets[i])
            : MH_QueueDisableHook(g_evaluate_hook_targets[i]);
        if (status != MH_OK &&
            status != (enable ? MH_ERROR_ENABLED : MH_ERROR_DISABLED)) {
            Log("Failed to queue %s for Evaluate hook target=%p status=%d",
                enable ? "enable" : "disable", g_evaluate_hook_targets[i],
                static_cast<int>(status));
            okay = false;
        }
    }
    if (okay) {
        const MH_STATUS applied = MH_ApplyQueued();
        okay = applied == MH_OK;
        if (!okay) {
            Log("MH_ApplyQueued for Evaluate hooks failed: status=%d",
                static_cast<int>(applied));
        }
    }
    if (okay) {
        g_evaluate_hooks_enabled.store(enable, std::memory_order_release);
        Log("Per-frame NGX Evaluate hooks %s (master=%d ShortFuseOwnsNR=%d).",
            enable ? "enabled" : "disabled",
            g_enabled_fast.load(std::memory_order_relaxed) ? 1 : 0,
            g_shortfuse_nr_enabled.load(std::memory_order_relaxed) ? 1 : 0);
    }
    ReleaseSRWLockExclusive(&g_evaluate_hook_lock);
    return okay;
}

void UpdateEvaluateHookActivation() {
    // Evaluate detours remain installed for the process lifetime. F6 and
    // ShortFuse ownership use the atomic fast path in ProcessEvaluate. Runtime
    // MinHook toggling from concurrent Present calls caused a race between UI
    // state and the actual hook state in multi-swapchain titles.
    SetEvaluateHooksEnabled(true);
}

bool InstallHooks(HMODULE ngx) {
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        Log("MH_Initialize failed: %d", static_cast<int>(init));
        return false;
    }

    void *create = reinterpret_cast<void *>(GetProcAddress(ngx, "NVSDK_NGX_D3D12_CreateFeature"));
    void *evaluate = reinterpret_cast<void *>(GetProcAddress(ngx, "NVSDK_NGX_D3D12_EvaluateFeature"));
    void *evaluate_c = reinterpret_cast<void *>(GetProcAddress(ngx, "NVSDK_NGX_D3D12_EvaluateFeature_C"));
    void *release = reinterpret_cast<void *>(GetProcAddress(ngx, "NVSDK_NGX_D3D12_ReleaseFeature"));
    HMODULE core = GetModuleHandleW(L"_nvngx.dll");
    void *core_create = core != nullptr ? reinterpret_cast<void *>(
        GetProcAddress(core, "NVSDK_NGX_D3D12_CreateFeature")) : nullptr;
    void *core_evaluate = core != nullptr ? reinterpret_cast<void *>(
        GetProcAddress(core, "NVSDK_NGX_D3D12_EvaluateFeature")) : nullptr;
    void *core_release = core != nullptr ? reinterpret_cast<void *>(
        GetProcAddress(core, "NVSDK_NGX_D3D12_ReleaseFeature")) : nullptr;
    bool okay = true;
    okay &= CreateHook(create, reinterpret_cast<void *>(&HookCreate),
                       reinterpret_cast<void **>(&g_original_create), "D3D12_CreateFeature");
    const bool evaluate_hooked = CreateHook(
        evaluate, reinterpret_cast<void *>(&HookEvaluate),
        reinterpret_cast<void **>(&g_original_evaluate),
        "D3D12_EvaluateFeature");
    okay &= evaluate_hooked;
    if (evaluate_hooked) RememberEvaluateHookTarget(evaluate);
    if (evaluate_c != nullptr && evaluate_c != evaluate) {
        if (CreateOptionalHook(evaluate_c,
                               reinterpret_cast<void *>(&HookEvaluateC),
                               reinterpret_cast<void **>(&g_original_evaluate_c),
                               "D3D12_EvaluateFeature_C")) {
            RememberEvaluateHookTarget(evaluate_c);
        }
    }
    okay &= CreateHook(release, reinterpret_cast<void *>(&HookRelease),
                       reinterpret_cast<void **>(&g_original_release), "D3D12_ReleaseFeature");
    if (CreateOptionalHook(core_evaluate,
                           reinterpret_cast<void *>(&HookCoreEvaluate),
                           reinterpret_cast<void **>(&g_original_core_evaluate),
                           "core D3D12_EvaluateFeature")) {
        RememberEvaluateHookTarget(core_evaluate);
    }
    CreateOptionalHook(core_create, reinterpret_cast<void *>(&HookCoreCreate),
                       reinterpret_cast<void **>(&g_original_core_create),
                       "core D3D12_CreateFeature");
    CreateOptionalHook(core_release,
                       reinterpret_cast<void *>(&HookCoreRelease),
                       reinterpret_cast<void **>(&g_original_core_release),
                       "core D3D12_ReleaseFeature");
    if (!okay) return false;
    const MH_STATUS enabled = MH_EnableHook(MH_ALL_HOOKS);
    Log("MH_EnableHook all status=%d", static_cast<int>(enabled));
    if (enabled != MH_OK) return false;
    g_evaluate_hooks_enabled.store(true, std::memory_order_release);
    g_evaluate_hooks_ready.store(true, std::memory_order_release);
    UpdateEvaluateHookActivation();
    return true;
}

DWORD WINAPI HookWorker(void *) {
    HMODULE pinned = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&HookWorker), &pinned);

    for (unsigned int attempt = 0; attempt < 24000; ++attempt) {
        HMODULE ngx = GetModuleHandleW(L"nvngx_dlss.dll");
        if (ngx != nullptr) {
            wchar_t path[MAX_PATH] = {};
            GetModuleFileNameW(ngx, path, MAX_PATH);
            Log("Found SR NGX module: %ls", path);
            if (InstallHooks(ngx)) {
                Log("NGX observer/AI-upscaling hooks active.");
            } else {
                Log("Hook installation failed; observer is inactive.");
            }
            return 0;
        }
        Sleep(attempt < 2000 ? 5 : 50);
    }
    Log("Timed out waiting for nvngx_dlss.dll; DLSS was not loaded.");
    return 0;
}

void ToggleMasterFromHotkey() {
    ChainConfig next = SnapshotConfig();
    next.enabled = !next.enabled;
    ApplyConfig(next, "F6");
}

void OnPresent(reshade::api::command_queue *queue, reshade::api::swapchain *,
               const reshade::api::rect *, const reshade::api::rect *,
               std::uint32_t, const reshade::api::rect *) {
    EnterCriticalSection(&g_chain_cs);
    if (queue != nullptr && queue->get_device() != nullptr &&
        queue->get_device()->get_api() == reshade::api::device_api::d3d12) {
        auto *native_queue = reinterpret_cast<ID3D12CommandQueue *>(
            queue->get_native());
        if (native_queue != nullptr)
            SignalLifecycleQueueAndCollectLocked(native_queue);
        else
            CollectRetiredContextsLocked();
    } else {
        CollectRetiredContextsLocked();
    }
    LeaveCriticalSection(&g_chain_cs);
    PollConfigFromPresent();
    // Measure presented frames instead of NGX evaluations. This keeps the
    // reference sampler alive while F6 is off and avoids counting engines that
    // submit more than one NGX evaluation per displayed frame.
    ObserveFrameCadence();
    static bool f6_was_down = false;
    static ULONGLONG last_f6_toggle_ms = 0;
    const SHORT f6_state = GetAsyncKeyState(VK_F6);
    const bool f6_is_down = (f6_state & 0x8000) != 0;
    // A very short tap can start and finish between two Present callbacks. Use
    // the transition bit as a fallback, while keeping the down-edge check for
    // hosts that consume that bit before the addon sees it. The debounce also
    // prevents one physical press from being handled by both paths.
    const bool f6_pressed = (f6_state & 0x0001) != 0 ||
                            (f6_is_down && !f6_was_down);
    const ULONGLONG now_ms = GetTickCount64();
    if (f6_pressed && now_ms - last_f6_toggle_ms >= 250) {
        last_f6_toggle_ms = now_ms;
        ToggleMasterFromHotkey();
    }
    f6_was_down = f6_is_down;
    UpdateEvaluateHookActivation();
}

void DrawSettings(reshade::api::effect_runtime *) {
    ChainConfig next = SnapshotConfig();
    bool changed = false;
    const auto ui = [](const char *english, const char *chinese) {
        return kChineseUi ? chinese : english;
    };

    ImGui::SeparatorText(ui("DLSS 5 AI Rendering Upscaling Edition",
                            "DLSS 5 AI渲染超分版"));
    changed |= ImGui::Checkbox(ui("Enabled (F6)", "启用（F6）"), &next.enabled);

    const bool shortfuse_loaded =
        g_shortfuse_loaded.load(std::memory_order_relaxed);
    const bool shortfuse_nr_enabled =
        g_shortfuse_nr_enabled.load(std::memory_order_relaxed);
    if (shortfuse_nr_enabled) {
        ImGui::TextWrapped(
            "%s", ui(
                "ShortFuse NR is enabled. Double-pass protection is active, "
                "so ShortFuse owns NR and this AI upscaling pass is bypassed.",
                "ShortFuse 的 NR 已开启。双重处理保护已生效：当前由 "
                "ShortFuse 接管 NR，本 AI 渲染超分链路自动跳过。"));
    } else if (shortfuse_loaded) {
        ImGui::TextDisabled(
            "%s", ui(
                "ShortFuse sidecar loaded (NR off): safe for this chain.",
                "ShortFuse 兼容插件已加载（NR 关闭）：可安全使用本链路。"));
    }

    bool before_sr = next.mode == 2;
    if (ImGui::Checkbox(
            ui("Use render-resolution NR -> SR",
               "使用渲染分辨率 NR -> SR"),
            &before_sr)) {
        next.mode = before_sr ? 2 : 1;
        changed = true;
    }
    ImGui::TextWrapped(
        "%s", ui(
            "Off: game SR -> output-resolution NR (original post-pass).  "
            "On: render-resolution NR -> game SR (performance path).",
            "关闭：游戏 SR -> 输出分辨率 NR（原始后置链路）。  "
            "开启：渲染分辨率 NR -> 游戏 SR（性能链路）。"));
    ImGui::TextDisabled(
        "%s", ui(
            "NR -> SR reads the game's real DLSS render size. Changing the "
            "in-game DLSS quality changes this input automatically. This "
            "add-on does not override render resolution.",
            "NR -> SR 直接读取游戏当前的 DLSS 渲染尺寸；切换游戏内 DLSS "
            "挡位后，输入分辨率会自动跟随。本插件不会覆盖游戏渲染分辨率。"));

    const unsigned long long evaluations =
        g_nr_successful_evaluations.load(std::memory_order_relaxed);
    const unsigned int width = g_last_nr_width.load(std::memory_order_relaxed);
    const unsigned int height = g_last_nr_height.load(std::memory_order_relaxed);
    const unsigned int input_width =
        g_last_dlss_input_width.load(std::memory_order_relaxed);
    const unsigned int input_height =
        g_last_dlss_input_height.load(std::memory_order_relaxed);
    const unsigned int output_width =
        g_last_dlss_output_width.load(std::memory_order_relaxed);
    const unsigned int output_height =
        g_last_dlss_output_height.load(std::memory_order_relaxed);
    const DXGI_FORMAT pre_sr_color_format = static_cast<DXGI_FORMAT>(
        g_last_pre_sr_color_format.load(std::memory_order_relaxed));
    const int carrier_feature =
        g_last_carrier_feature.load(std::memory_order_relaxed);
    const unsigned int live_rr_handles =
        g_live_rr_handles.load(std::memory_order_relaxed);
    const bool rr_protected = carrier_feature == 13 || live_rr_handles != 0;
    const bool pre_sr_runtime_rejected =
        g_pre_sr_runtime_rejected.load(std::memory_order_relaxed);
    const bool post_sr_runtime_rejected =
        g_post_sr_runtime_rejected.load(std::memory_order_relaxed);
    const unsigned int last_create_result =
        g_last_nr_create_result.load(std::memory_order_relaxed);
    const float pre_exposure =
        g_last_pre_exposure.load(std::memory_order_relaxed);
    const float exposure_scale =
        g_last_exposure_scale.load(std::memory_order_relaxed);
    const float source_to_canonical =
        g_last_source_to_canonical.load(std::memory_order_relaxed);
    const bool exposure_texture_present =
        g_last_exposure_texture_present.load(std::memory_order_relaxed);
    const bool unverified_exposure_texture_bypass =
        g_unverified_exposure_texture_bypass.load(std::memory_order_relaxed);
    const bool safety_tracking_uncertain =
        g_feature_tracking_uncertain.load(std::memory_order_relaxed) ||
        g_lifecycle_tracking_uncertain.load(std::memory_order_relaxed) ||
        g_evaluate_target_overflow.load(std::memory_order_relaxed);
    if (input_width != 0 && input_height != 0 &&
        output_width != 0 && output_height != 0) {
        ImGui::Text(ui("Game DLSS: %ux%u -> %ux%u",
                       "游戏 DLSS：%u x %u -> %u x %u"),
                    input_width, input_height, output_width, output_height);
    } else {
        ImGui::TextDisabled("%s", ui("Game DLSS: waiting for resolution data",
                                      "游戏 DLSS：等待分辨率数据"));
    }
    if (rr_protected) {
        ImGui::Text("%s", ui(
            "Active game path: Ray Reconstruction (Feature 13, protected pass-through)",
            "当前游戏链路：光线重构（Feature 13，受保护原样旁路）"));
    } else if (carrier_feature == 1) {
        ImGui::Text("%s", ui(
            "Active game path: DLSS Super Resolution (Feature 1)",
            "当前游戏链路：DLSS 超分（Feature 1）"));
    } else {
        ImGui::TextDisabled("%s", ui(
            "Active game path: waiting for DLSS Super Resolution",
            "当前游戏链路：等待 DLSS 超分"));
    }
    if (shortfuse_nr_enabled) {
        ImGui::Text("%s", ui("Status: ShortFuse NR owns this frame",
                              "状态：本帧由 ShortFuse NR 接管"));
    } else if (!next.enabled) {
        ImGui::Text("%s", rr_protected
            ? ui("Status: game Ray Reconstruction + SR only (F6 master off)",
                 "状态：仅游戏光线重构 + SR（F6 总开关已关闭）")
            : ui("Status: pure SR (F6 master off)",
                 "状态：纯 SR（F6 总开关已关闭）"));
    } else if (safety_tracking_uncertain) {
        ImGui::TextWrapped("%s", ui(
            "Status: safety tracking became uncertain, so Feature 18 is "
            "disabled for this process and native game SR is preserved. "
            "Restart the game and include nr-before-sr.log when reporting it.",
            "状态：安全跟踪状态不确定，本进程已禁用 Feature 18 并保留游戏原始 "
            "SR。请重启游戏；反馈时请附上 nr-before-sr.log。"));
    } else if (rr_protected) {
        ImGui::TextWrapped("%s", ui(
            "Status: game Ray Reconstruction is active and is left completely "
            "untouched. Disable RR in the game to use this add-on's SR -> NR "
            "or NR -> SR modes.",
            "状态：游戏光线重构正在运行，本插件完全不改写该链路。若要使用"
            "本插件的 SR -> NR 或 NR -> SR，请先在游戏内关闭光线重构。"));
    } else if (unverified_exposure_texture_bypass) {
        ImGui::TextWrapped("%s", ui(
            "Status: HDR ExposureTexture is present, but its Feature 18 "
            "contract is not verified. The add-on is preserving the game's "
            "original SR to prevent an exposure or colour shift.",
            "状态：检测到 HDR ExposureTexture，但其 Feature 18 合同尚未验证。"
            "为避免曝光或颜色偏移，本插件已保留游戏原始 SR。"));
    } else if (before_sr && pre_sr_runtime_rejected) {
        ImGui::TextWrapped(
            ui("Status: the signed NR runtime rejected Feature 18 creation "
               "(0x%08X). The game SR is preserved; a setting toggle retries "
               "immediately. If this remains, verify RTX 50 GPU, driver 615+ "
               "and the packaged nvngx_dlssnr.dll.",
               "状态：签名 NR 运行库拒绝创建 Feature 18（0x%08X）。已保留"
               "游戏 SR；切换任意设置会立即重试。若持续出现，请确认 RTX 50、"
               "615+ 驱动及压缩包内的 nvngx_dlssnr.dll。"),
            last_create_result);
    } else if (!before_sr && post_sr_runtime_rejected) {
        ImGui::TextWrapped(
            ui(
                "Status: this signed NR runtime rejected the game's Post-SR "
                "guide contract (0x%08X); original SR is retained. Use NR -> SR instead.",
                "状态：当前签名 NR 运行库拒绝了游戏的 Post-SR guides 合同，"
                "错误码 0x%08X；已保留原始 SR，请改用 NR -> SR。"),
            last_create_result);
    } else if (evaluations == 0) {
        ImGui::Text("%s", ui(
            "Status: waiting for a compatible DLSS Super Resolution frame",
            "状态：等待兼容的 DLSS 超分帧"));
    } else {
        ImGui::Text(ui("Status: %s at %ux%u  |  NR frames: %llu",
                       "状态：%s，NR 分辨率 %u x %u  |  NR 帧数：%llu"),
                    before_sr ? "NR -> SR" : "SR -> NR",
                    width, height, evaluations);
    }
    if (before_sr && pre_sr_color_format == DXGI_FORMAT_R11G11B10_FLOAT) {
        ImGui::TextDisabled(
            "%s", ui(
                "UE5 R11G11B10 SceneColor conversion to FP16 is active.",
                "UE5 R11G11B10 SceneColor -> FP16 格式转换已启用。"));
    }
    ImGui::TextDisabled(
        ui("HDR exposure contract: PreExposure %.3f, ExposureScale %.3f, "
           "source -> canonical %.3f%s",
           "HDR 曝光合同：预曝光 %.3f，曝光缩放 %.3f，源 -> 标准 %.3f%s"),
        pre_exposure, exposure_scale, source_to_canonical,
        exposure_texture_present
            ? ui(" (exposure texture observed)", "（检测到曝光纹理）")
            : "");
    ImGui::TextDisabled(
        "%s", ui(
            "NR changes image detail by design. F6 off is the pure-SR "
            "reference; render-resolution NR -> SR may lose some fine detail. "
            "A large global color/exposure shift is not expected—please report it with the log.",
            "NR 本身会改变画面细节。F6 关闭是纯 SR 参考；渲染分辨率 "
            "NR -> SR 可能损失少量精细纹理；明显的全局颜色或曝光偏移不属于正常现象，"
            "请连同日志反馈。"));

    ImGui::SeparatorText(ui("Quick Compare / Image Tuning", "快速对比 / 画质调节"));
    changed |= ImGui::SliderFloat(
        ui("NR Intensity", "NR 强度"), &next.intensity, 0.0f, 2.0f, "%.2f");
    static const char *style_items_en[] = {"Default", "Natural", "Cinematic"};
    static const char *style_items_zh[] = {"默认", "自然", "电影"};
    const char *const *style_items = kChineseUi ? style_items_zh : style_items_en;
    int style = static_cast<int>(next.style);
    if (ImGui::Combo(ui("NR Style", "NR 风格"), &style, style_items, 3)) {
        next.style = static_cast<unsigned int>(style);
        changed = true;
    }
    changed |= ImGui::Checkbox(
        ui("Automatic Face / Skin Mask", "自动人脸 / 皮肤遮罩"),
        &next.auto_mask);
    changed |= ImGui::SliderFloat(
        ui("Skin Structure (-1 = follow Local Structure)",
           "皮肤结构（-1 = 跟随局部结构）"), &next.skin_structure,
        -1.0f, 2.0f, "%.2f");
    changed |= ImGui::SliderFloat(
        ui("Local Tone", "局部影调"), &next.local_tone, 0.0f, 2.0f, "%.2f");
    changed |= ImGui::SliderFloat(
        ui("Local Structure", "局部结构"), &next.local_structure,
        0.0f, 2.0f, "%.2f");
    if (ImGui::SliderFloat(
            ui("NR Detail Blend", "NR 细节混合强度"),
            &next.transfer_strength, 0.0f, 1.0f, "%.2f")) {
        next.post_transfer_strength = next.transfer_strength;
        changed = true;
    }
    ImGui::TextDisabled(
        "%s", ui(
            "This controls how far the frame moves toward the NR picture; it "
            "does not change render resolution and is shared by both chains.",
            "此项控制画面向 NR 结果靠近多少，不改变渲染分辨率；两条链路共用同一数值。"));
    changed |= ImGui::SliderFloat(
        ui("NR Color Follow", "NR 颜色跟随强度"),
        &next.color_strength, 0.0f, 1.0f, "%.2f");
    ImGui::TextDisabled(
        "%s", ui(
            "0 keeps the game's hue while retaining NR luminance detail; 1 "
            "also follows the model's color. Use 0 for the closest cross-mode match.",
            "0 保留游戏原始色相并保留 NR 明暗细节；1 同时跟随模型颜色。要让两条链路最接近请用 0。"));
    changed |= ImGui::Checkbox(
        ui("Experimental Lighting Match (NR -> SR)",
           "实验性光照匹配（NR -> SR）"),
        &next.preserve_lighting);
    ImGui::TextDisabled(
        "%s", ui(
            "A sparse local correction that can reduce broad exposure drift, "
            "but may alter faces and edges. Keep it off for the most faithful "
            "NR result; it only affects NR -> SR.",
            "稀疏局部修正可减轻整体曝光漂移，但可能改变人脸和边缘。若追求最忠实的 "
            "NR 结果请保持关闭；仅影响 NR -> SR。"));
    if (next.enabled && next.transfer_strength <= 0.001f) {
        ImGui::TextWrapped("%s", ui(
            next.identity_use_original
                ? "Diagnostic identity mode is active: NR still runs, but its "
                  "color is discarded."
                : "Blend is 0: Feature 18 is bypassed for a true native-SR "
                  "image and performance reference.",
            next.identity_use_original
                ? "诊断直通模式已启用：NR 仍会运行，但会丢弃其颜色结果。"
                : "混合强度为 0：已旁路 Feature 18，可作为真正的原生 SR "
                  "画面与性能基准。"));
    }

    changed |= ImGui::Checkbox(ui("UI Correction", "界面修正"),
                               &next.ui_correction);

    ImGui::SeparatorText(ui("Advanced / Recovery", "高级 / 故障恢复"));
    static const char *depth_items_en[] = {
        "Auto from DLSS", "Force normal", "Force inverted"};
    static const char *depth_items_zh[] = {
        "跟随游戏 DLSS", "强制普通深度", "强制反转深度"};
    const char *const *depth_items = kChineseUi ? depth_items_zh : depth_items_en;
    int depth_index = next.depth_mode + 1;
    if (ImGui::Combo(ui("Depth Mode", "深度模式"), &depth_index,
                     depth_items, 3)) {
        next.depth_mode = depth_index - 1;
        changed = true;
    }
    int warmup = static_cast<int>(next.warmup_after_create);
    if (ImGui::SliderInt(ui("Warm-up Frames", "预热帧数"),
                         &warmup, 0, 10)) {
        next.warmup_after_create = static_cast<unsigned int>(warmup);
        changed = true;
    }
    changed |= ImGui::Checkbox(
        ui("Retry original SR after a derived-path failure",
           "派生链路失败后重试原始 SR"),
        &next.retry_original_on_sr_failure);
    changed |= ImGui::Checkbox(
        ui("Diagnostic identity mode (discard NR color)",
           "诊断直通模式（丢弃 NR 颜色结果）"),
        &next.identity_use_original);

    if (ImGui::Button(ui("Restore original post-SR settings",
                         "恢复原始后置链路设置"))) {
        next.mode = 1;
        next.identity_use_original = false;
        next.post_transfer_strength = 1.0f;
        next.transfer_strength = 1.0f;
        next.color_strength = 0.0f;
        next.max_luma_ratio = 2.0f;
        next.preserve_lighting = false;
        next.proxy_scale = 1.0f;
        next.warmup_after_create = 2;
        next.retry_original_on_sr_failure = true;
        next.depth_mode = -1;
        next.intensity = 1.0f;
        next.local_tone = 1.0f;
        next.local_structure = 1.0f;
        next.skin_structure = -1.0f;
        next.auto_mask = false;
        next.style = 0;
        next.ui_correction = false;
        changed = true;
    }
    if (changed) ApplyConfig(next, "ReShade overlay");
}

} // namespace

#if NR_UI_CHINESE
extern "C" __declspec(dllexport) const char *NAME =
    "DLSS 5 AI渲染超分版@野生的装机宅bilibili";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "可切换 SR -> 输出分辨率 NR 与渲染分辨率 NR -> SR；包含 F6 总开关、"
    "三种风格和实时画质调节，输入分辨率严格跟随游戏 DLSS 挡位；检测到"
    "DLSS 光线重构时会严格原样旁路，避免双重神经重建。";
#else
extern "C" __declspec(dllexport) const char *NAME =
    "DLSS 5 AI Rendering Upscaling Edition @野生的装机宅bilibili";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Switches between output-resolution NR after Super Resolution and "
    "render-resolution NR before Super Resolution. Includes an F6 master "
    "switch, three styles and live image-tuning controls. Input resolution "
    "strictly follows the in-game DLSS quality mode. DLSS Ray Reconstruction "
    "is detected and passed through strictly unmodified to prevent double reconstruction.";
#endif

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        InitializeCriticalSection(&g_chain_cs);
        SetSiblingPath(module, L"nr-before-sr.log", g_log_path);
        SetSiblingPath(module, L"nr_before_sr.ini", g_ini_path);
        SetSiblingPath(module, L"ReShade.ini", g_reshade_ini_path);
        TruncateLog();
        Log("DLSS 5 AI Rendering Upscaling Edition attached (language=%s build %s %s)",
            kChineseUi ? "zh-CN" : "en", __DATE__, __TIME__);
        ReloadConfig(true);
        if (!reshade::register_addon(module)) {
            Log("ReShade add-on registration failed.");
            return FALSE;
        }
        reshade::register_event<reshade::addon_event::execute_command_list>(
            OnExecuteCommandList);
        reshade::register_event<reshade::addon_event::reset_command_list>(
            OnResetOrDestroyCommandList);
        reshade::register_event<reshade::addon_event::destroy_command_list>(
            OnResetOrDestroyCommandList);
        reshade::register_event<reshade::addon_event::present>(OnPresent);
        reshade::register_overlay(nullptr, DrawSettings);
        Log("Registered ReShade overlay and F6 hotkey (API %u).",
            static_cast<unsigned int>(RESHADE_API_VERSION));
        HANDLE worker = CreateThread(nullptr, 0, &HookWorker, nullptr, 0, nullptr);
        if (worker == nullptr) {
            Log("Could not start NGX hook worker (error %lu).", GetLastError());
            reshade::unregister_overlay(nullptr, DrawSettings);
            reshade::unregister_event<reshade::addon_event::present>(OnPresent);
            reshade::unregister_event<reshade::addon_event::destroy_command_list>(
                OnResetOrDestroyCommandList);
            reshade::unregister_event<reshade::addon_event::reset_command_list>(
                OnResetOrDestroyCommandList);
            reshade::unregister_event<reshade::addon_event::execute_command_list>(
                OnExecuteCommandList);
            reshade::unregister_addon(module);
            return FALSE;
        }
        CloseHandle(worker);
    } else if (reason == DLL_PROCESS_DETACH && reserved == nullptr) {
        reshade::unregister_overlay(nullptr, DrawSettings);
        reshade::unregister_event<reshade::addon_event::present>(OnPresent);
        reshade::unregister_event<reshade::addon_event::destroy_command_list>(
            OnResetOrDestroyCommandList);
        reshade::unregister_event<reshade::addon_event::reset_command_list>(
            OnResetOrDestroyCommandList);
        reshade::unregister_event<reshade::addon_event::execute_command_list>(
            OnExecuteCommandList);
        reshade::unregister_addon(module);
    }
    return TRUE;
}
