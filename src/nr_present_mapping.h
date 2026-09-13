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
// Conservative keeps the established layer units. Only the explicit experiment
// requests P -> model-raster rebasing; size inequality alone is not evidence.
inline bool CompensateLayerMotion(float& x,float& y,Extent present,Extent model,unsigned mode) noexcept {
    return mode!=1 || ScaleToModel(x,y,present,model);
}
// Metadata preflight only; never retires GPU work, borrows stale guides, or
// grants consumption. The actual current buffer is checked again in Select.
template<unsigned Capacity> class Demands {
    struct Target {std::uint64_t chain=0,device=0;unsigned config=0;Extent size{};};
    std::array<Target,Capacity> targets{};
    bool overflow=false;
public:
    void Publish(std::uint64_t chain,std::uint64_t device,unsigned config,Extent size) noexcept {
        if(!chain||!device)return;
        Target* slot=nullptr;
        for(auto& t:targets) {
            if(t.chain==chain){slot=&t;break;}
            if(!t.chain&&!slot)slot=&t;
        }
        if(!slot){overflow=true;return;}
        *slot={chain,device,config,size};
    }
    void Forget(std::uint64_t chain) noexcept {
        for(auto& t:targets)if(t.chain==chain)t={};
    }
    bool Needed(std::uint64_t device,unsigned config,Extent native,bool automatic=true) const noexcept {
        if(!device||!config||overflow)return false;
        const Target* selected=nullptr;
        for(const auto& t:targets)if(t.chain&&t.device==device&&t.config==config) {
            if(selected)return false;
            selected=&t;
        }
        return selected&&bool(Map(native,selected->size,automatic));
    }
};
} // namespace nr_present_mapping
