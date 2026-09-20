#pragma once
#include <cmath>
#include <atomic>

namespace nr_rr_depth {
// NGX RR Guide: Linear is view-space depth; matrices are row-major and
// left-multiplied. Both belong to the same Evaluate/camera coordinate system.
// Support the standard positive-view-Z perspective form only. Do not infer
// metres, near distance, layout or inversion from the executable name.
struct Projection { float a = 0, b = 0; bool inverted = false; };
inline bool Resolve(const float *m, bool inverted, Projection &out) {
    if (!m) return false;
    for (unsigned i=0;i<16;++i) if (!std::isfinite(m[i])) return false;
    // clip Z/W must depend only on view Z. Off-axis X/Y jitter is harmless;
    // oblique depth, orthographic and negative-Z projections stay native.
    if (m[2]!=0 || m[6]!=0 || m[3]!=0 || m[7]!=0 || m[15]!=0 ||
        m[11]<=0 || m[0]<=0 || m[5]<=0) return false;
    const float a=m[10]/m[11], b=m[14]/m[11];
    if (!std::isfinite(a) || !std::isfinite(b) || b==0) return false;
    if (inverted ? (a>0 || b<=0) : (a<1 || b>=0)) return false;
    const float near_z=inverted ? b/(1-a) : -b/a;
    if (!std::isfinite(near_z) || near_z<=0) return false;
    const float far_denominator=inverted ? -a : a-1;
    if (far_denominator>0) {
        const float far_z=std::fabs(b)/far_denominator;
        if (!std::isfinite(far_z) || far_z<=near_z) return false;
    }
    out={a,b,inverted}; return true;
}
inline bool Resolve(const float *m, Projection &out) {
    // Linear RR input has no hardware-depth direction. Derive the NEW private
    // hardware depth's direction from the supplied camera projection.
    return m && Resolve(m, m[10] <= 0, out);
}
inline float Convert(const Projection &p, float view_z) {
    const float far_depth=p.inverted?0.f:1.f;
    if (!std::isfinite(view_z) || view_z<=0) return far_depth;
    const float value=p.a+p.b/view_z;
    if (!std::isfinite(value)) return far_depth;
    return value<0?0:value>1?1:value;
}

// Current Evaluate only. Missing data is distinct from a copied but unsupported
// projection; neither permits guessing units, a near plane or matrix layout.
enum class InputState : unsigned { Unobserved, Absent, Unreadable, Nonfinite, Supported, Unsupported };
inline const char* Name(InputState s) {
    switch(s) {
    case InputState::Unobserved:return "unobserved";
    case InputState::Absent:return "missing-current-matrix";
    case InputState::Unreadable:return "unreadable-current-matrix";
    case InputState::Nonfinite:return "nonfinite-current-matrix";
    case InputState::Supported:return "supported-current-projection";
    case InputState::Unsupported:return "unsupported-current-projection";
    }
    return "unknown";
}
struct CurrentInput { InputState state=InputState::Absent; float matrix[16]{}; Projection projection{}; };
template<class GetPointer, class Copy>
CurrentInput ReadCurrent(GetPointer get, Copy copy) {
    CurrentInput input; void* pointer=nullptr;
    if(!get(pointer)||!pointer)return input;
    if(!copy(pointer,input.matrix,sizeof(input.matrix))) { input.state=InputState::Unreadable;return input; }
    for(float value:input.matrix)if(!std::isfinite(value)){input.state=InputState::Nonfinite;return input;}
    input.state=Resolve(input.matrix,input.projection)?InputState::Supported:InputState::Unsupported;
    return input;
}
inline bool UsePlaceholder(InputState state,bool requested) {
    // An explicit experiment for absent camera data ONLY. Corrupt/unreadable or
    // unsupported supplied matrices keep the native RR bypass even when ON.
    return requested&&state==InputState::Absent;
}
struct Observations {
    std::atomic<InputState> input{InputState::Unobserved};
    std::atomic<unsigned long long> admitted{0}, placeholder_admitted{0}, placeholder_recorded{0};
};
inline Observations observations;
} // namespace nr_rr_depth
