#pragma once
// Direct public FFX 3.1 -> NGX SR mapping. No OptiScaler or third-party NR.
// SDK headers stay external; see docs/interop/DIRECT_SR_20260908.md for pins.
#include <algorithm>
#include <cmath>
#include <limits>
#include <d3d12.h>
#include <wrl/client.h>
#include <ffx_api/ffx_upscale.h>
#include <ffx_api/dx12/ffx_api_dx12.h>
#include <nvsdk_ngx_helpers.h>

namespace nr_fsr31_dlss {
enum class Status {
    Ready, MissingInput, UnsupportedExtension, UnsupportedFlags,
    UnsupportedMask, InvalidExtent, InvalidTemporalInput,
    UnsupportedResource, DeviceMismatch, AliasedOutput, UnsupportedState, QualityUnavailable,
};
inline const char *Name(Status s) {
    switch (s) {
    case Status::Ready: return "ready";
    case Status::MissingInput: return "missing-input";
    case Status::UnsupportedExtension: return "unsupported-extension";
    case Status::UnsupportedFlags: return "unsupported-flags-or-colour-domain";
    case Status::UnsupportedMask: return "reactive-or-transparency-mask-unmapped";
    case Status::InvalidExtent: return "invalid-active-or-output-extent";
    case Status::InvalidTemporalInput: return "invalid-jitter-motion-or-exposure";
    case Status::UnsupportedResource: return "unsupported-resource-contract";
    case Status::DeviceMismatch: return "device-mismatch";
    case Status::AliasedOutput: return "output-aliases-input";
    case Status::UnsupportedState: return "unsupported-resource-state";
    case Status::QualityUnavailable: return "no-dlss-quality-range-for-input";
    }
    return "unknown";
}
struct Mapping {
    NVSDK_NGX_DLSS_Create_Params create{};
    NVSDK_NGX_D3D12_DLSS_Eval_Params evaluate{};
};
struct Quality {
    NVSDK_NGX_PerfQuality_Value mode = NVSDK_NGX_PerfQuality_Value_MaxQuality;
    unsigned min_width = 0, min_height = 0, max_width = 0, max_height = 0;
};
inline bool Covers(const Quality &q, const ffxCreateContextDescUpscale &c,
                   const ffxDispatchDescUpscale &d) {
    return q.mode >= NVSDK_NGX_PerfQuality_Value_MaxPerf && q.mode <= NVSDK_NGX_PerfQuality_Value_DLAA &&
        q.min_width && q.min_height && q.max_width >= q.min_width && q.max_height >= q.min_height &&
        d.renderSize.width >= q.min_width && d.renderSize.height >= q.min_height &&
        d.renderSize.width <= c.maxRenderSize.width && d.renderSize.height <= c.maxRenderSize.height &&
        c.maxRenderSize.width <= q.max_width && c.maxRenderSize.height <= q.max_height;
}
// Serialize calls using a capability parameter block. Range results come from
// the loaded DLSS implementation, not a ratio guessed from an FSR mode name.
inline Status SelectQuality(NVSDK_NGX_Parameter *capabilities,
                            const ffxCreateContextDescUpscale &c,
                            const ffxDispatchDescUpscale &d, Quality &out) {
    out = {};
    if (!capabilities || !c.maxUpscaleSize.width || !c.maxUpscaleSize.height ||
        c.maxUpscaleSize.width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        c.maxUpscaleSize.height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION)
        return Status::QualityUnavailable;
    const NVSDK_NGX_PerfQuality_Value modes[] = {
        NVSDK_NGX_PerfQuality_Value_DLAA, NVSDK_NGX_PerfQuality_Value_MaxQuality,
        NVSDK_NGX_PerfQuality_Value_Balanced, NVSDK_NGX_PerfQuality_Value_MaxPerf,
        NVSDK_NGX_PerfQuality_Value_UltraPerformance,
    };
    double best = std::numeric_limits<double>::infinity();
    for (auto mode : modes) {
        unsigned optimal_w = 0, optimal_h = 0;
        Quality q{}; q.mode = mode;
        float unused_sharpness = 0;
        const auto result = NGX_DLSS_GET_OPTIMAL_SETTINGS(capabilities,
            c.maxUpscaleSize.width, c.maxUpscaleSize.height, mode,
            &optimal_w, &optimal_h, &q.max_width, &q.max_height,
            &q.min_width, &q.min_height, &unused_sharpness);
        if (NVSDK_NGX_FAILED(result) || !optimal_w || !optimal_h || !Covers(q, c, d)) continue;
        const double dx = static_cast<double>(d.renderSize.width) - optimal_w;
        const double dy = static_cast<double>(d.renderSize.height) - optimal_h;
        const double score = dx * dx + dy * dy;
        if (score < best) { best = score; out = q; }
    }
    return std::isfinite(best) ? Status::Ready : Status::QualityUnavailable;
}

inline bool SameDevice(ID3D12Device *a, ID3D12Device *b) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IUnknown> left, right;
    return a && b && SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&left))) &&
        SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&right))) && left.Get() == right.Get();
}

inline Status Resource(ID3D12Device *device, const FfxApiResource &view,
                       unsigned width, unsigned height, DXGI_FORMAT format,
                       unsigned ffx_format, bool output = false) {
    auto *r = static_cast<ID3D12Resource *>(view.resource);
    if (!r) return Status::MissingInput;
    const auto d = r->GetDesc();
    if (d.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        d.MipLevels != 1 || d.DepthOrArraySize != 1 || d.SampleDesc.Count != 1 ||
        d.Format != format || d.Width < width || d.Height < height ||
        view.description.type != FFX_API_RESOURCE_TYPE_TEXTURE2D ||
        view.description.width != d.Width || view.description.height != d.Height ||
        view.description.mipCount != 1 || view.description.depth != 1 ||
        view.description.format != ffx_format ||
        (d.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) ||
        (output && (!(d.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) ||
                    d.Width != width || d.Height != height)))
        return Status::UnsupportedResource;
    Microsoft::WRL::ComPtr<ID3D12Device> owner;
    if (FAILED(r->GetDevice(IID_PPV_ARGS(&owner))) || !SameDevice(device, owner.Get()))
        return Status::DeviceMismatch;
    // This first adapter records no guessed transitions. The caller supplies
    // the FFX compute-read/UAV boundary on its actual command list.
    const bool readable = view.state == FFX_API_RESOURCE_STATE_COMPUTE_READ ||
        view.state == FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ;
    if (output ? view.state != FFX_API_RESOURCE_STATE_UNORDERED_ACCESS : !readable)
        return Status::UnsupportedState;
    return Status::Ready;
}

inline Status Map(ID3D12Device *device, const ffxCreateContextDescUpscale &c,
                  const ffxDispatchDescUpscale &d, Mapping &output, const Quality &quality) {
    output = {};
    if (!device || !d.commandList || !d.color.resource || !d.depth.resource ||
        !d.motionVectors.resource || !d.output.resource) return Status::MissingInput;
    if (c.header.type != FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE ||
        d.header.type != FFX_API_DISPATCH_DESC_TYPE_UPSCALE || d.header.pNext)
        return Status::UnsupportedExtension;
    if (c.header.pNext) {
        const auto *backend_header = c.header.pNext;
        if (backend_header->type != FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12 ||
            backend_header->pNext) return Status::UnsupportedExtension;
        const auto *backend = reinterpret_cast<const ffxCreateBackendDX12Desc *>(backend_header);
        if (!SameDevice(device, backend->device)) return Status::DeviceMismatch;
    }
    constexpr unsigned supported = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE |
        FFX_UPSCALE_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS |
        FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION |
        FFX_UPSCALE_ENABLE_DEPTH_INVERTED | FFX_UPSCALE_ENABLE_DEPTH_INFINITE |
        FFX_UPSCALE_ENABLE_AUTO_EXPOSURE | FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION;
    if ((c.flags & ~supported) || d.flags || d.enableSharpening)
        return Status::UnsupportedFlags;
    // FSR reactivity and NGX BiasCurrentColor are not silently equated.
    if (d.reactive.resource || d.transparencyAndComposition.resource)
        return Status::UnsupportedMask;
    const unsigned rw = d.renderSize.width, rh = d.renderSize.height;
    const unsigned ow = d.upscaleSize.width ? d.upscaleSize.width : c.maxUpscaleSize.width;
    const unsigned oh = d.upscaleSize.height ? d.upscaleSize.height : c.maxUpscaleSize.height;
    if (!rw || !rh || !ow || !oh || rw > c.maxRenderSize.width || rh > c.maxRenderSize.height ||
        c.maxRenderSize.width > ow || c.maxRenderSize.height > oh ||
        ow != c.maxUpscaleSize.width || oh != c.maxUpscaleSize.height ||
        ow > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION || oh > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        ((d.upscaleSize.width == 0) != (d.upscaleSize.height == 0)))
        return Status::InvalidExtent;
    if (!std::isfinite(d.jitterOffset.x) || !std::isfinite(d.jitterOffset.y) ||
        !std::isfinite(d.motionVectorScale.x) || !std::isfinite(d.motionVectorScale.y) ||
        d.motionVectorScale.x == 0 || d.motionVectorScale.y == 0 ||
        !std::isfinite(d.preExposure) || d.preExposure <= 0 ||
        !std::isfinite(d.frameTimeDelta) || d.frameTimeDelta <= 0)
        return Status::InvalidTemporalInput;
    if (!Covers(quality, c, d)) return Status::QualityUnavailable;
    if (d.output.resource == d.color.resource || d.output.resource == d.depth.resource ||
        d.output.resource == d.motionVectors.resource || d.output.resource == d.exposure.resource)
        return Status::AliasedOutput;
    Microsoft::WRL::ComPtr<ID3D12Device> command_device;
    auto *command = static_cast<ID3D12GraphicsCommandList *>(d.commandList);
    if (command->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT ||
        FAILED(command->GetDevice(IID_PPV_ARGS(&command_device))) ||
        !SameDevice(device, command_device.Get())) return Status::DeviceMismatch;
    const bool high_mv = (c.flags & FFX_UPSCALE_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS) != 0;
    const Status checks[] = {
        Resource(device, d.color, rw, rh, DXGI_FORMAT_R16G16B16A16_FLOAT, FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT),
        Resource(device, d.depth, rw, rh, DXGI_FORMAT_R32_FLOAT, FFX_API_SURFACE_FORMAT_R32_FLOAT),
        Resource(device, d.motionVectors, high_mv ? ow : rw, high_mv ? oh : rh,
                 DXGI_FORMAT_R16G16_FLOAT, FFX_API_SURFACE_FORMAT_R16G16_FLOAT),
        Resource(device, d.output, ow, oh, DXGI_FORMAT_R16G16B16A16_FLOAT, FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT, true),
    };
    for (auto status : checks) if (status != Status::Ready) return status;
    if (d.exposure.resource) {
        auto s = Resource(device, d.exposure, 1, 1, DXGI_FORMAT_R32_FLOAT, FFX_API_SURFACE_FORMAT_R32_FLOAT);
        if (s != Status::Ready) return s;
        if (d.exposure.description.width != 1 || d.exposure.description.height != 1)
            return Status::UnsupportedResource;
    }
    Mapping next{};
    next.create.Feature.InWidth = c.maxRenderSize.width;
    next.create.Feature.InHeight = c.maxRenderSize.height;
    next.create.Feature.InTargetWidth = ow;
    next.create.Feature.InTargetHeight = oh;
    next.create.Feature.InPerfQualityValue = quality.mode;
    int flags = high_mv ? 0 : NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
    if (c.flags & FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE) flags |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;
    if (c.flags & FFX_UPSCALE_ENABLE_DEPTH_INVERTED) flags |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
    if (c.flags & FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION) flags |= NVSDK_NGX_DLSS_Feature_Flags_MVJittered;
    if (c.flags & FFX_UPSCALE_ENABLE_AUTO_EXPOSURE) flags |= NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
    next.create.InFeatureCreateFlags = flags;
    auto &e = next.evaluate;
    e.Feature.pInColor = static_cast<ID3D12Resource *>(d.color.resource);
    e.Feature.pInOutput = static_cast<ID3D12Resource *>(d.output.resource);
    e.pInDepth = static_cast<ID3D12Resource *>(d.depth.resource);
    e.pInMotionVectors = static_cast<ID3D12Resource *>(d.motionVectors.resource);
    e.pInExposureTexture = static_cast<ID3D12Resource *>(d.exposure.resource);
    e.InJitterOffsetX = d.jitterOffset.x; e.InJitterOffsetY = d.jitterOffset.y;
    e.InMVScaleX = d.motionVectorScale.x; e.InMVScaleY = d.motionVectorScale.y;
    e.InRenderSubrectDimensions = {rw, rh};
    e.InReset = d.reset ? 1 : 0;
    e.InPreExposure = d.preExposure;
    e.InExposureScale = 1.0f;
    e.InFrameTimeDeltaInMsec = d.frameTimeDelta;
    output = next;
    return Status::Ready;
}
} // namespace nr_fsr31_dlss
