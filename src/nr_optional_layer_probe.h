#pragma once

// Frozen key-name contract, not a promise that an incoming NGX call supplies
// these layers. No engine/Streamline inventory, resource ownership or GPU use.
// NR source: nvngx_dlssnr.dll 310.8.0.0, 165840496 bytes, SHA-256
// e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e.
// Exact NUL-terminated ASCII file offsets (resource; x,y,w,h):
// UI:          0xae0a8; 0xae0b8,0xae0d0,0xae0e8,0xae100
// UIAlpha:     0xae118; 0xae128,0xae148,0xae168,0xae188
// ControlMask: 0xae010; 0xae028,0xae048,0xae068,0xae088
// RR source: NVIDIA Streamline SDK v2.12.0/external/ngx-sdk/include:
// nvsdk_ngx_defs.h SHA-256 a9481cbc78d53ee56639f0a573ec095c6c1ea1e5f296d139c20c42bf342c2e1b
// nvsdk_ngx_defs_dlssd.h SHA-256 019036e5157c82431cb6aba7c0ff76110dae196300a3ea211d3b75376e137e1d
// nvsdk_ngx_helpers_dlssd.h SHA-256 be5ecfc8f5ecb17861600485ce9fb617dc56faa5f79ee1525081a54bf4281475
// Public source: https://github.com/NVIDIAGameWorks/Streamline/tree/v2.12.0/external/ngx-sdk/include
// Definitions: defs.h:769,771,775,798; defs_dlssd.h:57-67,92,96,138-147.
// D3D12 bindings: helpers_dlssd.h:592-593,615-626,647,651,690-699.
// RR's albedo keys are DLSS.Input.*, not the older GBuffer aliases.
// Missing RR subrect width/height keys are not invented; only their defined
// bases are queried. Packed roughness is separately observable via its mode.
namespace nr_optional_layer_probe {
inline constexpr const char *kContract = "nr3108-sdk2120-optional-layers-v1";
inline constexpr unsigned kReportBudget = 6144;
struct Layer {
    const char *key;
    const char *rect[4]; // x, y, width, height; null means not in this contract.
};
inline constexpr Layer kLayers[] = {
    {"DLSSNR.UI", {"DLSSNR.UISubrectBaseX", "DLSSNR.UISubrectBaseY", "DLSSNR.UISubrectWidth", "DLSSNR.UISubrectHeight"}},
    {"DLSSNR.UIAlpha", {"DLSSNR.UIAlphaSubrectBaseX", "DLSSNR.UIAlphaSubrectBaseY", "DLSSNR.UIAlphaSubrectWidth", "DLSSNR.UIAlphaSubrectHeight"}},
    {"DLSSNR.ControlMask", {"DLSSNR.ControlMaskSubrectBaseX", "DLSSNR.ControlMaskSubrectBaseY", "DLSSNR.ControlMaskSubrectWidth", "DLSSNR.ControlMaskSubrectHeight"}},
    {"GBuffer.Normals", {"DLSS.Input.Normals.Subrect.Base.X", "DLSS.Input.Normals.Subrect.Base.Y", nullptr, nullptr}},
    {"GBuffer.Roughness", {"DLSS.Input.Roughness.Subrect.Base.X", "DLSS.Input.Roughness.Subrect.Base.Y", nullptr, nullptr}},
    {"DLSS.Input.DiffuseAlbedo", {"DLSS.Input.DiffuseAlbedo.Subrect.Base.X", "DLSS.Input.DiffuseAlbedo.Subrect.Base.Y", nullptr, nullptr}},
    {"DLSS.Input.SpecularAlbedo", {"DLSS.Input.SpecularAlbedo.Subrect.Base.X", "DLSS.Input.SpecularAlbedo.Subrect.Base.Y", nullptr, nullptr}},
    {"GBuffer.SpecularMvec", {nullptr, nullptr, nullptr, nullptr}},
    {"DLSSD.SpecularHitDistance", {"DLSSD.SpecularHitDistance.Subrect.Base.X", "DLSSD.SpecularHitDistance.Subrect.Base.Y", nullptr, nullptr}},
    {"DLSSD.SpecularRayDirectionHitDistance", {"DLSSD.SpecularRayDirectionHitDistance.Subrect.Base.X", "DLSSD.SpecularRayDirectionHitDistance.Subrect.Base.Y", nullptr, nullptr}},
    {"RayTracingHitDistance", {nullptr, nullptr, nullptr, nullptr}},
};
inline constexpr const char *kRoughnessMode = "DLSS.Roughness.Mode";
enum class QueryState { MissingGetter, Error, Null, Present };
struct Query {
    QueryState state = QueryState::MissingGetter;
    unsigned result = 0;
    bool Present() const { return state == QueryState::Present; }
};
inline const char *QueryName(QueryState state) {
    switch (state) {
    case QueryState::MissingGetter: return "missing-getter";
    case QueryState::Error: return "getter-error";
    case QueryState::Null: return "success-null";
    case QueryState::Present: return "success-present";
    }
    return "unknown";
}
template<class Getter, class Params, class Resource>
Query ReadPointer(Getter getter, const Params *params, const char *key,
                  unsigned success, Resource *&value) {
    value = nullptr;
    if (getter == nullptr) return {};
    const unsigned result = getter(params, key, &value);
    if (result != success) {
        value = nullptr; // Even a failed getter may have written a poison value.
        return {QueryState::Error, result};
    }
    return {value != nullptr ? QueryState::Present : QueryState::Null, result};
}
template<class Getter, class Params>
Query ReadUnsigned(Getter getter, const Params *params, const char *key,
                   unsigned success, unsigned &value) {
    value = 0;
    if (getter == nullptr) return {};
    const unsigned result = getter(params, key, &value);
    if (result != success) { value = 0; return {QueryState::Error, result}; }
    return {QueryState::Present, result}; // Zero is a supplied scalar, not null.
}
} // namespace nr_optional_layer_probe
