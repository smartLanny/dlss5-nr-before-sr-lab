#define CINTERFACE
#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include "nr_allocation_ledger.h"
#include "ci_warp_runtime.h"

using namespace nr_allocation_ledger;
void Check(bool value,const char* message){if(!value){std::fprintf(stderr,"FAIL %s\n",message);std::exit(1);}}
void Cpu() {
    const void* owned=reinterpret_cast<const void*>(0x1000);
    const void* game=reinterpret_cast<const void*>(0x2000);
    Builder live;
    live.Known(Category::CoreSlot,Phase::Live,Heap::Default,owned,65536);
    live.Known(Category::CoreSlot,Phase::Live,Heap::Default,owned,65536);
    live.Known(Category::CoreSlot,Phase::Live,Heap::Default,game,65536,Ownership::Borrowed);
    const auto a=Aggregate(live.Get(),Category::CoreSlot,Phase::Live);
    Check(a.bytes==65536&&a.resources==1&&live.Get().aliases==1,"owned alias counted twice or game input counted");
    // A moved owner is classified at its current location, not both locations.
    Builder pending;
    pending.Known(Category::CoreSlot,Phase::Pending,Heap::Default,owned,65536);
    Check(Aggregate(pending.Get(),Category::CoreSlot,Phase::Live).resources==0&&
          Aggregate(pending.Get(),Category::CoreSlot,Phase::Pending).bytes==65536,"owner move not reclassified");
    Builder failed;
    failed.Known(Category::CoreSlot,Phase::Live,Heap::Default,nullptr,65536);
    Check(!Aggregate(failed.Get(),Category::CoreSlot,Phase::Live).resources,"failed create became allocation");
    failed.Known(Category::CoreSlot,Phase::Live,Heap::Default,reinterpret_cast<const void*>(0x3000),0);
    Check(Aggregate(failed.Get(),Category::CoreSlot,Phase::Live).unknown==1&&
          !failed.Get().sample_complete,"invalid size became known zero");
    Builder overflow;
    overflow.Known(Category::CoreSlot,Phase::Live,Heap::Default,owned,
                   std::numeric_limits<std::uint64_t>::max()-1);
    overflow.Known(Category::CoreSlot,Phase::Live,Heap::Default,game,2);
    const auto b=Aggregate(overflow.Get(),Category::CoreSlot,Phase::Live);
    Check(b.bytes==std::numeric_limits<std::uint64_t>::max()&&b.overflow&&
          !overflow.Get().sample_complete,"overflow wrapped or claimed complete");
    std::string overflow_text;
    WriteText(overflow.Get(),"start",[&](const char* row){overflow_text+=row;});
    Check(overflow_text.find("start.allocation.overflow=1\n")!=std::string::npos,
          "F8 summary concealed cell overflow");
    pending.Model(2,Phase::Pending);pending.Destroying();
    Check(pending.Get().model_instances[2][unsigned(Phase::Pending)]==1&&
          pending.Get().destroying_contexts==1&&!pending.Get().sample_complete,"model or destroying state omitted");
    char line[192]{};
    Check(FormatRow(line,sizeof(line),"start",pending.Get(),Category::CoreSlot,Phase::Pending)&&
          std::strstr(line,"start.allocation.coreSlot.pending=bytes:65536,resources:1,unknownGroups:0,overflow:0"),
          "F8 production row format lost move phase or values");
    Builder heaps;
    heaps.Known(Category::Capture,Phase::Retired,Heap::Default,owned,65536);
    heaps.Known(Category::Capture,Phase::Retired,Heap::Readback,game,4096);
    heaps.Unknown(Category::Capture,Phase::Retired,Heap::Upload);
    std::string heap_text;
    WriteText(heaps.Get(),"end",[&](const char* row){heap_text+=row;});
    Check(heap_text.find("end.allocation.capture.retired=bytes:69632,resources:2,unknownGroups:1")!=std::string::npos&&
          heap_text.find("end.allocation.capture.retired.heap.default=bytes:65536,resources:1,unknownGroups:0")!=std::string::npos&&
          heap_text.find("end.allocation.capture.retired.heap.readback=bytes:4096,resources:1,unknownGroups:0")!=std::string::npos&&
          heap_text.find("end.allocation.capture.retired.heap.upload=bytes:0,resources:0,unknownGroups:1")!=std::string::npos&&
          heap_text.find("end.allocation.capture.retired.heap.other=")==std::string::npos&&
          heap_text.find("end.allocation.knownBytesLowerBound=69632\n")!=std::string::npos&&
          heap_text.find("end.allocation.heapRows=emit nonempty")!=std::string::npos,
          "F8 heap rows omitted unknown, mislabelled heap or double counted subtotal");
    nr_dual::dx12::Surfaces reduced{};
    reduced.primary=reinterpret_cast<ID3D12Resource*>(0x1100);
    reduced.input=reinterpret_cast<ID3D12Resource*>(0x1200);
    reduced.output=reinterpret_cast<ID3D12Resource*>(0x1300);
    reduced.low_valid=reinterpret_cast<ID3D12Resource*>(0x1400);
    reduced.low_delta=reinterpret_cast<ID3D12Resource*>(0x1500);
    reduced.owns_low_valid=true;
    Builder reduced_owner;
    ForEachOwnedSpatialResource(reduced,[&](ID3D12Resource* resource){
        reduced_owner.Known(Category::Dual,Phase::Retired,Heap::Default,resource,4096);
    });
    Check(Aggregate(reduced_owner.Get(),Category::Dual,Phase::Retired).resources==4,
          "reduced owner lost its validity allocation");
    auto cumulative=reduced;
    cumulative.input=cumulative.primary;
    cumulative.owns_low_valid=false;
    Builder successor_owner;
    ForEachOwnedSpatialResource(cumulative,[&](ID3D12Resource* resource){
        successor_owner.Known(Category::Compat,Phase::Live,Heap::Default,resource,4096);
    });
    Check(Aggregate(successor_owner.Get(),Category::Compat,Phase::Live).resources==2&&
          Aggregate(successor_owner.Get(),Category::Compat,Phase::Live).unknown==0,
          "cumulative successor counted borrowed input/valid or invented unknown");
    Builder stable;
    stable.CompatRetiring(false);stable.DetachedQuarantine(false);
    Check(!stable.Get().compat_retiring_unknown&&!stable.Get().detached_quarantined_unknown&&
          Aggregate(stable.Get(),Category::Compat,Phase::Destroying).unknown==0&&
          Aggregate(stable.Get(),Category::CoreSlot,Phase::Destroying).unknown==0,
          "inactive retirement falsely marked unknown");
    Builder detached;
    detached.CompatRetiring(true);detached.DetachedQuarantine(true);
    std::string detached_text;
    WriteText(detached.Get(),"end",[&](const char* row){detached_text+=row;});
    Check(detached.Get().compat_retiring_unknown&&detached.Get().detached_quarantined_unknown&&
          Aggregate(detached.Get(),Category::Compat,Phase::Destroying).unknown==1&&
          Aggregate(detached.Get(),Category::CoreSlot,Phase::Destroying).unknown==1&&
          Aggregate(detached.Get(),Category::PrivateSr,Phase::Destroying).unknown==1&&
          detached_text.find("end.allocation.compatRetiringUnknown=1\n")!=std::string::npos&&
          detached_text.find("end.allocation.detachedQuarantinedUnknown=1\n")!=std::string::npos&&
          detached_text.find("destroying:unknown,gpuBytes:unknown")!=std::string::npos,
          "detached or compat lock-out reported as released zero");
    char ticks[160]{};
    Check(FormatSampleTicks(ticks,sizeof(ticks),"end",101,103)&&
          std::strcmp(ticks,"end.allocation.sampleStartedTickMs=101\nend.allocation.sampleFinishedTickMs=103\n")==0,
          "actual sample tick formatter lost interval or phase");
    Snapshot widest{};
    for(unsigned c=0;c<categories;++c)for(unsigned p=0;p<phases;++p)
        for(unsigned h=0;h<nr_allocation_ledger::heaps;++h){
            auto& cell=widest.cells[c][p][h];
            cell.bytes=std::numeric_limits<std::uint64_t>::max();
            cell.resources=cell.unknown=std::numeric_limits<unsigned>::max();
            cell.overflow=true;
        }
    for(auto& model:widest.model_instances)for(auto& count:model)
        count=std::numeric_limits<unsigned>::max();
    widest.destroying_contexts=widest.aliases=std::numeric_limits<unsigned>::max();
    widest.compat_retiring_unknown=widest.detached_quarantined_unknown=true;
    widest.model_destroying_unknown=widest.seen_overflow=true;
    widest.sample_complete=false;
    std::string wide;
    WriteText(widest,"start",[&](const char* row){wide+=row;});
    char wide_ticks[160]{};
    Check(FormatSampleTicks(wide_ticks,sizeof(wide_ticks),"start",
          std::numeric_limits<std::uint64_t>::max(),std::numeric_limits<std::uint64_t>::max()),
          "F8 widest tick row truncated");
    wide+=wide_ticks;
    unsigned lines=0;for(char ch:wide)if(ch=='\n')++lines;
    Check(lines==36+144+26&&wide.find("start.allocation.fg.destroying.heap.other=")!=std::string::npos,
          "F8 widest schema silently omitted a category, phase or heap row");
    std::printf("F8 maximum formatted one-snapshot bytes=%zu lines=%u (36 category/phase, 144 heap rows)\n",
                wide.size(),lines);
    std::puts("CPU ownership, borrowed references, alias, move, failure, overflow, F8 row PASS");
}
void Warp() {
    Check(SUCCEEDED(nr_test_warp::Prepare()),"Prepare pinned CI WARP");
    IDXGIFactory4* factory=nullptr;IDXGIAdapter* adapter=nullptr;ID3D12Device* device=nullptr;
    Check(SUCCEEDED(CreateDXGIFactory1(IID_IDXGIFactory4,reinterpret_cast<void**>(&factory))),"DXGI factory");
    Check(SUCCEEDED(IDXGIFactory4_EnumWarpAdapter(factory,IID_IDXGIAdapter,reinterpret_cast<void**>(&adapter))),"WARP adapter");
    Check(SUCCEEDED(D3D12CreateDevice(reinterpret_cast<IUnknown*>(adapter),D3D_FEATURE_LEVEL_11_0,
        IID_ID3D12Device,reinterpret_cast<void**>(&device))),"WARP device");
    Check(SUCCEEDED(nr_test_warp::Verify("allocation-ledger")),"Verify actual CI WARP");
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width=100000;desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
    desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES properties{};properties.Type=D3D12_HEAP_TYPE_DEFAULT;
    properties.CreationNodeMask=properties.VisibleNodeMask=1;
    ID3D12Resource* resource=nullptr;
    Check(SUCCEEDED(ID3D12Device_CreateCommittedResource(device,&properties,D3D12_HEAP_FLAG_NONE,&desc,
        D3D12_RESOURCE_STATE_COMMON,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(&resource))),"WARP resource");
    const auto expected=ID3D12Device_GetResourceAllocationInfo(device,properties.CreationNodeMask,1,&desc).SizeInBytes;
    const auto measured=Measure(resource);
    Check(measured.known&&measured.heap==Heap::Default&&measured.bytes==expected&&expected>=desc.Width,
          "production helper differs from actual WARP allocation info");
    Builder ledger;
    ledger.Resource(Category::CoreSlot,Phase::Live,resource);
    ledger.Resource(Category::CoreSlot,Phase::Live,resource);
    ledger.Resource(Category::CoreSlot,Phase::Live,resource,Ownership::Borrowed);
    Check(Aggregate(ledger.Get(),Category::CoreSlot,Phase::Live).bytes==expected&&
          Aggregate(ledger.Get(),Category::CoreSlot,Phase::Live).resources==1,"WARP alias counted twice");
    std::string f8;
    WriteText(ledger.Get(),"start",[&](const char* line){f8+=line;});
    const auto amount="start.allocation.knownBytesLowerBound="+std::to_string(expected)+"\n";
    Check(f8.find(amount)!=std::string::npos&&
          f8.find("start.allocation.coreSlot.live=bytes:"+std::to_string(expected)+",resources:1")!=std::string::npos&&
          f8.find("start.allocation.opaqueModelGpuBytes=unknown")!=std::string::npos&&
          f8.find("start.allocation.unit=GetResourceAllocationInfo.SizeInBytes; not resident")!=std::string::npos,
          "production F8 formatter omitted measured bytes or scope");
    Check(ID3D12Resource_Release(resource)==0,"WARP resource retained after owner release");
    ID3D12Device_Release(device);
    IDXGIAdapter_Release(adapter);IDXGIFactory4_Release(factory);
    Builder after_release;
    Check(Aggregate(after_release.Get(),Category::CoreSlot,Phase::Live).bytes==0,"released allocation persisted");
    std::puts("WARP owned D3D12 allocation-info, F8 text and release PASS");
}
void SpatialWarp() {
    Check(SUCCEEDED(nr_test_warp::Prepare()),"Prepare pinned CI WARP");
    IDXGIFactory4* factory=nullptr;IDXGIAdapter* adapter=nullptr;ID3D12Device* device=nullptr;
    Check(SUCCEEDED(CreateDXGIFactory1(IID_IDXGIFactory4,reinterpret_cast<void**>(&factory))),"spatial DXGI factory");
    Check(SUCCEEDED(IDXGIFactory4_EnumWarpAdapter(factory,IID_IDXGIAdapter,reinterpret_cast<void**>(&adapter))),"spatial WARP adapter");
    Check(SUCCEEDED(D3D12CreateDevice(reinterpret_cast<IUnknown*>(adapter),D3D_FEATURE_LEVEL_11_0,
        IID_ID3D12Device,reinterpret_cast<void**>(&device))),"spatial WARP device");
    Check(SUCCEEDED(nr_test_warp::Verify("allocation-ledger-spatial")),"Verify actual CI WARP");
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask=heap.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width=desc.Height=64;desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource* primary=nullptr;
    Check(SUCCEEDED(ID3D12Device_CreateCommittedResource(device,&heap,D3D12_HEAP_FLAG_NONE,&desc,
        D3D12_RESOURCE_STATE_COMMON,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(&primary))),"spatial primary");
    std::string error;
    auto* pipelines=nr_dual::dx12::CreatePipelines(device,error);
    Check(pipelines!=nullptr,error.c_str());
    const auto half=nr_dual::MakePlan({2,{1,2}},{64,64},{});
    auto* reduced=nr_dual::dx12::CreateSpatialSlot(pipelines,primary,half,
        nr_dual::ModelDomain::DirectValue,256ull<<20,error);
    Check(reduced!=nullptr,error.c_str());
    const auto base=nr_dual::dx12::GetSurfaces(reduced);
    Check(base.owns_low_valid&&base.input!=base.primary&&base.low_valid&&base.low_delta,
          "real MatchedResidual must own low input, valid and delta");
    const auto same=nr_dual::MakePlan({2,{1,1}},base.secondary_extent,{});
    auto* successor=nr_dual::dx12::CreateSpatialSlot(pipelines,base.output,same,
        nr_dual::ModelDomain::DirectValue,256ull<<20,error,nullptr,nullptr,&base);
    Check(successor!=nullptr,error.c_str());
    const auto next=nr_dual::dx12::GetSurfaces(successor);
    Check(!next.owns_low_valid&&next.low_valid==base.low_valid&&
          next.input==next.primary&&next.low_delta,
          "real cumulative successor must borrow valid and primary");
    const auto account=[&](Builder& builder,Category category,const nr_dual::dx12::Surfaces& surfaces){
        ForEachOwnedSpatialResource(surfaces,[&](ID3D12Resource* resource){
            builder.Resource(category,Phase::Live,resource);
        });
    };
    Builder base_only,successor_only,compat;
    account(base_only,Category::Dual,base);
    account(successor_only,Category::Compat,next);
    account(compat,Category::Compat,base);
    account(compat,Category::Compat,next);
    const ID3D12Resource* owned[]={base.input,base.output,base.low_valid,base.low_delta,next.output,next.low_delta};
    std::uint64_t expected=0;
    for(const auto* resource:owned){
        const auto d=ID3D12Resource_GetDesc(const_cast<ID3D12Resource*>(resource));
        expected+=ID3D12Device_GetResourceAllocationInfo(device,1,1,&d).SizeInBytes;
    }
    const auto measured=Aggregate(compat.Get(),Category::Compat,Phase::Live);
    Check(Aggregate(base_only.Get(),Category::Dual,Phase::Live).resources==4&&
          Aggregate(successor_only.Get(),Category::Compat,Phase::Live).resources==2&&
          measured.resources==6&&measured.unknown==0&&measured.bytes==expected,
          "real dual/compat spatial ownership, borrowed valid or allocInfo bytes wrong");
    std::string f8;
    WriteText(compat.Get(),"start",[&](const char* line){f8+=line;});
    Check(f8.find("start.allocation.compat.live=bytes:"+std::to_string(expected)+",resources:6,unknownGroups:0")!=std::string::npos&&
          f8.find("start.allocation.knownBytesLowerBound="+std::to_string(expected)+"\n")!=std::string::npos,
          "actual compat spatial allocations missing from F8 formatter");
    std::printf("WARP compat spatial F8 one-snapshot bytes=%zu\n",f8.size());
    ID3D12CommandQueue* queue=nullptr;D3D12_COMMAND_QUEUE_DESC q{};
    Check(SUCCEEDED(ID3D12Device_CreateCommandQueue(device,&q,IID_ID3D12CommandQueue,
        reinterpret_cast<void**>(&queue))),"spatial retirement queue");
    ID3D12Fence* fence=nullptr;
    Check(SUCCEEDED(ID3D12Device_CreateFence(device,0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,
        reinterpret_cast<void**>(&fence))),"spatial retirement fence");
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Check(event!=nullptr,"spatial event");
    Check(SUCCEEDED(ID3D12CommandQueue_Signal(queue,fence,1))&&
          SUCCEEDED(ID3D12Fence_SetEventOnCompletion(fence,1,event))&&
          WaitForSingleObject(event,10000)==WAIT_OBJECT_0,"spatial exact fence");
    const auto retired=[](void* pointer)->bool {
        return ID3D12Fence_GetCompletedValue(static_cast<ID3D12Fence*>(pointer))>=1;
    };
    Check(nr_dual::dx12::DestroySpatialSlotAfterRetirement(successor,retired,fence)&&
          nr_dual::dx12::DestroySpatialSlotAfterRetirement(reduced,retired,fence)&&
          nr_dual::dx12::DestroyPipelinesAfterRetirement(pipelines,retired,fence),"spatial release after fence");
    CloseHandle(event);ID3D12Fence_Release(fence);ID3D12CommandQueue_Release(queue);
    Check(ID3D12Resource_Release(primary)==0,"spatial primary retained after release");
    ID3D12Device_Release(device);IDXGIAdapter_Release(adapter);IDXGIFactory4_Release(factory);
    std::puts("WARP real MatchedResidual/cumulative ownership, compat F8 bytes and fence release PASS");
}
int main(int argc,char** argv){Cpu();if(argc>1&&std::strcmp(argv[1],"--warp")==0){Warp();SpatialWarp();}}
