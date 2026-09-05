#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define CINTERFACE
#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static unsigned checks = 0, transitions = 0;
static void Check(bool value, const char *message) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void HR(HRESULT value, const char *message) {
    if (FAILED(value)) { std::fprintf(stderr,"HRESULT=%08lx\n",static_cast<unsigned long>(value)); Check(false,message); }
}
static void Transition(ID3D12GraphicsCommandList *list, ID3D12Resource *r,
                        D3D12_RESOURCE_STATES &state, D3D12_RESOURCE_STATES next) {
    if (state == next) return;
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = state; b.Transition.StateAfter = next;
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &b);
    state = next; ++transitions;
}
#include "nr_post_sr_snapshot.inl"

static ID3D12Device *device = nullptr;
static std::vector<ID3D12Resource *> owned;
static ID3D12Resource *Resource(D3D12_RESOURCE_DESC desc, D3D12_HEAP_TYPE type,
                               D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap = {}; heap.Type = type;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    ID3D12Resource *r = nullptr;
    HR(ID3D12Device_CreateCommittedResource(device,&heap,D3D12_HEAP_FLAG_NONE,&desc,state,
        nullptr,IID_ID3D12Resource,reinterpret_cast<void **>(&r)),"create resource");
    owned.push_back(r); return r;
}
static ID3D12Resource *Texture(unsigned w, unsigned h, bool deny,
                               DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT) {
    D3D12_RESOURCE_DESC d = {}; d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width=w; d.Height=h; d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;
    d.Format=format; d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (deny) d.Flags=static_cast<D3D12_RESOURCE_FLAGS>(d.Flags|D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
    return Resource(d,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
}
static ID3D12Resource *Buffer(UINT64 bytes, D3D12_HEAP_TYPE type) {
    D3D12_RESOURCE_DESC d = {}; d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width=bytes; d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;
    d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return Resource(d,type,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST);
}
struct Readback { ID3D12Resource *buffer; D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp; UINT64 size; };
static Readback Footprint(ID3D12Resource *texture,D3D12_HEAP_TYPE type) {
    auto d=ID3D12Resource_GetDesc(texture); Readback r={};
    ID3D12Device_GetCopyableFootprints(device,&d,0,1,0,&r.fp,nullptr,nullptr,&r.size);
    r.buffer=Buffer(r.size,type); return r;
}
static std::vector<std::uint16_t> Pixels(unsigned w,unsigned h,unsigned seed) {
    std::vector<std::uint16_t> out(w*h*4);
    const std::uint16_t patterns[]={0x0000,0x8000,0x0001,0x3c00,0x3555,0x7bff,0xbc00,0x0400};
    for(unsigned i=0;i<out.size();++i)out[i]=patterns[(i+seed+i/13)%8];
    return out;
}
static void Upload(ID3D12GraphicsCommandList *list,ID3D12Resource *texture,
                    const std::vector<std::uint16_t> &data) {
    auto r=Footprint(texture,D3D12_HEAP_TYPE_UPLOAD);
    void *p=nullptr; D3D12_RANGE empty={0,0};
    HR(ID3D12Resource_Map(r.buffer,0,&empty,&p),"upload map");
    std::memset(p,0,static_cast<size_t>(r.size));
    for(unsigned y=0;y<r.fp.Footprint.Height;++y)
        std::memcpy(static_cast<char *>(p)+r.fp.Offset+y*r.fp.Footprint.RowPitch,
                    data.data()+y*r.fp.Footprint.Width*4,r.fp.Footprint.Width*8);
    ID3D12Resource_Unmap(r.buffer,0,nullptr);
    D3D12_TEXTURE_COPY_LOCATION from={},to={}; from.pResource=r.buffer;
    from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=r.fp;
    to.pResource=texture;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    ID3D12GraphicsCommandList_CopyTextureRegion(list,&to,0,0,0,&from,nullptr);
}
static Readback Read(ID3D12GraphicsCommandList *list,ID3D12Resource *texture,
                     D3D12_RESOURCE_STATES &state) {
    auto r=Footprint(texture,D3D12_HEAP_TYPE_READBACK);
    Transition(list,texture,state,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION from={},to={};from.pResource=texture;
    from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource=r.buffer;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=r.fp;
    ID3D12GraphicsCommandList_CopyTextureRegion(list,&to,0,0,0,&from,nullptr);
    return r;
}
static void Verify(const Readback &r,const std::vector<std::uint16_t> &expected) {
    void *p=nullptr; D3D12_RANGE range={0,static_cast<SIZE_T>(r.size)};
    HR(ID3D12Resource_Map(r.buffer,0,&range,&p),"readback map after fence");
    for(unsigned y=0;y<r.fp.Footprint.Height;++y) {
        const auto *row=reinterpret_cast<const std::uint16_t *>(static_cast<const char *>(p)+r.fp.Offset+y*r.fp.Footprint.RowPitch);
        for(unsigned x=0;x<r.fp.Footprint.Width*4;++x)
            Check(row[x]==expected[y*r.fp.Footprint.Width*4+x],"native/snapshot FP16 bytes incl alpha/padding");
    }
    D3D12_RANGE empty={0,0};ID3D12Resource_Unmap(r.buffer,0,&empty);
}
int main(int argc,char **argv) {
    const bool negative=argc>1 && std::strcmp(argv[1],"--negative-control")==0;
    ID3D12Debug *debug=nullptr;
    HR(D3D12GetDebugInterface(IID_ID3D12Debug,reinterpret_cast<void **>(&debug)),"required D3D12 debug layer");
    ID3D12Debug_EnableDebugLayer(debug); ID3D12Debug_Release(debug);
    IDXGIFactory4 *factory=nullptr; IDXGIAdapter *adapter=nullptr;
    HR(CreateDXGIFactory1(IID_IDXGIFactory4,reinterpret_cast<void **>(&factory)),"DXGI factory");
    HR(IDXGIFactory4_EnumWarpAdapter(factory,IID_IDXGIAdapter,reinterpret_cast<void **>(&adapter)),"WARP only");
    HR(D3D12CreateDevice(reinterpret_cast<IUnknown *>(adapter),D3D_FEATURE_LEVEL_11_0,
        IID_ID3D12Device,reinterpret_cast<void **>(&device)),"WARP device");
    ID3D12InfoQueue *info=nullptr;
    HR(ID3D12Device_QueryInterface(device,IID_ID3D12InfoQueue,reinterpret_cast<void **>(&info)),"debug info queue");
    ID3D12CommandQueue *queue=nullptr;D3D12_COMMAND_QUEUE_DESC q={};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    HR(ID3D12Device_CreateCommandQueue(device,&q,IID_ID3D12CommandQueue,reinterpret_cast<void **>(&queue)),"queue");
    ID3D12CommandAllocator *allocator=nullptr;
    HR(ID3D12Device_CreateCommandAllocator(device,q.Type,IID_ID3D12CommandAllocator,reinterpret_cast<void **>(&allocator)),"allocator");
    ID3D12GraphicsCommandList *list=nullptr;
    HR(ID3D12Device_CreateCommandList(device,0,q.Type,allocator,nullptr,IID_ID3D12GraphicsCommandList,reinterpret_cast<void **>(&list)),"list");
    std::vector<Readback> reads;std::vector<std::vector<std::uint16_t>> expected;
    for(unsigned trial=0;trial<3;++trial) {
        const unsigned sw=trial==2?9:8,sh=6,dw=trial==2?9:5,dh=trial==2?6:4;
        const unsigned x=trial==2?0:2,y=trial==2?0:1,w=trial==2?9:3,h=trial==2?6:2;
        auto *native=Texture(sw,sh,trial!=0),*snapshot=Texture(dw,dh,false);
        auto original=Pixels(sw,sh,trial),dest=Pixels(dw,dh,trial+3);
        Upload(list,native,original);Upload(list,snapshot,dest);
        D3D12_RESOURCE_STATES ns=D3D12_RESOURCE_STATE_COPY_DEST,ss=D3D12_RESOURCE_STATE_COPY_DEST;
        Transition(list,native,ns,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        const unsigned before=transitions;
        Check(!CopyPostSrFp16Snapshot(nullptr,native,ns,snapshot,ss,x,y,w,h),"null list rejected");
        Check(!CopyPostSrFp16Snapshot(list,native,ns,native,ns,0,0,1,1),"same resource rejected");
        Check(!CopyPostSrFp16Snapshot(list,native,ns,snapshot,ss,UINT_MAX,0,2,1),"overflow rejected");
        Check(!CopyPostSrFp16Snapshot(list,native,ns,snapshot,ss,sw,0,1,1),"source OOB rejected");
        Check(!CopyPostSrFp16Snapshot(list,native,ns,snapshot,ss,0,0,dw+1,dh),"destination OOB rejected");
        Check(!CopyPostSrFp16Snapshot(list,native,ns,snapshot,ss,0,0,0,1),"zero extent rejected");
        Check(transitions==before,"rejected snapshots record no barriers");
        Check(CopyPostSrFp16Snapshot(list,native,ns,snapshot,ss,x,y,w,h),"valid exact snapshot");
        Check(ns==D3D12_RESOURCE_STATE_UNORDERED_ACCESS && ss==D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,"states restored");
        // Repeat on the same workset, including a second origin, without rewriting descriptors.
        const unsigned rx=trial==2?x:x+1;
        Check(CopyPostSrFp16Snapshot(list,native,ns,snapshot,ss,rx,y,w,h),"repeated workset snapshot");
        for(unsigned row=0;row<h;++row)for(unsigned col=0;col<w*4;++col)
            dest[row*dw*4+col]=original[(row+y)*sw*4+rx*4+col];
        if(negative && trial==0)dest[0]^=1; // GPU oracle must reject an incorrect expected byte.
        reads.push_back(Read(list,native,ns));expected.push_back(original);
        reads.push_back(Read(list,snapshot,ss));expected.push_back(dest);
    }
    for(unsigned warmup: {0u,1u,3u})for(unsigned frames: {0u,1u,2u,4u}) {
        Check(KeepNativePostSrImage(false,true,frames,warmup),"reset always retains native even warmup zero");
        Check(KeepNativePostSrImage(true,false,frames,warmup),"identity retains native");
        Check(KeepNativePostSrImage(false,false,frames,warmup)==(frames<=warmup),"normal warmup policy");
    }
    HR(ID3D12GraphicsCommandList_Close(list),"close");
    ID3D12CommandList *submitted[]={reinterpret_cast<ID3D12CommandList *>(list)};
    ID3D12CommandQueue_ExecuteCommandLists(queue,1,submitted);
    ID3D12Fence *fence=nullptr;HR(ID3D12Device_CreateFence(device,0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,reinterpret_cast<void **>(&fence)),"fence");
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Check(event!=nullptr,"event");
    HR(ID3D12CommandQueue_Signal(queue,fence,1),"signal");
    HR(ID3D12Fence_SetEventOnCompletion(fence,1,event),"event on completion");
    Check(WaitForSingleObject(event,15000)==WAIT_OBJECT_0,"bounded test fence wait");
    for(size_t i=0;i<reads.size();++i)Verify(reads[i],expected[i]);
    for(UINT64 i=0;i<ID3D12InfoQueue_GetNumStoredMessages(info);++i) {
        SIZE_T n=0;HR(ID3D12InfoQueue_GetMessage(info,i,nullptr,&n),"debug message size");
        std::vector<unsigned char> bytes(n);auto *m=reinterpret_cast<D3D12_MESSAGE *>(bytes.data());
        HR(ID3D12InfoQueue_GetMessage(info,i,m,&n),"debug message");
        if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR) { std::fprintf(stderr,"%s\n",m->pDescription);Check(false,"D3D12 validation error"); }
    }
    for(auto *r:owned)ID3D12Resource_Release(r);
    CloseHandle(event);ID3D12Fence_Release(fence);ID3D12GraphicsCommandList_Release(list);
    ID3D12CommandAllocator_Release(allocator);ID3D12CommandQueue_Release(queue);ID3D12InfoQueue_Release(info);
    ID3D12Device_Release(device);IDXGIAdapter_Release(adapter);IDXGIFactory4_Release(factory);
    std::printf("PASS post-SR production snapshot: %u checks, 3 WARP scenarios, no hardware fallback\n",checks);
}
