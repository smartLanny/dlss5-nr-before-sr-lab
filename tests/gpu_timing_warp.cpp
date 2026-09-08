#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define CINTERFACE
#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
template<class T>void ReleaseCom(T *&value){if(value){value->lpVtbl->Release(value);value=nullptr;}}
#include "../src/nr_gpu_timing.inl"
static void Check(bool ok,const char *what){if(!ok){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}
static void HR(HRESULT value,const char *what){if(FAILED(value)){std::fprintf(stderr,"HRESULT %08lx\n",static_cast<unsigned long>(value));Check(false,what);}}
int main(){
    IDXGIFactory4 *factory=nullptr;IDXGIAdapter *adapter=nullptr;ID3D12Device *device=nullptr;
    HR(CreateDXGIFactory1(IID_IDXGIFactory4,reinterpret_cast<void **>(&factory)),"DXGI factory");
    HR(IDXGIFactory4_EnumWarpAdapter(factory,IID_IDXGIAdapter,reinterpret_cast<void **>(&adapter)),"WARP adapter");
    HR(D3D12CreateDevice(reinterpret_cast<IUnknown *>(adapter),D3D_FEATURE_LEVEL_11_0,IID_ID3D12Device,reinterpret_cast<void **>(&device)),"WARP device");
    ID3D12CommandQueue *queue=nullptr;ID3D12CommandAllocator *allocator=nullptr;ID3D12GraphicsCommandList *list=nullptr;ID3D12Fence *fence=nullptr;
    D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    HR(ID3D12Device_CreateCommandQueue(device,&q,IID_ID3D12CommandQueue,reinterpret_cast<void **>(&queue)),"queue");
    HR(ID3D12Device_CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,reinterpret_cast<void **>(&allocator)),"allocator");
    HR(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,IID_ID3D12GraphicsCommandList,reinterpret_cast<void **>(&list)),"list");
    HR(ID3D12Device_CreateFence(device,0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,reinterpret_cast<void **>(&fence)),"fence");
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Check(event!=nullptr,"event");
    ID3D12Resource *source=nullptr,*destination=nullptr;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=262144;desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;heap.CreationNodeMask=heap.VisibleNodeMask=1;
    HR(ID3D12Device_CreateCommittedResource(device,&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_ID3D12Resource,reinterpret_cast<void **>(&source)),"source");
    heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    HR(ID3D12Device_CreateCommittedResource(device,&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,reinterpret_cast<void **>(&destination)),"destination");
    void *data=nullptr;const D3D12_RANGE no_read={0,0};HR(ID3D12Resource_Map(source,0,&no_read,&data),"source Map");std::memset(data,0x3c,static_cast<size_t>(desc.Width));ID3D12Resource_Unmap(source,0,nullptr);
    UINT64 next_fence=0;
    auto wait=[&](UINT64 value){if(ID3D12Fence_GetCompletedValue(fence)<value){HR(ID3D12Fence_SetEventOnCompletion(fence,value,event),"completion event");Check(WaitForSingleObject(event,10000)==WAIT_OBJECT_0,"real WARP queue completed");}};
    auto submit=[&](UINT64 token){HR(ID3D12GraphicsCommandList_Close(list),"close");const UINT64 value=++next_fence;g_gpu_timing.OnSubmit(token,queue,fence,value);ID3D12CommandList *lists[]={reinterpret_cast<ID3D12CommandList *>(list)};ID3D12CommandQueue_ExecuteCommandLists(queue,1,lists);HR(ID3D12CommandQueue_Signal(queue,fence,value),"queue Signal");wait(value);};
    auto reset=[&](UINT64 token){HR(ID3D12CommandAllocator_Reset(allocator),"allocator reset");HR(ID3D12GraphicsCommandList_Reset(list,allocator,nullptr),"native reset");g_gpu_timing.OnReset(token,false);};
    auto work=[&](){ID3D12GraphicsCommandList_CopyBufferRegion(list,destination,0,source,0,desc.Width);};
    auto record=[&](UINT64 token){const auto ticket=g_gpu_timing.Begin(device,list,token,{1,1,0,640,360});Check(ticket!=0,"query allocation");
        for(unsigned i=1;i<=4;++i){work();g_gpu_timing.Mark(ticket,list,static_cast<nr_gpu_timing::Boundary>(i));}
        g_gpu_timing.BeforeSr(ticket,list);work();g_gpu_timing.End(ticket,list,true);};
    record(10);Check(!g_gpu_timing.Read().available,"no readback before Execute/fence");submit(10);
    Check(!g_gpu_timing.Read().available,"completed but replayable list remains pinned");reset(10);
    auto stats=g_gpu_timing.Read();Check(stats.available&&stats.completed==1&&stats.pending==0,"real completed and invalidated query readback");
    Check(std::isfinite(stats.encoding_ms)&&stats.encoding_ms>=0&&std::isfinite(stats.feature18_ms)&&stats.feature18_ms>=0,"finite GPU elapsed values");
    record(11);submit(11);reset(11);Check(g_gpu_timing.Read().completed==2,"query storage reused for fresh recording generation");
    record(12);submit(12);const UINT64 replay=++next_fence;g_gpu_timing.OnSubmit(12,queue,fence,replay);ID3D12CommandList *lists[]={reinterpret_cast<ID3D12CommandList *>(list)};
    ID3D12CommandQueue_ExecuteCommandLists(queue,1,lists);HR(ID3D12CommandQueue_Signal(queue,fence,replay),"replay fence");wait(replay);reset(12);
    Check(g_gpu_timing.Read().completed==2&&g_gpu_timing.Read().invalid==1,"replayed query never becomes performance evidence");
    auto invalid=g_gpu_timing.Begin(device,list,13,{1,1,0,640,360});work();g_gpu_timing.BeforeSr(invalid,list);g_gpu_timing.End(invalid,list,false);submit(13);reset(13);
    Check(g_gpu_timing.Read().invalid==2,"partial phase recording initializes queries but remains invalid");
    unsigned accepted=0;for(unsigned i=0;i<100;++i)accepted+=g_gpu_timing.Begin(device,list,14,{1,1,0,640,360})!=0;
    Check(accepted==96&&g_gpu_timing.Read().dropped==4,"bounded capacity drops measurements without waiting");
    HR(ID3D12GraphicsCommandList_Close(list),"discard close");HR(ID3D12CommandAllocator_Reset(allocator),"discard allocator reset");HR(ID3D12GraphicsCommandList_Reset(list,allocator,nullptr),"discard native reset");g_gpu_timing.OnReset(14,true);
    Check(g_gpu_timing.Read().pending==0,"proven unsubmitted query allocations reclaimed");
    const auto poisoned=g_gpu_timing.Begin(device,list,15,{1,1,0,640,360});g_gpu_timing.Quarantine(15);
    // Returned backend failures must not append even diagnostic commands. The
    // deliberately inaccessible pointer makes an accidental COM call fail this
    // isolated process, rather than merely inspecting a bookkeeping flag.
    auto *must_not_touch=reinterpret_cast<ID3D12GraphicsCommandList *>(1);
    g_gpu_timing.Mark(poisoned,must_not_touch,nr_gpu_timing::Boundary::FeatureEnd);
    g_gpu_timing.BeforeSr(poisoned,must_not_touch);g_gpu_timing.End(poisoned,must_not_touch,false);
    Check(g_gpu_timing.Read().pending==1,"quarantined recording remains pinned and emits no further commands");
    HR(ID3D12Device_GetDeviceRemovedReason(device),"WARP device remains healthy");
    stats=g_gpu_timing.Read();std::printf("PASS WARP GPU query lifecycle: completed=%llu invalid=%llu dropped=%llu; no NVIDIA model or game claim\n",static_cast<unsigned long long>(stats.completed),static_cast<unsigned long long>(stats.invalid),static_cast<unsigned long long>(stats.dropped));
    HR(ID3D12GraphicsCommandList_Close(list),"final close");CloseHandle(event);ReleaseCom(source);ReleaseCom(destination);ReleaseCom(list);ReleaseCom(allocator);ReleaseCom(fence);ReleaseCom(queue);ReleaseCom(device);ReleaseCom(adapter);ReleaseCom(factory);
}
