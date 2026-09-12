#pragma once
#include <cstdint>
#include <array>

// DXGI ABI numbers. No Windows dependency: the same policy is exercised by
// host tests and the D3D12 integration. This is a format FAMILY table, not a
// game/GPU whitelist. Device capabilities are supplied by the caller.
namespace nr_format_access {
using Format = std::uint32_t;
constexpr Format Unknown=0, Rgba32Typeless=1, Rgba32Float=2,
    Rgba32Uint=3, Rgba32Sint=4, Rgba16Typeless=9, Rgba16Float=10,
    Rgba16Unorm=11, Rgba16Uint=12, Rgba16Snorm=13, Rgba16Sint=14,
    Rgb10a2Typeless=23, Rgb10a2Unorm=24, Rgb10a2Uint=25,
    R11g11b10Float=26, Rgba8Typeless=27, Rgba8Unorm=28,
    Rgba8Srgb=29, Rgba8Uint=30, Rgba8Snorm=31, Rgba8Sint=32,
    Bgra8Unorm=87, Bgrx8Unorm=88, Bgra8Typeless=90, Bgra8Srgb=91,
    Bgrx8Typeless=92, Bgrx8Srgb=93;
enum class Family { Unknown, Rgba32, Rgba16, Rgb10a2, Rgb11, Rgba8, Bgra8, Bgrx8 };
enum class Usage { ShaderLoad, TypedStore };
enum class Provenance { None, ExactTyped, ObservedView, FloatInterpretation };
enum class Status { Ready, AmbiguousType, UnknownLayout, IllegalView, IntegerShaderRequired,
                    DeniedRead, UnsupportedRead, WritebackUnavailable };
struct Candidates { std::array<Format,6> values{}; unsigned count=0; };
inline Family GetFamily(Format f) noexcept {
    if (f>=1 && f<=4) return Family::Rgba32;
    if (f>=9 && f<=14) return Family::Rgba16;
    if (f>=23 && f<=25) return Family::Rgb10a2;
    if (f==26) return Family::Rgb11;
    if (f>=27 && f<=32) return Family::Rgba8;
    if (f==87 || f==90 || f==91) return Family::Bgra8;
    if (f==88 || f==92 || f==93) return Family::Bgrx8;
    return Family::Unknown;
}
inline bool IsTypeless(Format f) noexcept {
    return f==1 || f==9 || f==23 || f==27 || f==90 || f==92;
}
inline bool IsInteger(Format f) noexcept {
    return f==3 || f==4 || f==12 || f==14 || f==25 || f==30 || f==32;
}
inline bool IsSrgb(Format f) noexcept { return f==29 || f==91 || f==93; }
inline bool IsFloatColor(Format f) noexcept { return f==2 || f==10 || f==26; }
inline Candidates FamilyViews(Format f) noexcept {
    switch(GetFamily(f)) {
    case Family::Rgba32: return {{{2,3,4}},3};
    case Family::Rgba16: return {{{10,11,12,13,14}},5};
    case Family::Rgb10a2: return {{{24,25}},2};
    case Family::Rgb11: return {{{26}},1};
    case Family::Rgba8: return {{{28,29,30,31,32}},5};
    case Family::Bgra8: return {{{87,91}},2};
    case Family::Bgrx8: return {{{88,93}},2};
    default: return {};
    }
}
inline bool LegalView(Format resource, Format view) noexcept {
    if (!view || IsTypeless(view)) return false;
    // A fully typed resource is NOT an invitation to reinterpret arbitrary
    // family members. In particular, typed sRGB is not writable as UNORM.
    if (!IsTypeless(resource)) return resource==view;
    auto list=FamilyViews(resource);
    for(unsigned i=0;i<list.count;++i) if(list.values[i]==view) return true;
    return false;
}
struct Request {
    Format resource=Unknown;
    Format observed_view=Unknown; // provider-known SRV format, not a guess
    bool permit_float_interpretation=false;
    bool deny_shader_resource=false;
    bool allow_unordered_access=false;
    bool needs_inplace_store=false;
    bool equivalent_copyback_available=false;
};
struct Plan {
    Status status=Status::AmbiguousType;
    Format srv=Unknown, uav=Unknown;
    Provenance provenance=Provenance::None;
    bool use_canonical=true;
    bool copyback_required=false;
    explicit operator bool() const noexcept { return status==Status::Ready; }
};
// Probe must answer capabilities for the actual device and the exact typed
// view. A failed query is NOT cached as process-wide incompatibility.
template<class Probe>
Plan Resolve(const Request& r, Probe&& probe) {
    Plan p;
    if(r.deny_shader_resource) { p.status=Status::DeniedRead; return p; }
    if(r.observed_view) {
        if(!LegalView(r.resource,r.observed_view)) { p.status=Status::IllegalView; return p; }
        p.srv=r.observed_view; p.provenance=Provenance::ObservedView;
    } else if(!IsTypeless(r.resource) && r.resource) {
        p.srv=r.resource; p.provenance=Provenance::ExactTyped;
    } else if(r.permit_float_interpretation) {
        // Temporal SceneColor may be given as typeless FLOAT storage. Opting
        // into FLOAT never substitutes UNORM/UINT if the FLOAT probe fails.
        // Those would produce different values, not a hardware fallback.
        auto list=FamilyViews(r.resource);
        for(unsigned i=0;i<list.count;++i) if(IsFloatColor(list.values[i])) {
            p.srv=list.values[i]; break;
        }
        if(p.srv) p.provenance=Provenance::FloatInterpretation;
    }
    if(!p.srv) return p;
    if(GetFamily(p.srv)==Family::Unknown) { p.status=Status::UnknownLayout; return p; }
    if(IsInteger(p.srv)) { p.status=Status::IntegerShaderRequired; return p; }
    if(!probe(p.srv,Usage::ShaderLoad)) { p.status=Status::UnsupportedRead; return p; }
    if(!r.needs_inplace_store) {
        // Pre-SR substitutes an owned FP16 result. The source never needs UAV.
        p.status=Status::Ready; return p;
    }
    Format write=p.srv;
    if(IsSrgb(write) && IsTypeless(r.resource)) {
        // The caller must encode sRGB explicitly before this UNORM store.
        write=(write==Rgba8Srgb?Rgba8Unorm:write==Bgra8Srgb?Bgra8Unorm:Bgrx8Unorm);
    }
    if(r.allow_unordered_access && !IsSrgb(write) && LegalView(r.resource,write) &&
       probe(write,Usage::TypedStore)) {
        p.uav=write; p.status=Status::Ready; return p;
    }
    // Reading is insufficient for post-SR: the caller must also possess a
    // same-format encode/copyback route with legal destination state/lifetime.
    if(r.equivalent_copyback_available) {
        p.copyback_required=true; p.status=Status::Ready; return p;
    }
    p.status=Status::WritebackUnavailable; return p;
}
inline const char* StatusName(Status s) noexcept {
    switch(s) {
    case Status::Ready:return "ready";
    case Status::AmbiguousType:return "typed-interpretation-required";
    case Status::UnknownLayout:return "format-layout-adapter-required";
    case Status::IllegalView:return "illegal-resource-view-pair";
    case Status::IntegerShaderRequired:return "integer-conversion-shader-required";
    case Status::DeniedRead:return "shader-read-denied";
    case Status::UnsupportedRead:return "device-shader-load-unavailable";
    case Status::WritebackUnavailable:return "post-writeback-unavailable";
    }
    return "unknown";
}
} // namespace nr_format_access
