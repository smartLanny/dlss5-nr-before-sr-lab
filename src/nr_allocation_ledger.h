#pragma once
#define NR_ALLOCATION_LEDGER_ENABLED 1
#include <array>
#include <cstdio>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <new>
#include <d3d12.h>
#include "nr_dual_spatial_d3d12.hpp"

// F8-only accounting of allocations made by Core. These are D3D12 allocation
// sizes, not residency, DXGI process usage, or opaque model/PSO memory.
namespace nr_allocation_ledger {
enum class Category : unsigned { CoreSlot, Experiment, Capture, Dual, PrivateSr, Temporal, Pool, Compat, Fg, Count };
enum class Phase : unsigned { Live, Retired, Pending, Destroying, Count };
enum class Heap : unsigned { Default, Readback, Upload, Other, Count };
enum class Ownership : unsigned { Owned, Borrowed };
constexpr unsigned categories=unsigned(Category::Count), phases=unsigned(Phase::Count), heaps=unsigned(Heap::Count);
struct Cell {
    std::uint64_t bytes=0;
    unsigned resources=0, unknown=0;
    bool overflow=false;
};
enum class Role : unsigned { Source, Proxy, NeuralOutput, Result, SRIntermediate, MotionProxy, RrDepth, Other, Count };
constexpr unsigned kContexts=32,kSlots=8,kRoles=unsigned(Role::Count);
struct SlotDetail {Cell total{},roles[kRoles]{};bool occupied=false;};
struct ContextDetail {
    std::uint64_t instance=0,feature_generation=0,handle=0,sr_handle=0,sampled_high_water=0;
    unsigned layer=0,slots=0,max_slots=0;int mode=0;Phase phase=Phase::Live;
    bool quarantine=false;Cell total{};SlotDetail slot[kSlots]{};
};
struct ContextDetails {ContextDetail contexts[kContexts]{};};
struct Snapshot {
    Cell cells[categories][phases][heaps]{};
    unsigned model_instances[5][phases]{};
    unsigned destroying_contexts=0, aliases=0;
    bool compat_retiring_unknown=false, detached_quarantined_unknown=false;
    bool model_destroying_unknown=false;
    bool seen_overflow=false, sample_complete=true;
    // F8 workers may have a 64 KiB stack. The bounded ~64 KiB detail block
    // belongs to the heap and snapshots share it only after collection.
    std::shared_ptr<ContextDetails> context_details;
    unsigned context_count=0,context_overflow=0;
    std::uint64_t extra_budget_bytes=0,dual_budget_bytes=0,private_budget_bytes=0,
        temporal_budget_bytes=0,capture_budget_bytes=0,replay_budget_bytes=0,face_budget_bytes=0;
};
constexpr const char* CategoryName(Category c) noexcept {
    constexpr const char* names[]={"coreSlot","experiment","capture","dual","privateSR","temporal","pool","compat","fg"};
    return names[unsigned(c)];
}
constexpr const char* PhaseName(Phase p) noexcept {
    constexpr const char* names[]={"live","retired","pending","destroying"};
    return names[unsigned(p)];
}
constexpr const char* HeapName(Heap h) noexcept {
    constexpr const char* names[]={"default","readback","upload","other"};
    return names[unsigned(h)];
}
inline void SaturatingAdd(std::uint64_t& value,std::uint64_t increment,bool& overflow) noexcept {
    if(increment>std::numeric_limits<std::uint64_t>::max()-value){value=std::numeric_limits<std::uint64_t>::max();overflow=true;}
    else value+=increment;
}
inline void SaturatingCount(unsigned& value,bool& overflow) noexcept {
    if(value==std::numeric_limits<unsigned>::max())overflow=true;
    else ++value;
}
struct Measurement {std::uint64_t bytes=0;Heap heap=Heap::Other;bool known=false;};
inline Measurement Measure(ID3D12Resource* resource) noexcept {
    Measurement result{};
    if(!resource)return result;
    ID3D12Device* device=nullptr;
    if(FAILED(ID3D12Resource_GetDevice(resource,IID_ID3D12Device,reinterpret_cast<void**>(&device)))||!device)return result;
    D3D12_HEAP_PROPERTIES properties{};D3D12_HEAP_FLAGS flags{};
    const HRESULT hr=ID3D12Resource_GetHeapProperties(resource,&properties,&flags);
    if(SUCCEEDED(hr)) {
        switch(properties.Type){
        case D3D12_HEAP_TYPE_DEFAULT:result.heap=Heap::Default;break;
        case D3D12_HEAP_TYPE_READBACK:result.heap=Heap::Readback;break;
        case D3D12_HEAP_TYPE_UPLOAD:result.heap=Heap::Upload;break;
        default:result.heap=Heap::Other;break;
        }
        // The allocation belongs to this resource's device and creation node,
        // which need not equal the active codec's device after retirement.
        const auto description=ID3D12Resource_GetDesc(resource);
        const auto allocation=ID3D12Device_GetResourceAllocationInfo(device,properties.CreationNodeMask,1,&description);
        if(allocation.SizeInBytes&&allocation.SizeInBytes!=std::numeric_limits<std::uint64_t>::max()){
            result.bytes=allocation.SizeInBytes;result.known=true;
        }
    }
    ID3D12Device_Release(device);
    return result;
}
template<class Visit>
void ForEachOwnedSpatialResource(const nr_dual::dx12::Surfaces& surfaces,Visit&& visit) {
    // The primary and a cumulative successor's low_valid are AddRefs of
    // another owner's resource. A reduced slot owns its own low input/valid.
    if(surfaces.input!=surfaces.primary)visit(surfaces.input);
    visit(surfaces.output);
    if(surfaces.owns_low_valid)visit(surfaces.low_valid);
    visit(surfaces.low_delta);
}
class Builder {
    static constexpr unsigned kSeen=2048;
    std::array<std::uintptr_t,kSeen> seen_{};
    Snapshot result_{};
    ContextDetail* context_=nullptr;
    SlotDetail* slot_=nullptr;
    Role role_=Role::Other;
    void Detail(std::uint64_t bytes,bool known) noexcept {
        const auto add=[&](Cell& cell){
            if(known){SaturatingAdd(cell.bytes,bytes,cell.overflow);SaturatingCount(cell.resources,cell.overflow);}
            else SaturatingCount(cell.unknown,cell.overflow);
        };
        if(context_)add(context_->total);
        if(slot_){add(slot_->total);add(slot_->roles[unsigned(role_)]);}
    }
    bool Claim(const void* resource) noexcept {
        const auto key=reinterpret_cast<std::uintptr_t>(resource);
        if(!key)return false;
        unsigned index=unsigned((key>>4)^(key>>13))&(kSeen-1);
        for(unsigned n=0;n<kSeen;++n,index=(index+1)&(kSeen-1)) {
            if(seen_[index]==key){SaturatingCount(result_.aliases,result_.seen_overflow);return false;}
            if(!seen_[index]){seen_[index]=key;return true;}
        }
        result_.seen_overflow=true;result_.sample_complete=false;return false;
    }
public:
    ContextDetail* BeginContext(std::uint64_t instance,std::uint64_t generation,const void* sr,const void* nr,
                               unsigned layer,int mode,Phase phase,bool quarantine,unsigned maximum) noexcept {
        slot_=nullptr;context_=nullptr;
        if(result_.context_count==kContexts){++result_.context_overflow;result_.sample_complete=false;return nullptr;}
        if(!result_.context_details){
            try{result_.context_details=std::make_shared<ContextDetails>();}
            catch(const std::bad_alloc&){++result_.context_overflow;result_.sample_complete=false;return nullptr;}
        }
        context_=&result_.context_details->contexts[result_.context_count++];
        context_->instance=instance;context_->feature_generation=generation;
        context_->sr_handle=reinterpret_cast<std::uintptr_t>(sr);context_->handle=reinterpret_cast<std::uintptr_t>(nr);
        context_->layer=layer;context_->mode=mode;context_->phase=phase;
        context_->quarantine=quarantine;context_->max_slots=maximum;return context_;
    }
    void BeginSlot(unsigned index,bool occupied) noexcept {
        slot_=context_&&index<kSlots?&context_->slot[index]:nullptr;
        if(slot_){slot_->occupied=occupied;if(occupied)++context_->slots;}
        role_=Role::Other;
    }
    void EndContext() noexcept {context_=nullptr;slot_=nullptr;}
    void ResourceRole(Role role) noexcept {role_=role;}
    const Snapshot& Get() const noexcept {return result_;}
    Snapshot Take() const noexcept {return result_;}
    void Unknown(Category category,Phase phase,Heap heap,unsigned count=1) noexcept {
        auto& cell=result_.cells[unsigned(category)][unsigned(phase)][unsigned(heap)];
        for(unsigned n=0;n<count;++n)SaturatingCount(cell.unknown,cell.overflow);
        for(unsigned n=0;n<count;++n)Detail(0,false);
        result_.sample_complete=false;
    }
    void Known(Category category,Phase phase,Heap heap,const void* identity,std::uint64_t bytes,
               Ownership ownership=Ownership::Owned) noexcept {
        if(!identity||ownership==Ownership::Borrowed)return;
        if(!Claim(identity))return;
        if(!bytes||bytes==std::numeric_limits<std::uint64_t>::max()){Unknown(category,phase,heap);return;}
        auto& cell=result_.cells[unsigned(category)][unsigned(phase)][unsigned(heap)];
        SaturatingAdd(cell.bytes,bytes,cell.overflow);SaturatingCount(cell.resources,cell.overflow);
        Detail(bytes,true);
        if(cell.overflow)result_.sample_complete=false;
    }
    void Resource(Category category,Phase phase,ID3D12Resource* resource,
                  Ownership ownership=Ownership::Owned) noexcept {
        if(!resource||ownership==Ownership::Borrowed)return;
        if(!Claim(resource))return;
        const auto measurement=Measure(resource);
        if(!measurement.known){Unknown(category,phase,measurement.heap);return;}
        auto& cell=result_.cells[unsigned(category)][unsigned(phase)][unsigned(measurement.heap)];
        SaturatingAdd(cell.bytes,measurement.bytes,cell.overflow);
        SaturatingCount(cell.resources,cell.overflow);
        Detail(measurement.bytes,true);
        if(cell.overflow)result_.sample_complete=false;
    }
    void Model(unsigned layer,Phase phase) noexcept {
        if(layer>=5){result_.sample_complete=false;return;}
        bool overflow=false;SaturatingCount(result_.model_instances[layer][unsigned(phase)],overflow);
        if(overflow)result_.sample_complete=false;
    }
    void Destroying() noexcept {
        bool overflow=false;SaturatingCount(result_.destroying_contexts,overflow);
        Unknown(Category::CoreSlot,Phase::Destroying,Heap::Default);
        if(overflow)result_.sample_complete=false;
    }
    void CompatRetiring(bool active) noexcept {
        if(!active)return;
        result_.compat_retiring_unknown=true;
        result_.model_destroying_unknown=true;
        Unknown(Category::Compat,Phase::Destroying,Heap::Other);
    }
    void DetachedQuarantine(bool uncertain) noexcept {
        if(!uncertain)return;
        result_.detached_quarantined_unknown=true;
        result_.model_destroying_unknown=true;
        for(const auto category:{Category::CoreSlot,Category::Experiment,Category::Capture,
                                 Category::Dual,Category::PrivateSr,Category::Temporal,Category::Pool})
            Unknown(category,Phase::Destroying,Heap::Other);
    }
};
inline Cell Aggregate(const Snapshot& snapshot,Category category,Phase phase) noexcept {
    Cell result{};
    for(unsigned h=0;h<heaps;++h){
        const auto& value=snapshot.cells[unsigned(category)][unsigned(phase)][h];
        SaturatingAdd(result.bytes,value.bytes,result.overflow);
        const auto add_count=[&](unsigned count,unsigned& target) {
            if(count>std::numeric_limits<unsigned>::max()-target){target=std::numeric_limits<unsigned>::max();result.overflow=true;}
            else target+=count;
        };
        add_count(value.resources,result.resources);add_count(value.unknown,result.unknown);
        result.overflow|=value.overflow;
    }
    return result;
}
template<class Emit>
void WriteContextText(const Snapshot& snapshot,const char* prefix,Emit&& emit) {
    char line[1024]{};
    std::uint64_t quarantine_bytes=0;bool quarantine_overflow=false;
    unsigned quarantine_unknown=0;
    for(unsigned c=0;c<snapshot.context_count;++c){
        const auto& context=snapshot.context_details->contexts[c];
        if(context.quarantine){
            quarantine_overflow|=context.total.overflow;
            SaturatingAdd(quarantine_bytes,context.total.bytes,quarantine_overflow);
            if(context.total.unknown>std::numeric_limits<unsigned>::max()-quarantine_unknown){
                quarantine_unknown=std::numeric_limits<unsigned>::max();quarantine_overflow=true;
            }else quarantine_unknown+=context.total.unknown;
        }
        std::snprintf(line,sizeof(line),"%s.allocation.context.%u=instance:%llu,feature:%llu,srHandle:0x%llx,nrHandle:0x%llx,mode:%d,layer:%u,state:%s,quarantine:%u,slots:%u,maxSlots:%u,knownBytesLowerBound:%llu,sampledBytesHighWater:%llu,unknownGroups:%u,modelGpuBytes:unknown\n",
            prefix,c,static_cast<unsigned long long>(context.instance),static_cast<unsigned long long>(context.feature_generation),
            static_cast<unsigned long long>(context.sr_handle),static_cast<unsigned long long>(context.handle),context.mode,context.layer+1,
            PhaseName(context.phase),context.quarantine?1u:0u,context.slots,context.max_slots,
            static_cast<unsigned long long>(context.total.bytes),static_cast<unsigned long long>(context.sampled_high_water),context.total.unknown);emit(line);
        for(unsigned s=0;s<kSlots;++s){const auto& slot=context.slot[s];
            if(!slot.occupied&&!slot.total.resources&&!slot.total.unknown)continue;
            unsigned unknown_mask=0;for(unsigned r=0;r<kRoles;++r)if(slot.roles[r].unknown)unknown_mask|=1u<<r;
            std::snprintf(line,sizeof(line),"%s.allocation.context.%u.slot.%u=occupied:%u,knownBytesLowerBound:%llu,resources:%u,unknownGroups:%u,source:%llu,Proxy:%llu,NeuralOutput:%llu,Result:%llu,SRIntermediate:%llu,MotionProxy:%llu,RRdepth:%llu,other:%llu,roleUnknownMask:0x%x\n",
                prefix,c,s,slot.occupied?1u:0u,static_cast<unsigned long long>(slot.total.bytes),slot.total.resources,slot.total.unknown,
                static_cast<unsigned long long>(slot.roles[0].bytes),static_cast<unsigned long long>(slot.roles[1].bytes),
                static_cast<unsigned long long>(slot.roles[2].bytes),static_cast<unsigned long long>(slot.roles[3].bytes),
                static_cast<unsigned long long>(slot.roles[4].bytes),static_cast<unsigned long long>(slot.roles[5].bytes),
                static_cast<unsigned long long>(slot.roles[6].bytes),static_cast<unsigned long long>(slot.roles[7].bytes),unknown_mask);emit(line);
        }
    }
    std::snprintf(line,sizeof(line),"%s.allocation.contextCoverage=count:%u,overflow:%u,quarantineKnownBytesLowerBound:%llu,quarantineUnknownGroups:%u,quarantineOverflow:%u; Core/aux table contexts only, compat/FG remain category totals; deduplicated subsets; quarantine overlaps life state; sampled high-water is F8-only, not an instantaneous peak; roleUnknownMask bits=source/Proxy/NeuralOutput/Result/SRIntermediate/MotionProxy/RRdepth/other\n",prefix,snapshot.context_count,snapshot.context_overflow,static_cast<unsigned long long>(quarantine_bytes),quarantine_unknown,quarantine_overflow?1u:0u);emit(line);
    std::snprintf(line,sizeof(line),"%s.allocation.budgets=extra:%llu,dual:%llu,privateSR:%llu,temporal:%llu,capture:%llu,replay:%llu,face:%llu; separate owner counters, overlapping payload/SDK/allocation measures; never sum or add to lower-bound\n",prefix,
        static_cast<unsigned long long>(snapshot.extra_budget_bytes),static_cast<unsigned long long>(snapshot.dual_budget_bytes),
        static_cast<unsigned long long>(snapshot.private_budget_bytes),static_cast<unsigned long long>(snapshot.temporal_budget_bytes),
        static_cast<unsigned long long>(snapshot.capture_budget_bytes),static_cast<unsigned long long>(snapshot.replay_budget_bytes),
        static_cast<unsigned long long>(snapshot.face_budget_bytes));emit(line);
}
inline bool FormatRow(char* output,std::size_t capacity,const char* prefix,
                      const Snapshot& snapshot,Category category,Phase phase) noexcept {
    const auto value=Aggregate(snapshot,category,phase);
    const int length=std::snprintf(output,capacity,
        "%s.allocation.%s.%s=bytes:%llu,resources:%u,unknownGroups:%u,overflow:%u\n",
        prefix,CategoryName(category),PhaseName(phase),
        static_cast<unsigned long long>(value.bytes),value.resources,value.unknown,value.overflow?1u:0u);
    return length>=0&&static_cast<std::size_t>(length)<capacity;
}
inline bool FormatHeapRow(char* output,std::size_t capacity,const char* prefix,
                          const Snapshot& snapshot,Category category,Phase phase,Heap heap) noexcept {
    const auto& value=snapshot.cells[unsigned(category)][unsigned(phase)][unsigned(heap)];
    // Omitted heap rows have four zero fields. The category/phase row is
    // always present and includes every heap once, including unknown groups.
    if(!value.bytes&&!value.resources&&!value.unknown&&!value.overflow)return false;
    const int length=std::snprintf(output,capacity,
        "%s.allocation.%s.%s.heap.%s=bytes:%llu,resources:%u,unknownGroups:%u,overflow:%u\n",
        prefix,CategoryName(category),PhaseName(phase),HeapName(heap),
        static_cast<unsigned long long>(value.bytes),value.resources,value.unknown,value.overflow?1u:0u);
    return length>=0&&static_cast<std::size_t>(length)<capacity;
}
inline bool FormatSampleTicks(char* output,std::size_t capacity,const char* prefix,
                              std::uint64_t started,std::uint64_t finished) noexcept {
    const int length=std::snprintf(output,capacity,
        "%s.allocation.sampleStartedTickMs=%llu\n%s.allocation.sampleFinishedTickMs=%llu\n",
        prefix,static_cast<unsigned long long>(started),prefix,static_cast<unsigned long long>(finished));
    return length>=0&&static_cast<std::size_t>(length)<capacity;
}
template<class Emit>
void WriteText(const Snapshot& snapshot,const char* prefix,Emit&& emit) {
    char line[512]{};
    const auto fixed=[&](const char* suffix,const char* value){
        if(std::snprintf(line,sizeof(line),"%s.allocation.%s=%s\n",prefix,suffix,value)>0)emit(line);
    };
    fixed("schema","owned-d3d12-v2");
    fixed("unit","GetResourceAllocationInfo.SizeInBytes; not resident, DXGI process usage or GPU budget");
    fixed("sampling","F8 worker; Core/compat/FG owner lock; async module counters are separate moments; no GPU wait");
    fixed("opaqueModelGpuBytes","unknown");
    fixed("psoGpuBytes","unknown");
    fixed("descriptorHeapGpuBytes","unknown");
    fixed("retirementMeaning","compat retiring and detached/quarantined flags indicate possible retained ownership; no exact resource count inferred");
    fixed("heapRows","emit nonempty default/readback/upload/other rows; omitted heap rows are all zero; category/phase includes them once");
    fixed("separateReportedMetrics","processMemory.local/currentUsage/budget, spatial.residentBytes, privateSR/temporal/dual texture bytes and capture budgets; none are added to knownBytesLowerBound");
    fixed("unenumerated","source-guide transport, native-output/worker handoffs, SDK/NGX internals; see unknown fields and separate existing metrics");
    std::uint64_t lower=0;bool total_overflow=false;
    for(unsigned c=0;c<categories;++c)for(unsigned p=0;p<phases;++p){
        const auto category=static_cast<Category>(c);
        const auto phase=static_cast<Phase>(p);
        if(FormatRow(line,sizeof(line),prefix,snapshot,category,phase))emit(line);
        for(unsigned h=0;h<heaps;++h)
            if(FormatHeapRow(line,sizeof(line),prefix,snapshot,category,phase,static_cast<Heap>(h)))emit(line);
        const auto value=Aggregate(snapshot,category,phase);
        total_overflow|=value.overflow;
        SaturatingAdd(lower,value.bytes,total_overflow);
    }
    if(std::snprintf(line,sizeof(line),"%s.allocation.knownBytesLowerBound=%llu\n",prefix,
        static_cast<unsigned long long>(lower))>0)emit(line);
    if(std::snprintf(line,sizeof(line),"%s.allocation.complete=%u\n",prefix,
        snapshot.sample_complete?1u:0u)>0)emit(line);
    if(std::snprintf(line,sizeof(line),"%s.allocation.overflow=%u\n",prefix,
        (total_overflow||snapshot.seen_overflow)?1u:0u)>0)emit(line);
    if(std::snprintf(line,sizeof(line),"%s.allocation.aliasesDeduplicated=%u\n",prefix,
        snapshot.aliases)>0)emit(line);
    if(std::snprintf(line,sizeof(line),"%s.allocation.destroyingContexts=%u; in-progress detach count only, excludes possible retained quarantine\n",
        prefix,snapshot.destroying_contexts)>0)emit(line);
    if(std::snprintf(line,sizeof(line),"%s.allocation.compatRetiringUnknown=%u\n",prefix,
        snapshot.compat_retiring_unknown?1u:0u)>0)emit(line);
    if(std::snprintf(line,sizeof(line),"%s.allocation.detachedQuarantinedUnknown=%u\n",prefix,
        snapshot.detached_quarantined_unknown?1u:0u)>0)emit(line);
    for(unsigned layer=0;layer<5;++layer){
        if(std::snprintf(line,sizeof(line),
            "%s.allocation.model.layer%u=live:%u,retired:%u,pending:%u,destroying:%s,gpuBytes:unknown\n",
            prefix,layer+1,snapshot.model_instances[layer][0],snapshot.model_instances[layer][1],
            snapshot.model_instances[layer][2],
            (snapshot.destroying_contexts||snapshot.model_destroying_unknown)?"unknown":"0")>0)emit(line);
    }
    WriteContextText(snapshot,prefix,emit);
}
} // namespace nr_allocation_ledger
