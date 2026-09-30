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
    std::puts("CPU ownership, borrowed references, alias, move, failure, overflow, F8 row PASS");
}
void Warp() {
    IDXGIFactory4* factory=nullptr;IDXGIAdapter* adapter=nullptr;ID3D12Device* device=nullptr;
    Check(SUCCEEDED(CreateDXGIFactory1(IID_IDXGIFactory4,reinterpret_cast<void**>(&factory))),"DXGI factory");
    Check(SUCCEEDED(IDXGIFactory4_EnumWarpAdapter(factory,IID_IDXGIAdapter,reinterpret_cast<void**>(&adapter))),"WARP adapter");
    Check(SUCCEEDED(D3D12CreateDevice(reinterpret_cast<IUnknown*>(adapter),D3D_FEATURE_LEVEL_11_0,
        IID_ID3D12Device,reinterpret_cast<void**>(&device))),"WARP device");
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
int main(int argc,char** argv){Cpu();if(argc>1&&std::strcmp(argv[1],"--warp")==0)Warp();}
