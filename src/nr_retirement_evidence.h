#pragma once
#include <cstdint>
#include <memory>
#include <type_traits>

// Diagnostic values only. No value in this snapshot grants a lifetime permit.
namespace nr_retirement {
using Id=std::uint64_t;
enum Block : Id {
    None=0, CreateQuarantine=1ull<<0, LifecycleQuarantine=1ull<<1,
    TrackingUncertain=1ull<<2, ContextRecording=1ull<<3,
    ContextPending=1ull<<4, ContextAwaitSignal=1ull<<5, ContextFence=1ull<<6,
    SlotRecording=1ull<<7, SlotAwaitSignal=1ull<<8, SlotFence=1ull<<9,
    BindingRecording=1ull<<10, BindingAwaitSignal=1ull<<11, BindingFence=1ull<<12,
    OtherContextBinding=1ull<<13, PrivateRecording=1ull<<14,
    PrivatePoison=1ull<<15, TemporalRecording=1ull<<16, TemporalUnsafe=1ull<<17,
    PendingCapacity=1ull<<18, ReleaseInProgress=1ull<<19, PostReady=1ull<<20,
    Bindings=1ull<<21
};
constexpr Id kAll=(1ull<<22)-1;
constexpr unsigned kContexts=32,kSlots=256,kBindings=256,kRecordings=128,kOwnerFlights=96;
struct Fence {
    Id queue=0,object=0,value=0,completed=0,sample_tick=0;
    bool sampled=false,reused=false;
};
struct Lease {
    Fence fence{};
    unsigned recording=0;
    bool awaiting_signal=false;
};
struct Context {
    Id instance=0,parent=0,feature=0,global_codec_device=0,handle=0;
    Id blocked=0,evaluated=0,unknown=0,first_block=0;
    Id pending_token=0,last_eval=0,last_eval_frame=0,last_submit_queue=0,last_submit_value=0;
    Id last_completed_queue=0,last_completed_value=0,retired_tick=0;
    Lease lease{};
    unsigned configuration=0,layer=0,width=0,height=0,retire_reason=0;
    int mode=0;
    bool pending_destroy=false,post_evaluated=false,post_ready=false;
};
struct Slot { Lease lease{}; Id key=0; unsigned context=0,index=0; };
struct Binding {
    Lease lease{};
    Id instance=0,feature=0,primary_instance=0,secondary_instance=0;
    unsigned configuration=0,index=0,primary_slot=0,secondary_slot=0;
    bool primary_known=false,secondary_known=false;
};
struct Recording {
    Id token=0,address=0,expected_queue=0,submissions=0,contexts=0,direct_contexts=0,indirect_contexts=0;
    // The Core lifecycle token is authoritative here. Native registry/reset
    // identity is unknown unless an existing producer supplied that value.
    Id native_lifetime=0,native_reset=0,device=0;
    unsigned in_flight=0,context_count=0,slot_count=0;
    bool detached=false,unproven_execution=false,native_identity_known=false;
};
struct OwnerFlight {
    Id instance=0,token=0,list=0,reset=0,device=0,queue=0;
    Fence fence{};
    unsigned index=0;
    bool submitted=false,poisoned=false,unsafe=false;
};
struct OwnerInspection {
    OwnerFlight flights[3]{};
    unsigned count=0;
    bool present=false,poisoned=false,unsafe=false,opaque_unknown=false;
};
struct Snapshot {
    Id sequence=0,tick=0,observations=0,suppressed=0;
    unsigned contexts=0,slots=0,bindings=0,recordings=0,private_flights=0,temporal_flights=0;
    unsigned context_overflow=0,slot_overflow=0,binding_overflow=0,recording_overflow=0;
    unsigned private_overflow=0,temporal_overflow=0,pending_used=0,pending_capacity=0,releasing=0;
    bool tracking_uncertain=false;
    Context context[kContexts]{};
    Slot slot[kSlots]{};
    Binding binding[kBindings]{};
    Recording recording[kRecordings]{};
    OwnerFlight private_flight[kOwnerFlights]{},temporal_flight[kOwnerFlights]{};
};
static_assert(std::is_trivially_copyable_v<Snapshot> && std::is_standard_layout_v<Snapshot>);
static_assert(sizeof(Snapshot)<128u*1024u);
inline void ResetSnapshot(Snapshot& snapshot) noexcept {
    // Value-initialize in place: a Snapshot{} assignment can introduce a large
    // temporary on the small F8 worker stack, while memset bypasses the type's
    // default member initialization and triggers GCC's class-memaccess check.
    std::destroy_at(&snapshot);
    std::construct_at(&snapshot);
}
inline const char* Name(Id bit) {
    switch(bit) {
    case None:return "none";case CreateQuarantine:return "create-quarantine";
    case LifecycleQuarantine:return "lifecycle-quarantine";case TrackingUncertain:return "tracking-uncertain";
    case ContextRecording:return "context-recording";case ContextPending:return "context-pending-token";
    case ContextAwaitSignal:return "context-await-signal";case ContextFence:return "context-fence";
    case SlotRecording:return "slot-recording";case SlotAwaitSignal:return "slot-await-signal";case SlotFence:return "slot-fence";
    case BindingRecording:return "binding-recording";case BindingAwaitSignal:return "binding-await-signal";case BindingFence:return "binding-fence";
    case OtherContextBinding:return "other-context-binding";case PrivateRecording:return "private-recording";
    case PrivatePoison:return "private-poison";case TemporalRecording:return "temporal-recording";case TemporalUnsafe:return "temporal-unsafe";
    case PendingCapacity:return "pending-destroy-capacity";case ReleaseInProgress:return "release-in-progress";
    case PostReady:return "private-post-ready";case Bindings:return "binding-lease";default:return "unknown";
    }
}
}
