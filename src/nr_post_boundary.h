#pragma once
#include <cstdint>
#include <limits>

// Bounded recording evidence. Neither a state tracker nor permission to issue
// barriers: a recorded operation/Execute return is not executed or displayed pixels.
namespace nr_post_boundary {
inline void Increment(std::uint32_t &v) { if(v!=std::numeric_limits<std::uint32_t>::max()) ++v; }
enum class Kind : std::uint32_t {
    Transition=1, Uav, Aliasing, EnhancedTexture, EnhancedGlobal, EnhancedBuffer,
    Copy, CopyRegion, ClearUav, ClearRtv, Draw, DrawIndexed, Dispatch,
    BindTargets, Opaque, Resolve, Discard
};
enum class Source : std::uint32_t { Native=1, ReShade=2 };
enum class Close : std::uint32_t { Open=0, Present=1, ScopeChanged, ResourceRetired, Superseded, Expired, Detached };
enum Coverage : std::uint32_t {
    Legacy=1u, Enhanced=2u, EnhancedAbsent=4u, Operations=8u,
    HookMissing=16u, InterfaceUnknown=32u, IdentityUnknown=64u,
    EventLoss=128u, OpaqueOperation=256u, TargetUnknown=512u,
    SubmissionUnknown=1024u
};
struct List {
    std::uint64_t address=0,token=0,reset=0,device=0;
    bool known=false;
};
struct Event {
    List list;
    std::uint64_t order=0,queue=0,submission=0;
    std::uint64_t before=0,after=0,sync_before=0,sync_after=0,access_before=0,access_after=0;
    std::uint32_t subresources[6]={}; // Legacy: [0] subresource. Enhanced: exact six-field range.
    std::uint32_t flags=0;
    std::uint64_t descriptor_resource=0,descriptor_revision=0;
    std::uint32_t descriptor_format=0,clear_rect_count=0,clear_kind=0;
    std::int32_t clear_bounds[4]={}; // Multiple rectangles: envelope, not their exact union.
    std::uint32_t submission_index=0,submission_count=0;
    Kind kind=Kind::Opaque;
    Source source=Source::Native;
    bool reads=false,writes=false,target_known=false,submitted=false;
};
constexpr unsigned kNativeEvents=6,kFollowingEvents=12,kHistory=8;
struct Native {
    // Keep the original ReShade counts distinct from exact native observations.
    std::uint32_t callbacks=0,matches=0,other_lists=0;
    std::uint32_t first_before=0,last_after=0;
    std::uint32_t exact_matches=0,exact_other_lists=0,exact_global=0,unknown_operations=0,coverage=0;
    std::uint32_t count=0,dropped=0;
    std::uint32_t other_rtv_binds=0,other_rtv_clears=0;
    List list;
    Event events[kNativeEvents];
    void Barrier(bool same_list,bool target,std::uint32_t before,std::uint32_t after) {
        Increment(callbacks);
        if(!target)return;
        if(!same_list){Increment(other_lists);return;}
        if(!matches)first_before=before;
        Increment(matches);last_after=after;
    }
    void Append(const Event &event) {
        if(!event.list.known)coverage|=IdentityUnknown;
        if(count<kNativeEvents)events[count++]=event;
        else {
            // Preserve the start and latest native boundary when the bounded
            // trace fills. Missing middle events remain an explicit coverage gap.
            for(unsigned i=kNativeEvents/2;i+1<kNativeEvents;++i)events[i]=events[i+1];
            events[kNativeEvents-1]=event;Increment(dropped);coverage|=EventLoss;
        }
    }
};
struct Window {
    std::uint64_t trace=0,frame=0,feature=0,tick=0,closed_tick=0,device=0;
    std::uint64_t origin_queue=0,origin_submission=0,descriptor_resource=0;
    std::uint32_t origin_submission_index=0,origin_submission_count=0;
    std::uint32_t config=0,barriers=0,read_barriers=0,write_barriers=0;
    std::uint32_t copy_reads=0,copy_writes=0,render_target_binds=0;
    std::uint32_t first_before=0,first_after=0,last_after=0;
    std::uint32_t count=0,dropped=0,coverage=0,exact_following=0,unknown_operations=0;
    std::uint32_t other_rtv_binds=0,other_rtv_clears=0;
    bool origin_submitted=false;
    Close close=Close::Open;
    Native native;
    Event events[kFollowingEvents];
    void Barrier(std::uint32_t before,std::uint32_t after) {
        if(!barriers){first_before=before;first_after=after;}
        Increment(barriers);last_after=after;
        if(after & (0xC0u|0x800u))Increment(read_barriers);
        if(after & (0x8u|0x4u|0x400u))Increment(write_barriers);
    }
    void Copy(bool source,bool destination) {
        if(source)Increment(copy_reads);
        if(destination)Increment(copy_writes);
    }
    bool KnowsList(const List &list) const {
        if(!list.address)return false;
        if(native.list.address==list.address && native.list.token==list.token && native.list.reset==list.reset)return true;
        for(unsigned i=0;i<count;++i)
            if(events[i].list.address==list.address && events[i].list.token==list.token &&
               events[i].list.reset==list.reset && events[i].target_known)return true;
        return false;
    }
    void Append(const Event &event) {
        if(!event.list.known)coverage|=IdentityUnknown;
        if(!event.target_known)coverage|=TargetUnknown;
        if(event.kind==Kind::Opaque)coverage|=OpaqueOperation;
        if(count<kFollowingEvents)events[count++]=event;
        else {Increment(dropped);coverage|=EventLoss;}
    }
};
static_assert(sizeof(Window)<4096,"boundary snapshots must fit the existing small-worker stack budget");
inline const char *CloseName(Close value) {
    switch(value) {
    case Close::Present:return "first-same-device-Present";
    case Close::ScopeChanged:return "scope-changed";
    case Close::ResourceRetired:return "resource-retired";
    case Close::Superseded:return "superseded-before-Present";
    case Close::Expired:return "recording-window-expired";
    case Close::Detached:return "observer-detached";
    default:return "unclosed-or-unobserved";
    }
}
}
