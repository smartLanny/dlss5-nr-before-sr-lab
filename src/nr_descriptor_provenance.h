#pragma once
#include <array>
#include <algorithm>
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
    ShapeUnknown, RevisionChanged, Capacity, CoverageLost
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
    Id incarnation=0;
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
// A bounded descriptor-copy path may know the destination CPU-handle ranges
// but cannot decode every descriptor in the call.  The range is inclusive;
// the caller owns the array for the complete lifetime of the UnknownCopy node.
struct UnknownRange {
    Id first=0,last=0;
    unsigned type=0;
};
struct UnknownCopy {
    const UnknownRange *ranges=nullptr;
    unsigned count=0;
    bool all=false;
    UnknownCopy *next=nullptr;
    bool active=false;
    UnknownCopy()=default;
    UnknownCopy(const UnknownCopy&)=delete;
    UnknownCopy &operator=(const UnknownCopy&)=delete;
    UnknownCopy(UnknownCopy&&)=delete;
    UnknownCopy &operator=(UnknownCopy&&)=delete;
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
    Pair last_pair;
    void Observe(const Pair &pair) {
        last_pair=pair;
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
struct PoolCounters {
    unsigned active=0,capacity=0;
    Id drops=0,evictions=0,ambiguities=0;
    bool coverage_lost=false;
};
struct Counters {
    Id writes=0, copies=0, unknown_writes=0, overlaps=0, evictions=0, invalidations=0,
       resources_retired=0, heaps_retired=0, scoped_copies=0, global_copies=0;
    PoolCounters heaps,resources,cpu_slots,gpu_slots;
};
struct Inventory {
    struct Format { unsigned format=0,count=0;Kind kind=Kind::Unknown; };
    Id resource=0,device=0;
    unsigned slots=0,srv=0,uav=0,rtv=0,unknown_shapes=0,count=0;
    bool lifetime_known=false,overflow=false;
    Format formats[6];
};
inline void Increment(Id &value) { if(value!=std::numeric_limits<Id>::max())++value; }

// Full-key tables keep live objects until an actual retirement. Empty buckets
// end a probe; deletion repairs the cluster instead of accumulating tombstones.
template<class Value,unsigned Capacity> struct LiveTable {
    static_assert(Capacity>0);
    static constexpr unsigned BucketCapacity=Capacity*2;
    std::array<Value,BucketCapacity> values{};
    unsigned active=0;
    static unsigned Hash(Id id) {
        id^=id>>30;id*=0xbf58476d1ce4e5b9ull;id^=id>>27;id*=0x94d049bb133111ebull;id^=id>>31;
        return static_cast<unsigned>(id%BucketCapacity);
    }
    const Value *Find(Id id) const {
        if(!id)return nullptr;
        unsigned n=Hash(id);
        for(unsigned i=0;i<BucketCapacity;++i,n=(n+1)%BucketCapacity) {
            if(values[n].id==id)return &values[n];
            if(!values[n].id)return nullptr;
        }
        return nullptr;
    }
    bool Insert(Value value) {
        if(auto *existing=const_cast<Value *>(Find(value.id))){*existing=value;return true;}
        if(active==Capacity)return false;
        unsigned n=Hash(value.id);
        for(unsigned i=0;i<BucketCapacity;++i,n=(n+1)%BucketCapacity) {
            if(!values[n].id){values[n]=value;++active;return true;}
        }
        return false;
    }
    bool Erase(Id id) {
        const auto *entry=Find(id);if(!entry)return false;
        unsigned n=static_cast<unsigned>(entry-values.data());values[n]={};--active;
        for(unsigned i=0;i<BucketCapacity-1;++i) {
            n=(n+1)%BucketCapacity;if(!values[n].id)break;
            const auto move=values[n];values[n]={};--active;Insert(move);
        }
        return true;
    }
};

template<unsigned HeapCapacity=4096,unsigned ResourceCapacity=32768,
         unsigned CpuSlotCapacity=32768,unsigned GpuSlotCapacity=CpuSlotCapacity*2>
class Registry {
    struct Slot {
        Stamp stamp;
        View value;
        Id incarnation=0;
        unsigned pending=0,chain=0,older=0,newer=0;
        bool overlap=false;
    };
    // Chained full-key index and a free list use fixed storage. Only completed
    // entries join the eviction list; an in-flight write can never be recycled.
    template<unsigned Capacity> struct Slots {
        static_assert(Capacity>0);
        std::array<Slot,Capacity> values{};
        std::array<unsigned,Capacity> buckets{};
        unsigned free=1,oldest=0,newest=0,active=0;
        Slots(){for(unsigned i=0;i<Capacity;++i)values[i].chain=i+1<Capacity?i+2:0;}
        static unsigned Hash(Id heap,unsigned index) {
            return LiveTable<Resource,Capacity>::Hash(heap^(static_cast<Id>(index)*0x9E3779B185EBCA87ull))%Capacity;
        }
        Slot *Find(Id heap,unsigned index) {
            for(auto n=buckets[Hash(heap,index)];n;n=values[n-1].chain) {
                auto &s=values[n-1];if(s.stamp.heap==heap && s.stamp.index==index)return &s;
            }
            return nullptr;
        }
        const Slot *Find(Id heap,unsigned index) const {return const_cast<Slots *>(this)->Find(heap,index);}
        unsigned Number(const Slot &s) const {return static_cast<unsigned>(&s-values.data())+1;}
        void RemoveLru(Slot &s) {
            const auto n=Number(s);
            if(s.older)values[s.older-1].newer=s.newer;else if(oldest==n)oldest=s.newer;
            if(s.newer)values[s.newer-1].older=s.older;else if(newest==n)newest=s.older;
            s.older=s.newer=0;
        }
        void Touch(Slot &s) {
            RemoveLru(s);const auto n=Number(s);s.older=newest;
            if(newest)values[newest-1].newer=n;else oldest=n;
            newest=n;
        }
        void Erase(Slot &s) {
            const auto n=Number(s);auto *link=&buckets[Hash(s.stamp.heap,s.stamp.index)];
            while(*link && *link!=n)link=&values[*link-1].chain;
            if(*link)*link=s.chain;
            RemoveLru(s);s={};s.chain=free;free=n;--active;
        }
        Slot *Allocate(Id heap,unsigned index,Id incarnation,PoolCounters &counter) {
            if(!free) {
                if(!oldest){Increment(counter.drops);return nullptr;}
                Erase(values[oldest-1]);Increment(counter.evictions);
            }
            const auto n=free;auto &s=values[n-1];free=s.chain;s={};++active;
            s.stamp.heap=heap;s.stamp.index=index;s.incarnation=incarnation;
            const auto hash=Hash(heap,index);s.chain=buckets[hash];buckets[hash]=n;return &s;
        }
        void RetireHeap(Id id) {
            for(auto &s:values)if(s.stamp.heap==id)Erase(s);
        }
    };
    struct Range {Id base=0,end=0,prefix_end=0;unsigned heap=0;};
    mutable std::mutex mutex;
    LiveTable<Heap,HeapCapacity> heaps;
    LiveTable<Resource,ResourceCapacity> resources;
    mutable Slots<CpuSlotCapacity> cpu_slots;
    mutable Slots<GpuSlotCapacity> gpu_slots;
    std::array<Range,HeapCapacity> cpu_ranges{},gpu_ranges{};
    unsigned cpu_range_count=0,gpu_range_count=0;
    UnknownCopy *unknown_copies=nullptr;
    Id epoch=1,revision=0;
    Counters counters;
    struct Location {const Heap *heap=nullptr;unsigned index=0;};
    const Heap *HeapId(Id id) const {return heaps.Find(id);}
    bool UnknownAllLocked() const {
        for(const auto *scope=unknown_copies;scope;scope=scope->next)
            if(scope->active && scope->all)return true;
        return false;
    }
    static bool UnknownRangesValid(const UnknownRange *ranges,unsigned count) {
        if(!count)return true;
        if(!ranges)return false;
        for(unsigned i=0;i<count;++i)
            if(!ranges[i].first || ranges[i].first>ranges[i].last)return false;
        return true;
    }
    static bool UnknownScopeMasksLocked(const UnknownCopy &scope,Id handle,unsigned type) {
        if(!scope.active)return false;
        if(scope.all)return true;
        if(!scope.ranges)return false;
        for(unsigned i=0;i<scope.count;++i) {
            const auto &range=scope.ranges[i];
            if(range.type==type && range.first<=handle && handle<=range.last)return true;
        }
        return false;
    }
    bool UnknownCpuHandleLocked(Id handle,unsigned type) const {
        if(UnknownAllLocked())return true;
        if(!handle)return false;
        for(const auto *scope=unknown_copies;scope;scope=scope->next) {
            if(UnknownScopeMasksLocked(*scope,handle,type))return true;
        }
        return false;
    }
    bool UnknownMaskedLocked(const Heap &heap,unsigned index) const {
        if(index>=heap.count)return false;
        const auto offset=static_cast<Id>(index)*heap.stride;
        if(heap.cpu>std::numeric_limits<Id>::max()-offset)return true;
        const auto handle=heap.cpu+offset;
        for(const auto *scope=unknown_copies;scope;scope=scope->next)
            if(UnknownScopeMasksLocked(*scope,handle,heap.type))return true;
        return false;
    }
    void InvalidateUnknownSlotsLocked(const UnknownCopy *scope=nullptr) {
        if(revision==std::numeric_limits<Id>::max()) {InvalidateLocked();return;}
        ++revision;
        const auto invalidate=[&](auto &pool) {
            for(auto &slot:pool.values) {
                if(!slot.stamp.heap)continue;
                const auto *heap=HeapId(slot.stamp.heap);
                if(!heap)continue;
                const bool masked=scope?UnknownScopeMasksLocked(*scope,
                    heap->cpu+static_cast<Id>(slot.stamp.index)*heap->stride,heap->type):
                    UnknownMaskedLocked(*heap,slot.stamp.index);
                if(!masked)continue;
                slot.value={};
                // Keep the invalidation local to this heap slot.  A stale
                // epoch makes readers report SlotUnknown, which preserves the
                // distinction from a known empty descriptor while leaving
                // unrelated slots in the current registry epoch.
                slot.stamp.epoch=epoch?epoch-1:0;
                slot.stamp.revision=revision;
                if(slot.pending)slot.overlap=true;
            }
        };
        invalidate(cpu_slots);invalidate(gpu_slots);
    }
    bool ResourceCurrent(const Resource &r) const {
        if(!r || counters.resources.coverage_lost)return false;
        const auto *current=resources.Find(r.id);
        return current && current->device==r.device && current->address==r.address;
    }
    void RebuildRanges() {
        cpu_range_count=gpu_range_count=0;
        for(unsigned i=0;i<heaps.values.size();++i) {
            const auto &h=heaps.values[i];if(!h)continue;
            const Id size=static_cast<Id>(h.count-1)*h.stride;
            cpu_ranges[cpu_range_count++]={h.cpu,h.cpu+size,0,i};
            if(h.shader_visible)gpu_ranges[gpu_range_count++]={h.gpu,h.gpu+size,0,i};
        }
        const auto sort=[](auto &ranges,unsigned count) {
            std::sort(ranges.begin(),ranges.begin()+count,[](const Range &a,const Range &b){return a.base<b.base;});
            Id end=0;for(unsigned i=0;i<count;++i){end=std::max(end,ranges[i].end);ranges[i].prefix_end=end;}
        };
        sort(cpu_ranges,cpu_range_count);sort(gpu_ranges,gpu_range_count);
    }
    Location Locate(Id handle,unsigned type,bool gpu) const {
        Location result;if(!handle || counters.heaps.coverage_lost)return result;
        const auto &ranges=gpu?gpu_ranges:cpu_ranges;
        const unsigned count=gpu?gpu_range_count:cpu_range_count;
        unsigned begin=0,end=count;
        while(begin<end) {const unsigned mid=begin+(end-begin)/2;if(ranges[mid].base<=handle)begin=mid+1;else end=mid;}
        while(begin) {
            const auto &range=ranges[--begin];if(range.prefix_end<handle)break;
            if(range.end<handle)continue;
            const auto &h=heaps.values[range.heap];if(h.type!=type || (handle-range.base)%h.stride)continue;
            if(result.heap)return {}; // All overlapping live ranges remain ambiguous.
            result={&h,static_cast<unsigned>((handle-range.base)/h.stride)};
        }
        return result;
    }
    const Slot *FindSlot(const Heap &h,unsigned index) const {
        return h.shader_visible?gpu_slots.Find(h.id,index):cpu_slots.Find(h.id,index);
    }
    void TouchUseLocked(const Receipt &receipt) const {
        if(!receipt)return;
        const auto *heap=HeapId(receipt.stamp.heap);if(!heap)return;
        const auto touch=[&](auto &pool) {
            auto *slot=pool.Find(heap->id,receipt.stamp.index);
            if(slot && !slot->pending && slot->stamp.epoch==receipt.stamp.epoch &&
               slot->stamp.revision==receipt.stamp.revision)pool.Touch(*slot);
        };
        if(heap->shader_visible)touch(gpu_slots);else touch(cpu_slots);
    }
    Receipt Read(const Location &where,Kind expected) const {
        Receipt result;
        if(!where.heap){if(counters.heaps.coverage_lost)result.reason=Reason::CoverageLost;return result;}
        const auto &h=*where.heap;result.shader_visible=h.shader_visible;
        result.stamp.heap=h.id;result.stamp.index=where.index;
        if(UnknownMaskedLocked(h,where.index)){result.reason=Reason::WritePending;return result;}
        const auto *s=FindSlot(h,where.index);
        if(!s || s->stamp.epoch!=epoch){result.reason=Reason::SlotUnknown;return result;}
        result.stamp=s->stamp;
        if(s->pending){result.reason=Reason::WritePending;return result;}
        result.view=s->value;
        if(!s->value.known){result.reason=Reason::ViewUnknown;return result;}
        if(counters.resources.coverage_lost){result.reason=Reason::CoverageLost;return result;}
        if(!ResourceCurrent(s->value.resource)){result.reason=Reason::ResourceRetired;return result;}
        if(s->value.resource.device!=h.device){result.reason=Reason::WrongDevice;return result;}
        if(expected!=Kind::Unknown && s->value.kind!=expected){result.reason=Reason::WrongKind;return result;}
        result.reason=Reason::None;return result;
    }
    bool CurrentLocked(const Receipt &r) const {
        if(!r || r.stamp.epoch!=epoch || counters.heaps.coverage_lost)return false;
        const auto *h=HeapId(r.stamp.heap);if(!h || r.stamp.index>=h->count)return false;
        if(UnknownMaskedLocked(*h,r.stamp.index))return false;
        // Recheck uniqueness: a later overlapping live heap invalidates receipts.
        const auto where=Locate(h->cpu+static_cast<Id>(r.stamp.index)*h->stride,h->type,false);
        if(where.heap!=h)return false;
        if(r.shader_visible && Locate(h->gpu+static_cast<Id>(r.stamp.index)*h->stride,h->type,true).heap!=h)return false;
        const auto now=Read(where,r.view.kind);
        return now && now.stamp.revision==r.stamp.revision && SameView(now.view,r.view);
    }
    template<unsigned Capacity> Write BeginIn(Slots<Capacity> &pool,PoolCounters &counter,
                                              const Location &where,const View &value) {
        Write write;const auto &h=*where.heap;
        if(UnknownMaskedLocked(h,where.index)){Increment(counters.unknown_writes);return write;}
        Increment(revision);
        if(revision==std::numeric_limits<Id>::max()){InvalidateLocked();return write;}
        auto *s=pool.Find(h.id,where.index);
        if(!s) {
            const auto evictions=counter.evictions;s=pool.Allocate(h.id,where.index,revision,counter);
            if(counter.evictions!=evictions)Increment(counters.evictions);
            if(!s){Increment(counters.unknown_writes);return write;}
        }
        pool.RemoveLru(*s);
        if(s->pending){s->overlap=true;Increment(counters.overlaps);}else s->overlap=false;
        if(s->pending==std::numeric_limits<unsigned>::max()){InvalidateLocked();return write;}
        ++s->pending;s->stamp={h.id,revision,epoch,where.index};s->value={};
        write.destination=s->stamp;write.incarnation=s->incarnation;
        if(value.resource.device==h.device)write.value=value;
        Increment(counters.writes);return write;
    }
    Write BeginLocked(const Location &where,const View &value) {
        if(!where.heap){Increment(counters.unknown_writes);return {};}
        return where.heap->shader_visible?BeginIn(gpu_slots,counters.gpu_slots,where,value):
            BeginIn(cpu_slots,counters.cpu_slots,where,value);
    }
    template<unsigned Capacity> void FinishIn(Slots<Capacity> &pool,const Write &write) {
        const auto &stamp=write.destination;auto *s=pool.Find(stamp.heap,stamp.index);
        if(!s || s->incarnation!=write.incarnation || !s->pending)return;
        --s->pending;
        if(!s->pending)pool.Touch(*s);
        if(s->stamp.revision!=stamp.revision || s->pending || s->overlap || stamp.epoch!=epoch || !HeapId(stamp.heap))return;
        if(write.value.known && ResourceCurrent(write.value.resource) &&
           (!write.copy || (!write.source.shader_visible && CurrentLocked(write.source))))s->value=write.value;
    }
    void InvalidateLocked(){Increment(epoch);Increment(counters.invalidations);}
public:
    void ObserveResource(Resource resource) {
        if(!resource)return;std::lock_guard guard(mutex);
        const auto *old=resources.Find(resource.id);
        if(old) {
            if(old->device!=resource.device || old->address!=resource.address) {
                Increment(counters.resources.ambiguities);counters.resources.coverage_lost=true;InvalidateLocked();
            }
            return;
        }
        // Cold identity publication checks address uniqueness once. Copies and
        // receipt validation consult the flag and full-key index, never scan it.
        for(const auto &r:resources.values)if(r.id && r.address==resource.address) {
            Increment(counters.resources.ambiguities);counters.resources.coverage_lost=true;InvalidateLocked();break;
        }
        if(!resources.Insert(resource)) {
            Increment(counters.resources.drops);counters.resources.coverage_lost=true;InvalidateLocked();
        }
    }
    bool ObserveHeap(Heap heap) {
        if(!heap || heap.type>3 || (heap.shader_visible&&!heap.gpu) ||
           (heap.count-1)>((std::numeric_limits<Id>::max()-heap.cpu)/heap.stride) ||
           (heap.gpu && (heap.count-1)>((std::numeric_limits<Id>::max()-heap.gpu)/heap.stride)))return false;
        std::lock_guard guard(mutex);
        if(const auto *old=heaps.Find(heap.id)) {
            if(old->device==heap.device && old->address==heap.address && old->cpu==heap.cpu && old->gpu==heap.gpu &&
               old->count==heap.count && old->stride==heap.stride && old->type==heap.type && old->shader_visible==heap.shader_visible)return true;
            Increment(counters.heaps.ambiguities);counters.heaps.coverage_lost=true;InvalidateLocked();return false;
        }
        if(!heaps.Insert(heap)) {
            Increment(counters.heaps.drops);counters.heaps.coverage_lost=true;InvalidateLocked();return false;
        }
        // Losing uniqueness must also retire earlier slot revisions; otherwise
        // writes during the ambiguous interval could leave a stale receipt that
        // appears current again after the second heap is destroyed.
        for(const auto &other:heaps.values)if(other.id && other.id!=heap.id) {
            const Id span=static_cast<Id>(heap.count-1)*heap.stride,other_span=static_cast<Id>(other.count-1)*other.stride;
            if(heap.address==other.address) {
                Increment(counters.heaps.ambiguities);counters.heaps.coverage_lost=true;InvalidateLocked();break;
            }
            if(other.type!=heap.type)continue;
            if((heap.cpu<=other.cpu+other_span && other.cpu<=heap.cpu+span) ||
               (heap.shader_visible && other.shader_visible && heap.gpu<=other.gpu+other_span && other.gpu<=heap.gpu+span)) {
                Increment(counters.heaps.ambiguities);InvalidateLocked();break;
            }
        }
        RebuildRanges();return true;
    }
    void RetireResource(Id id) {
        std::lock_guard guard(mutex);if(resources.Erase(id))Increment(counters.resources_retired);
    }
    void RetireHeap(Id id) {
        std::lock_guard guard(mutex);
        if(heaps.Erase(id)) {
            cpu_slots.RetireHeap(id);gpu_slots.RetireHeap(id);RebuildRanges();Increment(counters.heaps_retired);
        }
    }
    void RetireDevice(Id device) {
        std::lock_guard guard(mutex);
        // Erasure may move the next entry into this slot; revisit it.
        for(unsigned i=0;i<resources.values.size();) {
            const auto &r=resources.values[i];if(r.id && r.device==device){resources.Erase(r.id);Increment(counters.resources_retired);}else ++i;
        }
        for(unsigned i=0;i<heaps.values.size();) {
            const auto &h=heaps.values[i];if(h.id && h.device==device) {
                const auto id=h.id;cpu_slots.RetireHeap(id);gpu_slots.RetireHeap(id);heaps.Erase(id);Increment(counters.heaps_retired);
            }else ++i;
        }
        RebuildRanges();
        // Capacity loss is sticky: an untracked live object's retirement cannot
        // certify that every omitted alias has gone away, even on another device.
    }
    Resource ResourceAt(Id address) const {
        std::lock_guard guard(mutex);Resource result;
        if(counters.resources.coverage_lost)return result;
        for(const auto &r:resources.values)if(r && r.address==address){if(result)return {};result=r;}
        return result;
    }
    Heap HeapAt(Id address) const {
        std::lock_guard guard(mutex);Heap result;if(counters.heaps.coverage_lost)return result;
        for(const auto &h:heaps.values)if(h && h.address==address){if(result)return {};result=h;}
        return result;
    }
    Receipt Cpu(Id handle,unsigned type,Kind expected=Kind::Unknown) const {
        std::lock_guard guard(mutex);
        if(UnknownCpuHandleLocked(handle,type)){Receipt result;result.reason=Reason::WritePending;return result;}
        return Read(Locate(handle,type,false),expected);
    }
    Receipt Gpu(Id handle,unsigned type,Kind expected=Kind::Unknown) const {
        std::lock_guard guard(mutex);
        if(UnknownAllLocked()){Receipt result;result.reason=Reason::WritePending;return result;}
        return Read(Locate(handle,type,true),expected);
    }
    Id OffsetCpu(Id handle,unsigned type,unsigned offset) const {
        std::lock_guard guard(mutex);const auto where=Locate(handle,type,false);
        if(!where.heap || offset>=where.heap->count-where.index)return 0;
        return handle+static_cast<Id>(offset)*where.heap->stride;
    }
    // The range array is borrowed for the lifetime of scope.  The caller must
    // keep both the node and its array alive until EndUnknownCopy returns.
    // A malformed range is treated as a global unknown window so a bad
    // observer cannot publish a guessed descriptor value.
    void BeginUnknownCopy(UnknownCopy &scope,const UnknownRange *ranges,unsigned count,bool all=false) {
        std::lock_guard guard(mutex);
        if(scope.active)return;
        const bool global=all || !UnknownRangesValid(ranges,count);
        scope.ranges=global?nullptr:ranges;scope.count=global?0:count;
        scope.all=global;scope.next=nullptr;scope.active=true;
        scope.next=unknown_copies;unknown_copies=&scope;
        if(global){Increment(counters.global_copies);InvalidateLocked();}
        else if(count){Increment(counters.scoped_copies);InvalidateUnknownSlotsLocked(&scope);}
    }
    void EndUnknownCopy(UnknownCopy &scope) {
        std::lock_guard guard(mutex);
        if(!scope.active)return;
        const bool global=scope.all;
        if(global)InvalidateLocked();
        else if(scope.count)InvalidateUnknownSlotsLocked(&scope);
        UnknownCopy **link=&unknown_copies;
        while(*link && *link!=&scope)link=&(*link)->next;
        if(*link==&scope)*link=scope.next;
        scope.ranges=nullptr;scope.count=0;scope.all=false;scope.next=nullptr;scope.active=false;
    }
    Write BeginWrite(Id cpu,unsigned type,const View &value) {
        std::lock_guard guard(mutex);
        if(UnknownCpuHandleLocked(cpu,type)){Increment(counters.unknown_writes);return {};}
        return BeginLocked(Locate(cpu,type,false),value);
    }
    Write BeginCopy(Id destination,Id source,unsigned type) {
        std::lock_guard guard(mutex);
        if(UnknownCpuHandleLocked(destination,type)) {
            Increment(counters.unknown_writes);Increment(counters.copies);
            Write write;write.copy=true;return write;
        }
        const auto from=Read(Locate(source,type,false),Kind::Unknown);
        if(from && !from.shader_visible)TouchUseLocked(from);
        auto write=BeginLocked(Locate(destination,type,false),from && !from.shader_visible?from.view:View{});
        write.source=from;write.copy=true;Increment(counters.copies);return write;
    }
    void Finish(const Write &write) {
        std::lock_guard guard(mutex);if(!write.destination.heap)return;
        const auto *h=HeapId(write.destination.heap);if(!h)return;
        if(h->shader_visible)FinishIn(gpu_slots,write);else FinishIn(cpu_slots,write);
    }
    bool Current(const Receipt &receipt) const {std::lock_guard guard(mutex);return CurrentLocked(receipt);}
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
        pair.reason=Reason::None;TouchUseLocked(pair.cpu);TouchUseLocked(pair.gpu);return pair;
    }
    void Invalidate(){std::lock_guard guard(mutex);InvalidateLocked();}
    Counters SnapshotCounters() const {
        std::lock_guard guard(mutex);auto result=counters;
        result.heaps.active=heaps.active;result.heaps.capacity=HeapCapacity;
        result.resources.active=resources.active;result.resources.capacity=ResourceCapacity;
        result.cpu_slots.active=cpu_slots.active;result.cpu_slots.capacity=CpuSlotCapacity;
        result.gpu_slots.active=gpu_slots.active;result.gpu_slots.capacity=GpuSlotCapacity;return result;
    }
    Inventory ResourceInventory(Id id) const {
        std::lock_guard guard(mutex);Inventory inventory;inventory.resource=id;
        const auto *resource=resources.Find(id);if(!resource || !ResourceCurrent(*resource))return inventory;
        inventory.lifetime_known=true;inventory.device=resource->device;
        const auto collect=[&](const auto &pool) {
            for(const auto &slot:pool.values) {
                if(slot.stamp.epoch!=epoch || slot.pending || !slot.value.known || slot.value.resource.id!=id ||
                   !HeapId(slot.stamp.heap) || !ResourceCurrent(slot.value.resource))continue;
                Receipt receipt;receipt.stamp=slot.stamp;receipt.view=slot.value;receipt.reason=Reason::None;
                if(!CurrentLocked(receipt))continue;
                ++inventory.slots;const auto &view=slot.value;
                if(view.kind==Kind::Srv)++inventory.srv;
                if(view.kind==Kind::Uav)++inventory.uav;
                if(view.kind==Kind::Rtv)++inventory.rtv;
                if(!view.shape_known || !view.typed)++inventory.unknown_shapes;
                bool found=false;
                for(unsigned i=0;i<inventory.count;++i)if(inventory.formats[i].kind==view.kind && inventory.formats[i].format==view.format) {
                    ++inventory.formats[i].count;found=true;break;
                }
                if(found)continue;
                if(inventory.count<6)inventory.formats[inventory.count++]={view.format,1,view.kind};else inventory.overflow=true;
            }
        };
        collect(cpu_slots);collect(gpu_slots);return inventory;
    }
};
static_assert(sizeof(Registry<>)<=16u*1024u*1024u,"descriptor registry exceeds the fixed 16 MiB budget");
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
    case Reason::CoverageLost:return "identity-coverage-lost";
    default:return "bounded-capacity-exceeded";
    }
}
} // namespace nr_descriptor
