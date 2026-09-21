#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

// This policy is also used by the concrete Core/Present adapter. It does not
// manufacture guides or claim that an API return proves a displayed image.
namespace nr_compat_present {
enum class Encoding : unsigned { Unknown, Srgb, ScRgb, Pq2020 };
enum class Status : unsigned {
    Off, Switching, AwaitingConsent, MissingNativeGuides, SourceUnavailable,
    UnsupportedApi, UnsupportedSurface, WrongDeviceOrQueue, OtherNrOwner,
    Budget, Preparing, Creating, Busy, Submitted, Completed, Draining,
    BuildFailure, Poisoned, RetryBackoff, FgBoundaryOwned
};
inline const char* Name(Status s) noexcept {
    switch (s) {
    case Status::Off:return "off"; case Status::Switching:return "switching";
    case Status::AwaitingConsent:return "color-only-consent-required";
    case Status::MissingNativeGuides:return "native-present-guide-transport-unavailable";
    case Status::SourceUnavailable:return "current-source-frame-unverified";
    case Status::UnsupportedApi:return "native-D3D12-required";
    case Status::UnsupportedSurface:return "unsupported-present-surface";
    case Status::WrongDeviceOrQueue:return "presentation-device-or-queue-changed";
    case Status::OtherNrOwner:return "other-NR-owner"; case Status::Budget:return "allocation-budget";
    case Status::Preparing:return "preparing"; case Status::Creating:return "create-fence-pending";
    case Status::Busy:return "recording-slots-in-flight"; case Status::Submitted:return "submitted-not-displayed-proof";
    case Status::Completed:return "GPU-completed-not-displayed-proof";
    case Status::Draining:return "retiring-old-generation";
    case Status::BuildFailure:return "resource-or-shader-build-failed";
    case Status::Poisoned:return "quarantined-restart-required";
    case Status::RetryBackoff:return "waiting-for-source-frame-retry";
    case Status::FgBoundaryOwned:return "FG-owns-Present; final-surface-NR-skipped";
    }
    return "invalid";
}
struct Options {
    bool selected=false;
    bool allow_color_only=false;
    bool fg_off_confirmed=false; // Manual experiment constraint, NOT measured evidence.
    unsigned percent=100;
};
inline bool Valid(Options o) noexcept {return o.percent>=25 && o.percent<=100;}
struct Extent {unsigned width=0,height=0;};
inline bool operator==(Extent a,Extent b) noexcept {return a.width==b.width&&a.height==b.height;}
inline bool Valid(Extent e) noexcept {return e.width&&e.height&&e.width<=16384&&e.height<=16384;}
inline Extent WorkExtent(Extent s,unsigned percent) noexcept {
    if(!Valid(s)||percent<25||percent>100)return {};
    if(percent==100)return s;
    return {std::max(1u,unsigned((std::uint64_t(s.width)*percent+50)/100)),
            std::max(1u,unsigned((std::uint64_t(s.height)*percent+50)/100))};
}
inline bool Fits(std::uint64_t used,std::uint64_t add,std::uint64_t cap) noexcept {
    return add && add!=UINT64_MAX && used<=cap && add<=cap-used;
}
struct Gate {
    bool master=false, selected=false, finite_strength=false, nonzero_strength=false;
    bool application_present=false, full_rect=false, only_swapchain=false;
    bool d3d12=false, color_known=false, device_and_queue=false;
    bool other_nr=false, hard_fault=false, prior_inline_drained=false;
    bool source_verified=false;
};
inline Status Admit(const Gate& g) noexcept {
    if(!g.master||!g.selected||!g.finite_strength||!g.nonzero_strength)return Status::Off;
    if(g.hard_fault)return Status::Poisoned;
    if(g.other_nr)return Status::OtherNrOwner;
    if(!g.d3d12)return Status::UnsupportedApi;
    if(!g.source_verified||!g.application_present)return Status::SourceUnavailable;
    if(!g.color_known||!g.full_rect||!g.only_swapchain)return Status::UnsupportedSurface;
    if(!g.device_and_queue)return Status::WrongDeviceOrQueue;
    if(!g.prior_inline_drained)return Status::Switching;
    return Status::Preparing;
}
// One queue, one shared scratch set, three allocator/evaluation-parameter slots.
// Immutable descriptors and queue order make scratch reuse independent of CPU
// fence polling. Allocators and parameter blocks do still require their fence.
class Timeline {
    std::array<std::uint64_t,3> busy_{};
    std::uint64_t next_=1,last_=0,create_=0;
    bool poisoned_=false;
public:
    int Free(std::uint64_t completed) const noexcept {
        if(poisoned_||completed==UINT64_MAX)return -1;
        for(unsigned i=0;i<busy_.size();++i)if(!busy_[i]||busy_[i]<=completed)return int(i);
        return -1;
    }
    std::uint64_t Reserve(unsigned slot) noexcept {
        if(poisoned_||slot>=busy_.size()||next_==UINT64_MAX)return 0;
        return next_++;
    }
    bool Submitted(unsigned slot,std::uint64_t fence,bool create) noexcept {
        if(poisoned_||slot>=busy_.size()||!fence||fence>=next_||fence<=last_)return false;
        busy_[slot]=last_=fence;if(create)create_=fence;return true;
    }
    bool Ready(std::uint64_t completed) const noexcept {
        return !poisoned_&&create_&&completed!=UINT64_MAX&&completed>=create_;
    }
    bool Drained(std::uint64_t completed) const noexcept {
        return !poisoned_&&completed!=UINT64_MAX&&completed>=last_;
    }
    void Poison() noexcept {poisoned_=true;}
    bool Poisoned() const noexcept {return poisoned_;}
    std::uint64_t Last() const noexcept {return last_;}
};
struct Observation {
    Status status=Status::Off;
    std::uint64_t generation=0,source=0,submitted_source=0,completed_source=0;
    std::uint64_t submitted=0,completed=0,bytes=0;
    unsigned source_width=0,source_height=0,work_width=0,work_height=0,passes=0;
    std::uint32_t result=0;
    bool timing_valid=false;
    std::uint64_t timing_source=0;
    double capture_ms=0,encode_ms=0,nr_ms[5]{},compose_ms=0,blit_ms=0,total_ms=0;
    bool color_only=true,reset_each_frame=true;
    std::uint64_t attempted[5]{},evaluated[5]{},adopted[5]{};
};
// CPU EOTF/primary reference for tests. The rendering shader uses these same
// published constants, but a CPU test is NOT proof that the HLSL was executed.
inline double DecodePq(double x) noexcept {
    constexpr double m1=2610.0/16384.0,m2=2523.0/32.0,c1=3424.0/4096.0,
        c2=2413.0/128.0,c3=2392.0/128.0;
    x=std::pow(std::clamp(x,0.0,1.0),1/m2);
    return std::pow(std::max(x-c1,0.0)/std::max(c2-c3*x,1e-12),1/m1);
}
inline double EncodePq(double x) noexcept {
    constexpr double m1=2610.0/16384.0,m2=2523.0/32.0,c1=3424.0/4096.0,
        c2=2413.0/128.0,c3=2392.0/128.0;
    x=std::pow(std::clamp(x,0.0,1.0),m1);return std::pow((c1+c2*x)/(1+c3*x),m2);
}
} // namespace nr_compat_present
