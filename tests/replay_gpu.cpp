#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define CINTERFACE
#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>
template<class T>void ReleaseCom(T *&p){if(p)p->lpVtbl->Release(p);p=nullptr;}
#define NR_F8_GPU_LIFECYCLE_ONLY 1
#include "../src/nr_f8_gpu.inl"
#undef NR_F8_GPU_LIFECYCLE_ONLY
#include "../src/nr_replay_gpu.inl"
unsigned checks=0;
const char *export_fixture=nullptr;
void Check(bool ok,const char *why){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
void CheckHr(HRESULT value,const char *why){if(FAILED(value))std::fprintf(stderr,"HRESULT 0x%08x\n",static_cast<unsigned>(value));Check(SUCCEEDED(value),why);}
struct Gpu {
    ID3D12Device *device=nullptr;ID3D12CommandQueue *queue=nullptr;
    ID3D12CommandAllocator *allocator=nullptr;ID3D12GraphicsCommandList *list=nullptr;
    ID3D12Fence *fence=nullptr;HANDLE event=nullptr;std::uint64_t next=1;
    Gpu() {
        IDXGIFactory4 *factory=nullptr;IDXGIAdapter *adapter=nullptr;
        CheckHr(CreateDXGIFactory1(IID_IDXGIFactory4,reinterpret_cast<void **>(&factory)),"factory");
        CheckHr(IDXGIFactory4_EnumWarpAdapter(factory,IID_IDXGIAdapter,reinterpret_cast<void **>(&adapter)),"WARP adapter");
        CheckHr(D3D12CreateDevice(reinterpret_cast<IUnknown *>(adapter),D3D_FEATURE_LEVEL_11_0,IID_ID3D12Device,reinterpret_cast<void **>(&device)),"device");
        ReleaseCom(adapter);ReleaseCom(factory);
        D3D12_COMMAND_QUEUE_DESC q{};CheckHr(ID3D12Device_CreateCommandQueue(device,&q,IID_ID3D12CommandQueue,reinterpret_cast<void **>(&queue)),"queue");
        CheckHr(ID3D12Device_CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,reinterpret_cast<void **>(&allocator)),"allocator");
        CheckHr(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,IID_ID3D12GraphicsCommandList,reinterpret_cast<void **>(&list)),"list");
        CheckHr(ID3D12Device_CreateFence(device,0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,reinterpret_cast<void **>(&fence)),"fence");
        event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Check(event!=nullptr,"event");
    }
    std::uint64_t Execute() {
        CheckHr(ID3D12GraphicsCommandList_Close(list),"close");
        auto *base=reinterpret_cast<ID3D12CommandList *>(list);
        ID3D12CommandQueue_ExecuteCommandLists(queue,1,&base);
        const auto value=next++;CheckHr(ID3D12CommandQueue_Signal(queue,fence,value),"signal");
        CheckHr(ID3D12Fence_SetEventOnCompletion(fence,value,event),"event target");
        Check(WaitForSingleObject(event,30000)==WAIT_OBJECT_0,"bounded fixture wait");
        CheckHr(ID3D12CommandAllocator_Reset(allocator),"allocator reset");
        CheckHr(ID3D12GraphicsCommandList_Reset(list,allocator,nullptr),"list reset");return value;
    }
    ID3D12Resource *Texture(DXGI_FORMAT format,unsigned width,unsigned height,unsigned bytes_per_pixel,
                           const std::vector<unsigned char> &data) {
        ID3D12Resource *texture=nullptr,*upload=nullptr;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width=width;desc.Height=height;desc.DepthOrArraySize=1;desc.MipLevels=1;
        desc.SampleDesc.Count=1;desc.Format=format;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        CheckHr(ID3D12Device_CreateCommittedResource(device,&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,
            IID_ID3D12Resource,reinterpret_cast<void **>(&texture)),"texture");
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 total=0;
        ID3D12Device_GetCopyableFootprints(device,&desc,0,1,0,&footprint,nullptr,nullptr,&total);
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width=total;buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;
        buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;heap.Type=D3D12_HEAP_TYPE_UPLOAD;
        CheckHr(ID3D12Device_CreateCommittedResource(device,&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,
            IID_ID3D12Resource,reinterpret_cast<void **>(&upload)),"upload");
        void *mapped=nullptr;const D3D12_RANGE none{0,0};
        CheckHr(ID3D12Resource_Map(upload,0,&none,&mapped),"upload map");
        std::memset(mapped,0,static_cast<std::size_t>(total));
        for(unsigned y=0;y<height;++y)std::memcpy(static_cast<unsigned char *>(mapped)+footprint.Offset+y*footprint.Footprint.RowPitch,
            data.data()+y*width*bytes_per_pixel,width*bytes_per_pixel);
        ID3D12Resource_Unmap(upload,0,nullptr);
        D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=texture;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource=upload;src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=footprint;
        ID3D12GraphicsCommandList_CopyTextureRegion(list,&dst,0,0,0,&src,nullptr);
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={texture,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
        ID3D12GraphicsCommandList_ResourceBarrier(list,1,&barrier);Execute();ReleaseCom(upload);return texture;
    }
    ~Gpu(){ReleaseCom(list);ReleaseCom(allocator);ReleaseCom(queue);ReleaseCom(fence);ReleaseCom(device);if(event)CloseHandle(event);}
};

void FormatCases() {
    using namespace nr_replay;
    std::uint64_t bytes=0;
    Check(!PlaneBytes(0,10,4,Encoding::Float32,bytes),"empty image rejected");
    Check(!PlaneBytes(0xffffffff,0xffffffff,4,Encoding::Float32,bytes),"overflow rejected");
    Check(!PlaneBytes(10,10,3,Encoding::Float32,bytes),"unsupported packing rejected");
    Header header{};header.sequence=4;header.mode=2;header.source_width=5;header.source_height=3;
    header.work_width=5;header.work_height=3;
    for(unsigned i=0;i<kPlanes;++i)if(i!=5)
        Check(AppendPlane(header,static_cast<Role>(i),5,3,i==3?1:4,2,2,Encoding::Float32),"full plane layout");
    Check(ValidHeader(header),"coherent full header");
    auto bad=header;bad.planes[2].offset=0;Check(!ValidHeader(bad),"overlapping planes rejected");
    bad=header;bad.payload_bytes--;Check(!ValidHeader(bad),"truncated payload rejected");
    bad=header;bad.planes[3].bytes=0;Check(!ValidHeader(bad),"missing depth rejected");
    bad=header;bad.planes[0].encoding=7;Check(!ValidHeader(bad),"unknown encoding rejected");
    Sequence sequence;
    for(unsigned i=0;i<kFrames;++i) {
        header.index=i;header.instance=9;header.evaluation=i+1;header.since_reset=i+1;header.reset=i?0:1;
        header.completion=static_cast<unsigned>(Completion::Complete);
        Check(sequence.Accept(header),"contiguous cold sequence");
    }
    Check(sequence.Complete(),"complete only after all frames");
    Sequence wrong;header.index=0;header.evaluation=100;header.since_reset=100;header.reset=0;
    Check(!wrong.Accept(header),"warm unexplained initial history rejected");
}
void GpuCases() {
    using namespace nr_replay;
    Gpu gpu;Recorder recorder;Check(recorder.Ensure(gpu.device),"actual capture pipeline");
    constexpr unsigned width=7,height=5;
    Input inputs[kPlanes]{};std::vector<unsigned char> expected[kPlanes];
    for(unsigned i=0;i<kPlanes;++i) {
        if(i==5)continue;
        const bool half=i==1||i==2||i==4;
        const unsigned channels=i==3?1:(i==4?2:4);
        const auto format=half?(channels==2?DXGI_FORMAT_R16G16_FLOAT:DXGI_FORMAT_R16G16B16A16_FLOAT):
            (channels==1?DXGI_FORMAT_R32_FLOAT:DXGI_FORMAT_R32G32B32A32_FLOAT);
        auto &data=expected[i];data.resize(width*height*channels*(half?2:4));
        for(unsigned c=0;c<width*height*channels;++c) {
            if(half) {
                constexpr std::uint16_t values[]={0x0000,0x3800,0x3c00,0x4000,0xbc00,0x0400};
                const auto value=values[(c+i)%6];std::memcpy(data.data()+c*2,&value,2);
            } else {const float value=static_cast<float>(int(c%11)-3)*.125f+float(i);std::memcpy(data.data()+c*4,&value,4);}
        }
        inputs[i]={gpu.Texture(format,width,height,channels*(half?2:4),data),format,channels,half?Encoding::Float16:Encoding::Float32};
    }
    Header header{};header.sequence=100;header.instance=1;header.mode=2;
    header.source_width=width;header.source_height=height;header.work_width=width;header.work_height=height;
    header.reset=1;
    const auto armed=recorder.Arm(header,inputs);Check(armed==0,"full GPU frame arm");
    Check(recorder.Begin(gpu.list,armed,44),"pre-model planes dispatched");
    header.evaluation=1;header.since_reset=1;header.frame=42;
    Check(recorder.Finish(gpu.list,armed,header),"post-model planes and readback dispatched");
    recorder.Submit(44,gpu.fence,gpu.next);
    unsigned delivered=0;
    auto ready=[](void *key,std::uint64_t value){return ID3D12Fence_GetCompletedValue(static_cast<ID3D12Fence *>(key))>=value;};
    auto handoff=[&](ExportJob *job) {
        ++delivered;Check(job->header.completion==static_cast<unsigned>(Completion::Complete),"valid exact-submission result");
        Check(job->header.frame==42 && job->header.evaluation==1 && ValidHeader(job->header),"metadata corresponds to actual finish");
        void *mapped=nullptr;const D3D12_RANGE read{0,static_cast<SIZE_T>(job->bytes)};
        CheckHr(ID3D12Resource_Map(job->readback,0,&read,&mapped),"actual readback");
        const auto *data=static_cast<const unsigned char *>(mapped);
        for(unsigned i=0;i<kPlanes;++i) {
            const auto &plane=job->header.planes[i];if(!plane.bytes)continue;
            const unsigned pixel_bytes=plane.channels*(plane.encoding==2?2:4);
            for(unsigned y=0;y<plane.height;++y)for(unsigned x=0;x<plane.width;++x)for(unsigned b=0;b<pixel_bytes;++b)
                Check(data[plane.offset+(y*plane.width+x)*pixel_bytes+b]==expected[i][((y+plane.y)*width+x+plane.x)*pixel_bytes+b],
                    "all complete typed pixels match independent upload bytes");
            const auto end=plane.offset+plane.bytes;const auto aligned=std::min<std::uint64_t>((end+15)&~std::uint64_t(15),job->bytes);
            for(auto b=end;b<aligned;++b)Check(data[b]==0,"alignment padding is initialized, not leaked memory");
        }
        if(export_fixture && !job->header.index) {
            auto *file=std::fopen(export_fixture,"wb");Check(file!=nullptr,"fixture output");
            std::snprintf(job->header.build,sizeof(job->header.build),"WARP-fixture-no-NR");
            Check(std::fwrite(&job->header,1,sizeof(job->header),file)==sizeof(job->header),"export C++ header for independent reader");
            Check(std::fwrite(mapped,1,static_cast<std::size_t>(job->bytes),file)==job->bytes,"export actual WARP payload");
            std::fclose(file);
        }
        const D3D12_RANGE none{0,0};ID3D12Resource_Unmap(job->readback,0,&none);
        ReleaseCom(job->readback);resident_bytes.fetch_sub(job->bytes);delete job;
    };
    recorder.Collect(ready,handoff);Check(delivered==0,"not exported before completion");
    gpu.Execute();recorder.Collect(ready,handoff);Check(delivered==0,"completed fence does not override live command-list ownership");
    recorder.Invalidate(44,false);recorder.Collect(ready,handoff);
    Check(delivered==1 && recorder.Empty() && resident_bytes.load()==0,"readbacks drained after exact ownership release");
    // Nonzero source/target origins must not read unwritten allocation fringes.
    Header crop=header;crop.index=1;crop.source_width=4;crop.source_height=3;crop.source_x=1;crop.source_y=1;
    inputs[0].width=inputs[6].width=4;inputs[0].height=inputs[6].height=3;
    inputs[0].x=1;inputs[0].y=1;inputs[6].x=2;inputs[6].y=2;
    const auto cropped=recorder.Arm(crop,inputs);Check(cropped==1,"frame ordinal is independent of recycled ledger slot");
    Check(recorder.Begin(gpu.list,cropped,46),"subrect input dispatch");
    Check(recorder.Finish(gpu.list,cropped,header),"subrect result dispatch");
    recorder.Submit(46,gpu.fence,gpu.next);gpu.Execute();recorder.Invalidate(46,false);recorder.Collect(ready,handoff);
    Check(delivered==2 && recorder.Empty()&&!resident_bytes.load(),"subrect handoff is complete");
    inputs[0].width=inputs[6].width=inputs[0].height=inputs[6].height=0;
    inputs[0].x=inputs[0].y=inputs[6].x=inputs[6].y=0;
    // An unsubmitted list has no valid model frame and cannot leak its budget.
    const auto second=recorder.Arm(header,inputs);Check(second==0,"arm reuse after drain");
    Check(recorder.Begin(gpu.list,second,45),"second recording");
    CheckHr(ID3D12GraphicsCommandList_Close(gpu.list),"discard list close");
    CheckHr(ID3D12CommandAllocator_Reset(gpu.allocator),"discard allocator reset");
    CheckHr(ID3D12GraphicsCommandList_Reset(gpu.list,gpu.allocator,nullptr),"discard list reset");
    recorder.Invalidate(45,true);
    recorder.Collect(ready,[&](ExportJob *job){Check(job->header.completion==static_cast<unsigned>(Completion::Invalidated)&&!job->readback,"unsubmitted capture is explicitly invalid");delete job;});
    Check(recorder.Empty()&&!resident_bytes.load(),"discard drains all allocations");
    for(auto &input:inputs)ReleaseCom(input.resource);
    Check(recorder.ResetDevice(),"safe recorder shutdown");
}
int main(int argc,char **argv){if(argc==2)export_fixture=argv[1];FormatCases();GpuCases();std::printf("PASS replay format and actual WARP capture: %u checks\n",checks);}
