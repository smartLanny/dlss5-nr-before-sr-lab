#pragma once

namespace nr_dx11_observer_status {

// The D3D12 NGX observer can be fully hooked while a presenting D3D11
// swapchain never calls NVSDK_NGX_D3D12_CreateFeature. That is not
// waiting-feature1: Feature 1 cannot arrive on the D3D12 exports.
enum class PresentingApi : int {
    Unknown = 0,
    D3d11 = 1,
    D3d12 = 2,
    Other = 3,
};

enum class Class : int {
    NotDx11 = 0,
    WaitingForHooks,
    D3d12CarrierPresent,
    Dx11Feature1Unbridged,
    Dx11PresentNoD3d12Carrier,
};

struct Sample {
    PresentingApi presenting = PresentingApi::Unknown;
    bool d3d12_hooks_ready = false;
    unsigned long long d3d12_feature1_success = 0;
    unsigned long long d3d12_carrier_evals = 0;
    unsigned long long d3d11_feature1_success = 0;
    unsigned long long d3d11_feature1_evals = 0;
};

inline Class Classify(const Sample &sample) noexcept {
    if (sample.presenting != PresentingApi::D3d11) return Class::NotDx11;
    if (sample.d3d12_feature1_success != 0 || sample.d3d12_carrier_evals != 0)
        return Class::D3d12CarrierPresent;
    if (!sample.d3d12_hooks_ready) return Class::WaitingForHooks;
    if (sample.d3d11_feature1_success != 0 || sample.d3d11_feature1_evals != 0)
        return Class::Dx11Feature1Unbridged;
    return Class::Dx11PresentNoD3d12Carrier;
}

inline const char *PresentingApiName(PresentingApi api) noexcept {
    switch (api) {
    case PresentingApi::D3d11: return "d3d11";
    case PresentingApi::D3d12: return "d3d12";
    case PresentingApi::Other: return "other";
    case PresentingApi::Unknown:
    default: return "unknown";
    }
}

inline const char *ClassName(Class value) noexcept {
    switch (value) {
    case Class::WaitingForHooks: return "waiting-hooks";
    case Class::D3d12CarrierPresent: return "d3d12-carrier";
    case Class::Dx11Feature1Unbridged: return "dx11-feature1-unbridged";
    case Class::Dx11PresentNoD3d12Carrier: return "dx11-present-no-d3d12-carrier";
    case Class::NotDx11:
    default: return "not-dx11";
    }
}

}  // namespace nr_dx11_observer_status
