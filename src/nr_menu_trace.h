#pragma once
#define NR_MENU_TRACE_ENABLED 1
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include "nr_guide_contract.h"

// CPU-only evidence for the short period following a foreground Esc press.
// This does not grant admission, retain COM objects, or infer displayed pixels.
namespace nr_menu_trace {
constexpr std::uint64_t kWindowMs = 2000;
constexpr unsigned kCapacity = 64;
enum FrameBit : std::uint32_t {
    Render=1u<<0, Work=1u<<1, Output=1u<<2, Carrier=1u<<3,
    Instance=1u<<4, LayerRoute=1u<<5, Generations=1u<<6, Source=1u<<7
};
enum StorageBit : std::uint32_t {
    DepthFormat=1u<<0, MotionFormat=1u<<1, ColorStorage=1u<<2,
    MotionBound=1u<<3, CreateFlags=1u<<4, DepthPlaceholder=1u<<5,
    MotionResample=1u<<6
};
enum MappingBit : std::uint32_t {
    ColorFormat=1u<<0, DepthPhysical=1u<<1, MotionPhysical=1u<<2,
    ColorPhysical=1u<<3, DepthActive=1u<<4, MotionSource=1u<<5,
    DepthOrigin=1u<<6, MotionOrigin=1u<<7, ColorOrigin=1u<<8,
    MotionScale=1u<<9, DepthDirection=1u<<10, DepthProjection=1u<<11
};
enum ResourceBit : std::uint32_t { DepthResource=1u, MotionResource=2u, ColorResource=4u };
enum class Kind : unsigned { Esc, First, Change, ResourceRotation, Create, CreateFailed, Retire, Reset, EvaluateFailed, Bypass, GuideRejected };
struct Frame {
    bool observed=false,context_known=false,source_known=false;
    unsigned render_w=0,render_h=0,work_w=0,work_h=0,output_w=0,output_h=0;
    int feature=0,route=0;
    std::uintptr_t carrier_key=0; // comparison key only; never exported
    std::uint64_t feature_generation=0,instance=0,source_stream=0,source_epoch=0,source_frame=0;
    unsigned layer=0,config=0,history=0;
};
struct Resources { std::uintptr_t depth=0,motion=0,color=0; };
struct Event {
    Kind kind=Kind::First;
    std::uint64_t ms=0;
    Frame frame{};
    std::uint32_t frame_mask=0,storage_mask=0,mapping_mask=0,resource_mask=0;
    unsigned reason=0;
    bool reset=false;
    unsigned previous_work_w=0,previous_work_h=0;
    NrGuideSignature guide_before{},guide_after{};
    unsigned coalesced_count=0;
    std::uint64_t last_ms=0;
};
struct Snapshot {
    bool marked=false,expired=false,has_frame=false;
    std::uint64_t started=0,deadline=0;
    unsigned presses=0,repeated_presses=0,observations=0,evaluations=0,successful=0,failed=0;
    unsigned creates=0,create_failed=0,retires=0,resets=0,reset_success=0,reset_rejected=0;
    unsigned bypasses=0,guide_rejected=0,resource_rotations=0,overflow=0;
    std::uint32_t resource_rotation_union_mask=0;
    unsigned count=0;
    Event events[kCapacity]{};
};
struct History { unsigned count=0; Snapshot windows[3]{}; };
inline std::uint32_t FrameMask(const Frame& a,const Frame& b) {
    return (a.render_w!=b.render_w||a.render_h!=b.render_h?std::uint32_t{Render}:0u) |
        (a.work_w!=b.work_w||a.work_h!=b.work_h?std::uint32_t{Work}:0u) |
        (a.output_w!=b.output_w||a.output_h!=b.output_h?std::uint32_t{Output}:0u) |
        (a.feature!=b.feature||a.feature_generation!=b.feature_generation?std::uint32_t{Carrier}:0u) |
        (a.instance!=b.instance?std::uint32_t{Instance}:0u) |
        (a.layer!=b.layer||a.route!=b.route?std::uint32_t{LayerRoute}:0u) |
        (a.config!=b.config||a.history!=b.history?std::uint32_t{Generations}:0u) |
        (a.source_stream!=b.source_stream||a.source_epoch!=b.source_epoch?std::uint32_t{Source}:0u);
}
inline std::uint32_t StorageMask(const NrGuideSignature& a,const NrGuideSignature& b) {
    const bool color_same=a.color_format==b.color_format ||
        ((a.color_format==10u||a.color_format==26u)&&(b.color_format==10u||b.color_format==26u));
    return (a.depth_format!=b.depth_format?std::uint32_t{DepthFormat}:0u) |
        (a.motion_format!=b.motion_format?std::uint32_t{MotionFormat}:0u) |
        (!color_same?std::uint32_t{ColorStorage}:0u) |
        (a.motion_bound_width!=b.motion_bound_width||a.motion_bound_height!=b.motion_bound_height?std::uint32_t{MotionBound}:0u) |
        (a.normalized_create_flags!=b.normalized_create_flags?std::uint32_t{CreateFlags}:0u) |
        (a.rr_depth_placeholder!=b.rr_depth_placeholder?std::uint32_t{DepthPlaceholder}:0u) |
        (a.resample_motion!=b.resample_motion?std::uint32_t{MotionResample}:0u);
}
inline std::uint32_t MappingMask(const NrGuideSignature& a,const NrGuideSignature& b) {
    return (a.color_format!=b.color_format?std::uint32_t{ColorFormat}:0u) |
        (a.depth_physical_width!=b.depth_physical_width||a.depth_physical_height!=b.depth_physical_height?std::uint32_t{DepthPhysical}:0u) |
        (a.motion_physical_width!=b.motion_physical_width||a.motion_physical_height!=b.motion_physical_height?std::uint32_t{MotionPhysical}:0u) |
        (a.color_physical_width!=b.color_physical_width||a.color_physical_height!=b.color_physical_height?std::uint32_t{ColorPhysical}:0u) |
        (a.depth_width!=b.depth_width||a.depth_height!=b.depth_height?std::uint32_t{DepthActive}:0u) |
        (a.motion_source_width!=b.motion_source_width||a.motion_source_height!=b.motion_source_height?std::uint32_t{MotionSource}:0u) |
        (a.depth_x!=b.depth_x||a.depth_y!=b.depth_y?std::uint32_t{DepthOrigin}:0u) |
        (a.motion_x!=b.motion_x||a.motion_y!=b.motion_y?std::uint32_t{MotionOrigin}:0u) |
        (a.color_x!=b.color_x||a.color_y!=b.color_y?std::uint32_t{ColorOrigin}:0u) |
        (a.motion_scale_x!=b.motion_scale_x||a.motion_scale_y!=b.motion_scale_y?std::uint32_t{MotionScale}:0u) |
        (a.depth_inverted!=b.depth_inverted||a.linear_depth!=b.linear_depth?std::uint32_t{DepthDirection}:0u) |
        (a.depth_projection_a!=b.depth_projection_a||a.depth_projection_b!=b.depth_projection_b?std::uint32_t{DepthProjection}:0u);
}
inline std::uint32_t ResourceMask(Resources a,Resources b) {
    return (a.depth!=b.depth?std::uint32_t{DepthResource}:0u) |
        (a.motion!=b.motion?std::uint32_t{MotionResource}:0u) |
        (a.color!=b.color?std::uint32_t{ColorResource}:0u);
}
class Trace {
    mutable std::mutex mutex_;
    std::atomic<std::uint64_t> deadline_{0};
    Snapshot state_{};
    Snapshot previous_[2]{};
    unsigned previous_count_=0;
    struct Baseline { Frame frame{}; NrGuideSignature guide{}; Resources resources{}; bool valid=false,rotation_logged=false; };
    Baseline baselines_[16]{};
    unsigned head_=0;
    void Push(Event e) {
        if(state_.count==kCapacity) { ++state_.overflow; state_.events[head_]=e; head_=(head_+1)%kCapacity; }
        else state_.events[(head_+state_.count++)%kCapacity]=e;
    }
    bool Within(std::uint64_t now) const {
        const auto deadline=deadline_.load(std::memory_order_relaxed);
        return deadline!=0 && now<=deadline;
    }
    static bool SameStream(const Frame& a,const Frame& b) {
        return a.carrier_key==b.carrier_key && a.feature_generation==b.feature_generation &&
            a.route==b.route && a.layer==b.layer &&
            a.source_stream==b.source_stream && a.source_epoch==b.source_epoch;
    }
    static void Order(Snapshot& value,unsigned head) {
        Snapshot copy=value;
        for(unsigned i=0;i<copy.count;++i)value.events[i]=copy.events[(head+i)%kCapacity];
    }
public:
    bool Armed() const { return deadline_.load(std::memory_order_relaxed)!=0; }
    bool Active(std::uint64_t now) {
        if(Within(now))return true;
        auto deadline=deadline_.load(std::memory_order_relaxed);
        if(deadline && now>deadline)
            deadline_.compare_exchange_strong(deadline,0,std::memory_order_relaxed);
        return false;
    }
    void Mark(std::uint64_t now) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(state_.marked && now<=state_.deadline) {
            ++state_.presses; ++state_.repeated_presses;
            state_.deadline=now+kWindowMs;
        } else {
            if(state_.marked) {
                if(previous_count_==2)previous_[0]=previous_[1];
                else ++previous_count_;
                previous_[previous_count_-1]=state_;
                previous_[previous_count_-1].expired=true;
                Order(previous_[previous_count_-1],head_);
            }
            // Keep default member values without a large temporary Snapshot.
            std::destroy_at(&state_);std::construct_at(&state_);
            state_.marked=true;state_.started=now;
            state_.deadline=now+kWindowMs;state_.presses=1;
            for(auto& baseline:baselines_){baseline.valid=false;baseline.rotation_logged=false;}
            head_=0;
        }
        Push({Kind::Esc,now});
        deadline_.store(state_.deadline,std::memory_order_release);
    }
    void Observe(std::uint64_t now,const Frame& f,const NrGuideSignature& g,Resources r) {
        if(!Within(now))return;
        std::lock_guard<std::mutex> lock(mutex_);
        if(!Within(now))return;
        ++state_.observations;
        Baseline* baseline=nullptr;
        for(auto& slot:baselines_)if(slot.valid&&SameStream(slot.frame,f)){baseline=&slot;break;}
        if(!baseline)for(auto& slot:baselines_)if(!slot.valid){baseline=&slot;break;}
        if(!baseline)baseline=&baselines_[0]; // bounded stream table; evicted stream starts as First
        const bool known=baseline->valid&&SameStream(baseline->frame,f);
        if(!known)baseline->rotation_logged=false;
        const auto fm=known?FrameMask(baseline->frame,f):0;
        const auto sm=known?StorageMask(baseline->guide,g):0;
        const auto mm=known?MappingMask(baseline->guide,g):0;
        const auto rm=known?ResourceMask(baseline->resources,r):0;
        if(rm){++state_.resource_rotations;state_.resource_rotation_union_mask|=rm;}
        if(!known||fm||sm||mm)Push({!known?Kind::First:Kind::Change,
            now,f,fm,sm,mm,rm,0,false,known?baseline->frame.work_w:0,known?baseline->frame.work_h:0,
            known?baseline->guide:NrGuideSignature{},g});
        else if(rm){
            // One ring event per stream/window. Ordinary double-buffer rotation
            // updates its summary and cannot evict earlier lifecycle evidence.
            if(!baseline->rotation_logged){
                Push({Kind::ResourceRotation,now,f,0,0,0,rm,0,false,
                    baseline->frame.work_w,baseline->frame.work_h,baseline->guide,g,1,now});
                baseline->rotation_logged=true;
            } else {
                for(unsigned i=0;i<state_.count;++i){
                    auto& event=state_.events[(head_+i)%kCapacity];
                    if(event.kind==Kind::ResourceRotation&&SameStream(event.frame,f)){
                        ++event.coalesced_count;event.resource_mask|=rm;event.last_ms=now;
                        break;
                    }
                }
                // If genuine non-rotation overflow evicted the first event,
                // the global count/union and overflow remain explicit.
            }
        }
        baseline->frame=f;baseline->guide=g;baseline->resources=r;baseline->valid=true;
        state_.has_frame=true;
    }
    void ContractChange(std::uint64_t now,const Frame& before,const Frame& after,
                        const NrGuideSignature& old_guide,const NrGuideSignature& new_guide) {
        if(!Within(now))return;
        std::lock_guard<std::mutex> lock(mutex_);
        if(!Within(now))return;
        Push({Kind::Change,now,after,FrameMask(before,after),StorageMask(old_guide,new_guide),
            MappingMask(old_guide,new_guide),0,3,false,before.work_w,before.work_h,old_guide,new_guide});
    }
    void Note(std::uint64_t now,Kind kind,const Frame& f,unsigned reason=0,bool reset=false) {
        if(!Within(now))return;
        std::lock_guard<std::mutex> lock(mutex_);
        if(!Within(now))return;
        switch(kind) {
        case Kind::Create: ++state_.creates;break;
        case Kind::CreateFailed: ++state_.create_failed;break;
        case Kind::Retire: ++state_.retires;break;
        case Kind::Reset: ++state_.resets;break;
        case Kind::EvaluateFailed: ++state_.evaluations;++state_.failed;if(reset)++state_.reset_rejected;break;
        case Kind::Bypass: ++state_.bypasses;break;
        case Kind::GuideRejected: ++state_.guide_rejected;break;
        default: break;
        }
        Push({kind,now,f,0,0,0,0,reason,reset||kind==Kind::Reset});
    }
    void Success(std::uint64_t now,bool reset=false) {
        if(!Within(now))return;
        std::lock_guard<std::mutex> lock(mutex_);
        if(!Within(now))return;
        ++state_.evaluations;++state_.successful;if(reset)++state_.reset_success;
    }
    Snapshot Copy(std::uint64_t now) const {
        std::lock_guard<std::mutex> lock(mutex_);
        Snapshot result=state_;
        result.expired=result.marked&&now>result.deadline;
        for(unsigned i=0;i<result.count;++i)result.events[i]=state_.events[(head_+i)%kCapacity];
        return result;
    }
    History CopyHistory(std::uint64_t now) const {
        std::lock_guard<std::mutex> lock(mutex_);
        History result{};
        for(unsigned i=0;i<previous_count_;++i)result.windows[result.count++]=previous_[i];
        if(state_.marked) {
            auto& current=result.windows[result.count++];current=state_;
            current.expired=now>current.deadline;
            Order(current,head_);
        }
        return result;
    }
};
} // namespace nr_menu_trace
