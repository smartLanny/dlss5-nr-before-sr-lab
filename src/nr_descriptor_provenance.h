#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <mutex>

// Value-only, bounded descriptor provenance. This module never owns a graphics
// object and never grants rendering permission. The adapter brackets real API
// writes with Begin/Finish; overlapping writes and changed copy sources lose
// evidence instead of choosing whichever observer happened to finish last.
namespace nr_descriptor {
using Id = std::uint64_t;
enum class Kind : unsigned { Unknown, Srv, Uav, Rtv, Other };
enum class Reason : unsigned {
    None, HeapUnknown, SlotUnknown, WritePending, ViewUnknown, ResourceRetired,
    WrongKind, WrongDevice, WrongResource, WrongHeap, DifferentViews,
    ShapeUnknown, RevisionChanged, Capacity
};
struct Resource {
    Id id=0, device=0, address=0;
    explicit operator bool() const { return id && device && address; }
};
struct Heap {
    Id id=0, device=0, address=0, cpu=0, gpu=0;
    unsigned count=0, stride=0, type=0;
    bool shader_visible=false;
    explicit operator bool() const { return id && device && address && cpu && count && stride; }
};
struct View {
    Resource resource;
    Kind kind=Kind::Unknown;
    unsigned format=0, mip=0, plane=0, width=0, height=0;
    bool known=false, shape_known=false, typed=false;
};
inline bool SameView(const View &a,const View &b) {
    return a.known && b.known && a.resource.id==b.resource.id && a.resource.device==b.resource.device &&
        a.kind==b.kind && a.format==b.format && a.mip==b.mip && a.plane==b.plane &&
        a.width==b.width && a.height==b.height && a.shape_known==b.shape_known && a.typed==b.typed;
}
struct Stamp {
    Id heap=0, revision=0, epoch=0;
    unsigned index=0;
};
struct Receipt {
    Stamp stamp;
    View view;
    Reason reason=Reason::HeapUnknown;
    bool shader_visible=false;
    explicit operator bool() const { return reason==Reason::None; }
};
struct Write {
    Stamp destination;
    View value;
    Receipt source;
    bool copy=false;
};
struct Pair {
    Receipt cpu, gpu;
    Reason reason=Reason::HeapUnknown;
    explicit operator bool() const { return reason==Reason::None; }
};
enum class RangeKind : unsigned { Unknown, FullView, SingleRect, RectEnvelope };
struct ClearRange {
    std::int32_t bounds[4]={};
    unsigned count=0;
    RangeKind kind=RangeKind::Unknown;
};
template<class Rect> ClearRange DescribeClear(const View &view,unsigned count,const Rect *rects) {
    ClearRange result;result.count=count;
    if(!view.shape_known || !view.width || !view.height ||
       view.width>0x7fffffffu || view.height>0x7fffffffu)return result;
    if(!count) {
        result.kind=RangeKind::FullView;
        result.bounds[2]=static_cast<std::int32_t>(view.width);
        result.bounds[3]=static_cast<std::int32_t>(view.height);return result;
    }
    if(!rects || count>64)return result;
    for(unsigned i=0;i<count;++i) {
        const auto &r=rects[i];
        if(r.left<0 || r.top<0 || r.right<r.left || r.bottom<r.top ||
           static_cast<std::uint64_t>(r.right)>view.width || static_cast<std::uint64_t>(r.bottom)>view.height)return result;
        if(!i) {
            result.bounds[0]=r.left;result.bounds[1]=r.top;result.bounds[2]=r.right;result.bounds[3]=r.bottom;
        } else {
            if(r.left<result.bounds[0])result.bounds[0]=r.left;
            if(r.top<result.bounds[1])result.bounds[1]=r.top;
            if(r.right>result.bounds[2])result.bounds[2]=r.right;
            if(r.bottom>result.bounds[3])result.bounds[3]=r.bottom;
        }
    }
    result.kind=count==1?RangeKind::SingleRect:RangeKind::RectEnvelope;return result;
}
// Four exact descriptor pairs per native call. Overflow is a diagnostic gap,
// never permission to assume the retained pair describes the omitted calls.
struct NativeUse {
    Id call=0,config=0,feature=0,carrier=0,device=0,resource=0,semantic_resource=0,resource_address=0,list=0,reset=0,list_address=0;
    Id queue=0,submission=0;
    unsigned clear_calls=0,matches=0,unknown=0,other_resources=0,other_lists=0,observed_typed=0,count=0;
    unsigned submission_index=0,submission_count=0;
    Reason reason=Reason::HeapUnknown;
    bool hooks_ready=false,identity_known=false,conflict=false,overflow=false;
    bool finished=false,native_success=false,after_native_valid=false,submission_seen=false,at_submit_valid=false,execute_returned=false,replayed=false;
    Pair pairs[4];
    void Observe(const Pair &pair) {
        ++clear_calls;
        if(!pair){++unknown;reason=pair.reason;return;}
        ++matches;
        if(observed_typed && observed_typed!=pair.cpu.view.format)conflict=true;
        if(!observed_typed)observed_typed=pair.cpu.view.format;
        if(count<4)pairs[count++]=pair;
        else {overflow=true;reason=Reason::Capacity;}
    }
    template<class RegistryType> bool RevisionsCurrent(const RegistryType &registry) const {
        if(!count || unknown || conflict || overflow || !identity_known || !hooks_ready)return false;
        for(unsigned i=0;i<count;++i)if(!registry.Current(pairs[i]))return false;
        return true;
    }
    template<class RegistryType> void Finish(bool success,const RegistryType &registry) {
        finished=true;native_success=success;
        after_native_valid=success && RevisionsCurrent(registry);
        if(count && !after_native_valid && reason==Reason::HeapUnknown)reason=Reason::RevisionChanged;
        else if(after_native_valid)reason=Reason::None;
    }
    template<class RegistryType> void Submit(Id queue_id,Id submission_id,unsigned index,unsigned total,
                                           const RegistryType &registry) {
        if(submission_seen){replayed=true;return;}
        submission_seen=true;queue=queue_id;submission=submission_id;submission_index=index;submission_count=total;
        at_submit_valid=finished && native_success && queue_id && submission_id && RevisionsCurrent(registry);
    }
};
struct RtvBatch {
    Receipt views[8];
    unsigned count=0;
    bool unknown=false;
};
struct Counters {
    Id writes=0, copies=0, unknown_writes=0, overlaps=0, evictions=0, invalidations=0,
       resources_retired=0, heaps_retired=0;
};
struct Inventory {
    struct Format { unsigned format=0,count=0;Kind kind=Kind::Unknown; };
    Id resource=0,device=0;
    unsigned slots=0,srv=0,uav=0,rtv=0,unknown_shapes=0,count=0;
    bool lifetime_known=false,overflow=false;
    Format formats[6];
};
inline void Increment(Id &value) { if(value!=std::numeric_limits<Id>::max())++value; }

template<unsigned HeapCapacity=256,unsigned ResourceCapacity=2048,unsigned SlotCapacity=8192>
class Registry {
    struct Slot {
        Stamp stamp;
        View value;
        unsigned pending=0;
        bool overlap=false;
    };
    mutable std::mutex mutex;
    std::array<Heap,HeapCapacity> heaps{};
    std::array<Resource,ResourceCapacity> resources{};
    std::array<Slot,SlotCapacity> slots{};
    Id epoch=1, revision=0;
    Counters counters;
    struct Location { const Heap *heap=nullptr; unsigned index=0; };
    static unsigned Hash(Id heap,unsigned index) {
        return static_cast<unsigned>((heap*0x9E3779B185EBCA87ull+index)%SlotCapacity);
    }
    const Heap *HeapId(Id id) const {
        if(!id)return nullptr;
        const auto &h=heaps[id%HeapCapacity];return h.id==id?&h:nullptr;
    }
    bool ResourceCurrent(const Resource &r) const {
        if(!r)return false;
        const auto &current=resources[r.id%ResourceCapacity];
        return current.id==r.id && current.device==r.device && current.address==r.address;
    }
    Location Locate(Id handle,unsigned type,bool gpu) const {
        Location result;
        if(!handle)return result;
        for(const auto &h:heaps) {
            const Id base=gpu?h.gpu:h.cpu;
            if(!h || h.type!=type || !base || (gpu&&!h.shader_visible) || handle<base)continue;
            const Id delta=handle-base;
            if(delta%h.stride || delta/h.stride>=h.count)continue;
            if(result.heap)return {}; // Overlapping live heap ranges are ambiguous.
            result={&h,static_cast<unsigned>(delta/h.stride)};
        }
        return result;
    }
    Receipt Read(const Location &where,Kind expected) const {
        Receipt result;
        if(!where.heap)return result;
        const auto &h=*where.heap;
        result.shader_visible=h.shader_visible;
        const auto &s=slots[Hash(h.id,where.index)];
        result.stamp=s.stamp;
        if(s.stamp.heap!=h.id || s.stamp.index!=where.index || s.stamp.epoch!=epoch) {
            result.reason=Reason::SlotUnknown;return result;
        }
        if(s.pending){result.reason=Reason::WritePending;return result;}
        result.view=s.value;
        if(!s.value.known){result.reason=Reason::ViewUnknown;return result;}
        if(!ResourceCurrent(s.value.resource)){result.reason=Reason::ResourceRetired;return result;}
        if(s.value.resource.device!=h.device){result.reason=Reason::WrongDevice;return result;}
        if(expected!=Kind::Unknown && s.value.kind!=expected){result.reason=Reason::WrongKind;return result;}
        result.reason=Reason::None;return result;
    }
    bool CurrentLocked(const Receipt &r) const {
        if(!r || r.stamp.epoch!=epoch)return false;
        const auto *h=HeapId(r.stamp.heap);
        if(!h || r.stamp.index>=h->count)return false;
        const auto now=Read({h,r.stamp.index},r.view.kind);
        return now && now.stamp.revision==r.stamp.revision && SameView(now.view,r.view);
    }
    Write BeginLocked(const Location &where,const View &value) {
        Write write;
        if(!where.heap){Increment(counters.unknown_writes);return write;}
        const auto &h=*where.heap;
        auto &s=slots[Hash(h.id,where.index)];
        const bool same=s.stamp.heap==h.id && s.stamp.index==where.index && s.stamp.epoch==epoch;
        if(!same) {
            if(s.stamp.heap)Increment(counters.evictions);
            s={};
        }
        if(s.pending) {s.overlap=true;Increment(counters.overlaps);}
        else s.overlap=false;
        if(s.pending!=std::numeric_limits<unsigned>::max())++s.pending;
        else {InvalidateLocked();return write;}
        Increment(revision);
        if(revision==std::numeric_limits<Id>::max()){InvalidateLocked();return write;}
        s.stamp={h.id,revision,epoch,where.index};
        s.value={};
        write.destination=s.stamp;
        if(value.resource.device==h.device)write.value=value;
        Increment(counters.writes);
        return write;
    }
    void InvalidateLocked() { Increment(epoch);Increment(counters.invalidations); }
public:
    void ObserveResource(Resource resource) {
        if(!resource)return;
        std::lock_guard guard(mutex);
        auto &entry=resources[resource.id%ResourceCapacity];
        if(entry.id && entry.id!=resource.id)Increment(counters.evictions);
        entry=resource;
    }
    bool ObserveHeap(Heap heap) {
        if(!heap || heap.type>3 || (heap.shader_visible&&!heap.gpu) ||
           (heap.count-1)>((std::numeric_limits<Id>::max()-heap.cpu)/heap.stride) ||
           (heap.gpu && (heap.count-1)>((std::numeric_limits<Id>::max()-heap.gpu)/heap.stride)))return false;
        std::lock_guard guard(mutex);
        auto &entry=heaps[heap.id%HeapCapacity];
        if(entry.id && entry.id!=heap.id)Increment(counters.evictions);
        entry=heap;return true;
    }
    void RetireResource(Id id) {
        std::lock_guard guard(mutex);
        auto &r=resources[id%ResourceCapacity];
        if(id && r.id==id){r={};Increment(counters.resources_retired);}
    }
    void RetireHeap(Id id) {
        std::lock_guard guard(mutex);
        auto &h=heaps[id%HeapCapacity];
        if(id && h.id==id){h={};Increment(counters.heaps_retired);}
    }
    void RetireDevice(Id device) {
        std::lock_guard guard(mutex);
        for(auto &r:resources)if(r.device==device)r={};
        for(auto &h:heaps)if(h.device==device)h={};
    }
    Resource ResourceAt(Id address) const {
        std::lock_guard guard(mutex);
        Resource result;
        for(const auto &r:resources)if(r && r.address==address) {
            if(result)return {};result=r;
        }
        return result;
    }
    Heap HeapAt(Id address) const {
        std::lock_guard guard(mutex);
        Heap result;
        for(const auto &h:heaps)if(h && h.address==address) {
            if(result)return {};result=h;
        }
        return result;
    }
    Receipt Cpu(Id handle,unsigned type,Kind expected=Kind::Unknown) const {
        std::lock_guard guard(mutex);return Read(Locate(handle,type,false),expected);
    }
    Receipt Gpu(Id handle,unsigned type,Kind expected=Kind::Unknown) const {
        std::lock_guard guard(mutex);return Read(Locate(handle,type,true),expected);
    }
    Id OffsetCpu(Id handle,unsigned type,unsigned offset) const {
        std::lock_guard guard(mutex);
        const auto where=Locate(handle,type,false);
        if(!where.heap || offset>=where.heap->count-where.index)return 0;
        return handle+static_cast<Id>(offset)*where.heap->stride;
    }
    Write BeginWrite(Id cpu,unsigned type,const View &value) {
        std::lock_guard guard(mutex);return BeginLocked(Locate(cpu,type,false),value);
    }
    Write BeginCopy(Id destination,Id source,unsigned type) {
        std::lock_guard guard(mutex);
        const auto from=Read(Locate(source,type,false),Kind::Unknown);
        auto write=BeginLocked(Locate(destination,type,false),
            from && !from.shader_visible?from.view:View{});
        write.source=from;write.copy=true;Increment(counters.copies);return write;
    }
    void Finish(const Write &write) {
        std::lock_guard guard(mutex);
        const auto &stamp=write.destination;
        if(!stamp.heap)return;
        auto &slot=slots[Hash(stamp.heap,stamp.index)];
        if(slot.stamp.heap!=stamp.heap || slot.stamp.index!=stamp.index || slot.stamp.epoch!=stamp.epoch)return;
        if(slot.pending)--slot.pending;
        if(slot.stamp.revision!=stamp.revision || slot.pending || slot.overlap || stamp.epoch!=epoch || !HeapId(stamp.heap))return;
        if(write.value.known && ResourceCurrent(write.value.resource) &&
           (!write.copy || (!write.source.shader_visible && CurrentLocked(write.source))))slot.value=write.value;
    }
    bool Current(const Receipt &receipt) const { std::lock_guard guard(mutex);return CurrentLocked(receipt); }
    bool Current(const Pair &pair) const {
        std::lock_guard guard(mutex);return pair && CurrentLocked(pair.cpu) && CurrentLocked(pair.gpu);
    }
    Pair UavPair(Id device,Resource output,Id bound_heap,Id gpu,Id cpu) const {
        std::lock_guard guard(mutex);
        Pair pair;pair.cpu=Read(Locate(cpu,0,false),Kind::Uav);pair.gpu=Read(Locate(gpu,0,true),Kind::Uav);
        if(!pair.cpu){pair.reason=pair.cpu.reason;return pair;}
        if(!pair.gpu){pair.reason=pair.gpu.reason;return pair;}
        if(pair.cpu.shader_visible || !pair.gpu.shader_visible || !bound_heap || pair.gpu.stamp.heap!=bound_heap) {
            pair.reason=Reason::WrongHeap;return pair;
        }
        if(!device || pair.cpu.view.resource.device!=device || pair.gpu.view.resource.device!=device) {
            pair.reason=Reason::WrongDevice;return pair;
        }
        if(!ResourceCurrent(output) || pair.cpu.view.resource.id!=output.id || pair.gpu.view.resource.id!=output.id) {
            pair.reason=Reason::WrongResource;return pair;
        }
        if(!SameView(pair.cpu.view,pair.gpu.view)){pair.reason=Reason::DifferentViews;return pair;}
        if(!pair.cpu.view.shape_known || !pair.cpu.view.typed || !pair.cpu.view.format) {
            pair.reason=Reason::ShapeUnknown;return pair;
        }
        pair.reason=Reason::None;return pair;
    }
    void Invalidate() { std::lock_guard guard(mutex);InvalidateLocked(); }
    Counters SnapshotCounters() const { std::lock_guard guard(mutex);return counters; }
    Inventory ResourceInventory(Id id) const {
        std::lock_guard guard(mutex);
        Inventory inventory;inventory.resource=id;
        if(!id || resources[id%ResourceCapacity].id!=id)return inventory;
        inventory.lifetime_known=true;inventory.device=resources[id%ResourceCapacity].device;
        for(const auto &slot:slots) {
            if(slot.stamp.epoch!=epoch || slot.pending || !slot.value.known || slot.value.resource.id!=id ||
               !HeapId(slot.stamp.heap) || !ResourceCurrent(slot.value.resource))continue;
            ++inventory.slots;
            const auto &view=slot.value;
            if(view.kind==Kind::Srv)++inventory.srv;
            if(view.kind==Kind::Uav)++inventory.uav;
            if(view.kind==Kind::Rtv)++inventory.rtv;
            if(!view.shape_known || !view.typed)++inventory.unknown_shapes;
            bool found=false;
            for(unsigned i=0;i<inventory.count;++i)if(inventory.formats[i].kind==view.kind && inventory.formats[i].format==view.format) {
                ++inventory.formats[i].count;found=true;break;
            }
            if(found)continue;
            if(inventory.count<6)inventory.formats[inventory.count++]={view.format,1,view.kind};
            else inventory.overflow=true;
        }
        return inventory;
    }
};
inline const char *ReasonName(Reason reason) {
    switch(reason) {
    case Reason::None:return "known";
    case Reason::HeapUnknown:return "heap-unobserved-or-ambiguous";
    case Reason::SlotUnknown:return "slot-unobserved-or-evicted";
    case Reason::WritePending:return "descriptor-write-in-progress";
    case Reason::ViewUnknown:return "descriptor-value-unknown";
    case Reason::ResourceRetired:return "resource-lifetime-retired";
    case Reason::WrongKind:return "descriptor-kind-mismatch";
    case Reason::WrongDevice:return "descriptor-device-mismatch";
    case Reason::WrongResource:return "explicit-resource-mismatch";
    case Reason::WrongHeap:return "descriptor-heap-binding-mismatch";
    case Reason::DifferentViews:return "CPU-GPU-view-mismatch";
    case Reason::ShapeUnknown:return "view-shape-or-type-unknown";
    case Reason::RevisionChanged:return "descriptor-revision-changed";
    default:return "bounded-capacity-exceeded";
    }
}
} // namespace nr_descriptor
