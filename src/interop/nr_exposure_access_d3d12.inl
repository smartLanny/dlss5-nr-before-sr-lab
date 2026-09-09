#pragma once
// NGX ExposureTexture contract: final exposure scale in red, mip 0, 1x1.
// Same-family FLOAT views and already typed R8_UNORM (normalized red).
// R8 TYPELESS/UINT/SNORM are not inferred or reinterpreted as exposure.
// Read-only access does not require source UAV support or source UAV flags.
DXGI_FORMAT NrExposureReadFormat(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R8_UNORM: return DXGI_FORMAT_R8_UNORM;
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_R16_FLOAT: return DXGI_FORMAT_R16_FLOAT;
    case DXGI_FORMAT_R16G16_TYPELESS:
    case DXGI_FORMAT_R16G16_FLOAT: return DXGI_FORMAT_R16G16_FLOAT;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_R32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R32G32_TYPELESS:
    case DXGI_FORMAT_R32G32_FLOAT: return DXGI_FORMAT_R32G32_FLOAT;
    case DXGI_FORMAT_R32G32B32_TYPELESS:
    case DXGI_FORMAT_R32G32B32_FLOAT: return DXGI_FORMAT_R32G32B32_FLOAT;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
    case DXGI_FORMAT_R32G32B32A32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

enum class NrExposureAccessReason { Ready, MissingDevice, Shape, DeniedSrv, Format, Capability };
struct NrExposureAccess {
    DXGI_FORMAT view = DXGI_FORMAT_UNKNOWN;
    NrExposureAccessReason reason = NrExposureAccessReason::MissingDevice;
    bool query_attempted = false;
    // A sentinel until query_attempted; never report this as a device failure.
    HRESULT query_result = E_INVALIDARG;
    unsigned support1 = 0, support2 = 0;
    bool allowed() const { return reason == NrExposureAccessReason::Ready; }
};
const char *NrExposureAccessReasonName(NrExposureAccessReason reason) {
    switch (reason) {
    case NrExposureAccessReason::Ready: return "ready";
    case NrExposureAccessReason::MissingDevice: return "missing-device";
    case NrExposureAccessReason::Shape: return "not-1x1-single-sample-mip0";
    case NrExposureAccessReason::DeniedSrv: return "resource-denies-srv";
    case NrExposureAccessReason::Format: return "no-supported-exposure-view";
    case NrExposureAccessReason::Capability: return "device-texture2d-or-shader-load-unavailable";
    }
    return "unknown";
}
NrExposureAccess NrResolveExposureAccess(ID3D12Device *device, const D3D12_RESOURCE_DESC &desc) {
    NrExposureAccess result;
    if (!device) return result;
    result.reason = NrExposureAccessReason::Shape;
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        desc.Width != 1 || desc.Height != 1 || desc.DepthOrArraySize != 1 ||
        desc.MipLevels < 1 || desc.SampleDesc.Count != 1) return result;
    result.reason = NrExposureAccessReason::DeniedSrv;
    if ((desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) != 0) return result;
    result.reason = NrExposureAccessReason::Format;
    result.view = NrExposureReadFormat(desc.Format);
    if (result.view == DXGI_FORMAT_UNKNOWN) return result;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
    support.Format = result.view;
    result.reason = NrExposureAccessReason::Capability;
    result.query_attempted = true;
    result.query_result = ID3D12Device_CheckFeatureSupport(device,
        D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support));
    result.support1 = static_cast<unsigned>(support.Support1);
    result.support2 = static_cast<unsigned>(support.Support2);
    if (SUCCEEDED(result.query_result) &&
        (support.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D) != 0 &&
        (support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD) != 0)
        result.reason = NrExposureAccessReason::Ready;
    return result;
}
#if defined(NR_HAS_NATIVE_FORMAT_OBSERVER)
#include "../nr_exposure_semantic_evidence.inl"
#endif
