#pragma once
#include <array>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <limits>

// Present-only geometry policy. This is explicitly an inferred full-viewport
// correspondence, NOT proof of image lineage. Native before/after paths do not
// use it. Device, submission, ownership and freshness are checked separately.
namespace nr_present_mapping {
struct Extent {unsigned width=0,height=0;};
struct Plan {
    float x=1.f,y=1.f;
    bool valid=false,scaled=false,rounded_aspect=false;
    explicit operator bool() const noexcept {return valid;}
};
inline bool Valid(Extent e) noexcept {
    return e.width&&e.height&&e.width<=16384&&e.height<=16384;
}
inline Plan Map(Extent native_output,Extent present,bool automatic=true) noexcept {
    if(!Valid(native_output)||!Valid(present))return {};
    if(native_output.width==present.width&&native_output.height==present.height)
        return {1,1,true,false,false};
    if(!automatic)return {};
    const auto a=std::uint64_t(native_output.width)*present.height;
    const auto b=std::uint64_t(present.width)*native_output.height;
    const auto difference=a>b?a-b:b-a;
    // DRS/odd raster rounding: at most one raster pixel AND 0.1% relative
    // aspect error. A crop, letterbox, split view or anamorphic transform is
    // not inferred merely because both rectangles exist.
    const auto pixel_bound=std::max({native_output.width,native_output.height,
                                    present.width,present.height});
    if(difference && (difference>pixel_bound || difference*1000>std::max(a,b)))return {};
    return {float(present.width)/native_output.width,
            float(present.height)/native_output.height,true,true,difference!=0};
}
inline const char* Name(const Plan& p) noexcept {
    return !p?"unmapped":!p.scaled?"exact-extent":p.rounded_aspect?
        "inferred-whole-viewport-rounded":"inferred-whole-viewport-scale";
}
inline bool ScaleToModel(float& x,float& y,Extent from,Extent to) noexcept;
// Conservative keeps the established layer units. Only the explicit experiment
// requests P -> model-raster rebasing; size inequality alone is not evidence.
inline bool CompensateLayerMotion(float& x,float& y,Extent present,Extent model,unsigned mode) noexcept {
    return mode!=1 || ScaleToModel(x,y,present,model);
}
// Input units must already be the Core's native-output-raster contract. This
// helper does not convert raw NGX or normalized-UV motion automatically.
inline bool ScaleToModel(float& x,float& y,Extent from,Extent to) noexcept {
    if(!Valid(from)||!Valid(to)||!std::isfinite(x)||!std::isfinite(y)||x==0.f||y==0.f)return false;
    const double nx=double(x)*to.width/from.width,ny=double(y)*to.height/from.height;
    const double limit=std::numeric_limits<float>::max();
    if(!std::isfinite(nx)||!std::isfinite(ny)||std::abs(nx)>limit||std::abs(ny)>limit)return false;
    const float a=static_cast<float>(nx),b=static_cast<float>(ny);
    if(!std::isfinite(a)||!std::isfinite(b)||a==0.f||b==0.f)return false;
    x=a;y=b;return true; // all-or-nothing; no partially rebased binding
}
// Advisory geometry hint, NOT producer admission. A missing/partial cache must
// allow observation to bootstrap (including a user enabling DLSS much later).
// Only a complete known set with no compatible extent can avoid GPU snapshots.
// Publish runs in CanPresent even when no receipt has ever been selected.
template<unsigned Capacity> class Demands {
    static_assert(Capacity>0);
    struct Target {std::uint64_t chain=0,device=0;unsigned config=0;Extent size{};};
    std::array<Target,Capacity> targets{};
    bool incomplete=false;
    unsigned next=0;
public:
    void Publish(std::uint64_t chain,std::uint64_t device,unsigned config,Extent size) noexcept {
        if(!chain||!device)return;
        Target* slot=nullptr;
        for(auto& t:targets) {
            if(t.chain==chain){slot=&t;break;}
            if(!t.chain&&!slot)slot=&t;
        }
        if(!slot) {
            // Loss of hint coverage must NEVER become a permanent rejection.
            // Keep recent positive hints, conservatively retry on a miss.
            incomplete=true;slot=&targets[next];next=(next+1)%Capacity;
        }
        *slot={chain,device,config,size};
    }
    void Forget(std::uint64_t chain) noexcept {
        for(auto& t:targets)if(t.chain==chain)t={};
    }
    bool Needed(std::uint64_t device,unsigned config,Extent native,bool automatic=true) const noexcept {
        if(!device||!config||!Valid(native))return true;
        bool observed=false,unknown=incomplete;
        for(const auto& t:targets)if(t.chain&&t.device==device&&t.config==config) {
            observed=true;
            if(!Valid(t.size)){unknown=true;continue;}
            if(Map(native,t.size,automatic))return true; // ANY consumer, not exactly one
        }
        // Config change, cold start, forgotten chain, or capacity pressure is
        // observation-missing, not 'DLSS permanently unsupported'. No backoff
        // counter, history reset, INI latch, or main-swapchain guess is used.
        return !observed||unknown;
    }
};
} // namespace nr_present_mapping

