#define WIDL_C_INLINE_WRAPPERS
#define CINTERFACE
#define COBJMACROS
#include <d3dcompiler.h>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include "nr_pipeline_cache.h"
#include "ci_warp_runtime.h"
using namespace nr_pipeline_cache;
unsigned checks=0;
void Check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
void Hr(HRESULT hr,const char* text){if(FAILED(hr)){std::fprintf(stderr,"HRESULT 0x%08x\n",static_cast<unsigned>(hr));throw std::runtime_error(text);}++checks;}
template<class T>void Drop(T*& value){if(value){value->lpVtbl->Release(value);value=nullptr;}}
void FileTests(const std::wstring& root){
    Check(MaxFiles==128&&MaxBlob==16u*1024u*1024u&&MaxDirectoryBytes==128ull*1024ull*1024ull,"fixed bounded disk policy");
    const auto directory=root+L"\\files";Check(Directory(directory),"create file fixture");
    Digest key{};const char payload[]="checked private PSO file fixture";
    Check(DigestOf(payload,sizeof(payload),key),"SHA256 available");
    Check(Write(directory,key,payload,sizeof(payload)),"publish cache file");
    std::vector<unsigned char> data;const auto file=directory+L"\\"+Hex(key)+L".pso";
    Check(Read(file,key,data)&&data.size()==sizeof(payload)&&std::memcmp(data.data(),payload,sizeof(payload))==0,"cache round trip");
    auto wrong=key;wrong[0]^=1;Check(!Read(file,wrong,data)&&data.empty(),"wrong shader/root key must be ignored");
    {
        File corrupt(CreateFileW(file.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
        Check(bool(corrupt),"open private corrupt fixture");LARGE_INTEGER at{};at.QuadPart=sizeof(Header);Check(SetFilePointerEx(corrupt.h,at,nullptr,FILE_BEGIN),"seek fixture");
        const unsigned char byte=0xff;DWORD written=0;Check(WriteFile(corrupt.h,&byte,1,&written,nullptr)&&written==1,"corrupt fixture payload");
    }
    Check(!Read(file,key,data)&&data.empty(),"corruption must fail before the driver sees bytes");
    Check(Write(directory,key,payload,sizeof(payload)),"replace corrupted owned file atomically");
    {
        File lock(CreateFileW((directory+L"\\write.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
        Check(bool(lock),"hold writer lock");auto another=key;another[1]^=1;
        Check(!Write(directory,another,payload,sizeof(payload)),"another process writer must not block or overwrite");
        Check(Read(file,key,data),"writer lock must not disable a completed cache read");
    }
    Check(!OwnedName(L"notes.pso")&&!OwnedName(L"../anything.pso")&&OwnedName((Hex(key)+L".pso").c_str()),"strict owned filename scope");
    Check(!Write(directory,key,payload,MaxBlob+1ull),"oversized cache rejected before reading source pointer");
    const auto quota=root+L"\\quota";Check(Directory(quota),"quota fixture");
    for(unsigned i=0;i<MaxFiles;++i){Digest k{};std::memcpy(k.data(),&i,sizeof(i));Check(Write(quota,k,payload,sizeof(payload)),"within file quota");}
    Digest full{};full[10]=1;Check(!Write(quota,full,payload,sizeof(payload)),"bounded file count");
    const auto orphan=root+L"/orphan";Check(Directory(orphan),"orphan fixture");
    {
        File partial(CreateFileW((orphan+L"/interrupted.tmp").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
        Check(bool(partial),"create orphaned private temp");LARGE_INTEGER end{};end.QuadPart=MaxDirectoryBytes;
        Check(SetFilePointerEx(partial.h,end,nullptr,FILE_BEGIN)&&SetEndOfFile(partial.h),"bounded-size orphan fixture");
    }
    Check(!Write(orphan,key,payload,sizeof(payload)),"orphaned temp escaped total disk quota");
    Identity id{};id.driver=1;unsigned char shader[]={1,2,3};D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};desc.CS={shader,sizeof(shader)};
    Digest a{},b{};Check(PipelineKey(id,desc,a),"key generation");
    ++id.driver;Check(PipelineKey(id,desc,b)&&a!=b,"driver invalidation");--id.driver;
    ++id.device;Check(PipelineKey(id,desc,b)&&a!=b,"adapter invalidation");--id.device;
    ++id.root[0];Check(PipelineKey(id,desc,b)&&a!=b,"serialized root invalidation");--id.root[0];
    ++desc.NodeMask;Check(PipelineKey(id,desc,b)&&a!=b,"node invalidation");--desc.NodeMask;
    ++shader[0];Check(PipelineKey(id,desc,b)&&a!=b,"shader invalidation");
    Check(RetryWithoutCache(E_INVALIDARG)&&RetryWithoutCache(D3D12_ERROR_DRIVER_VERSION_MISMATCH)&&RetryWithoutCache(D3D12_ERROR_ADAPTER_NOT_FOUND),"incompatible cache retries original creation");
    Check(!RetryWithoutCache(E_OUTOFMEMORY)&&!RetryWithoutCache(DXGI_ERROR_DEVICE_REMOVED)&&!RetryWithoutCache(DXGI_ERROR_DEVICE_RESET),"device/OOM failure is not retried as a cache miss");
}
struct Gpu {
    ID3D12Device* device=nullptr;ID3D12CommandQueue* queue=nullptr;ID3D12CommandAllocator* allocator=nullptr;
    ID3D12GraphicsCommandList* list=nullptr;ID3D12Fence* fence=nullptr;HANDLE event=nullptr;unsigned long long serial=0;
    ID3D12Resource* output=nullptr;ID3D12Resource* readback=nullptr;
    void Initialize(bool hardware){
        Check(!hardware||!nr_test_warp::Configured(),"Pinned CI WARP forbids hardware selection");
        if(!hardware)Hr(nr_test_warp::Prepare(),"Prepare pinned CI WARP");
        IDXGIFactory4* factory=nullptr;IDXGIAdapter1* adapter=nullptr;
        Hr(CreateDXGIFactory1(IID_IDXGIFactory4,reinterpret_cast<void**>(&factory)),"factory");
        if(hardware){for(unsigned i=0;IDXGIFactory4_EnumAdapters1(factory,i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i){DXGI_ADAPTER_DESC1 d{};IDXGIAdapter1_GetDesc1(adapter,&d);if(d.VendorId==0x10de&&!(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))break;Drop(adapter);}}
        else Hr(IDXGIFactory4_EnumWarpAdapter(factory,IID_IDXGIAdapter1,reinterpret_cast<void**>(&adapter)),"WARP");
        Check(adapter!=nullptr,"requested adapter available");
        Hr(D3D12CreateDevice(reinterpret_cast<IUnknown*>(adapter),D3D_FEATURE_LEVEL_11_0,IID_ID3D12Device,reinterpret_cast<void**>(&device)),"device");Drop(adapter);Drop(factory);
        if(!hardware)Hr(nr_test_warp::Verify("pipeline-cache"),"Verify actual CI WARP");
        D3D12_COMMAND_QUEUE_DESC q{};Hr(ID3D12Device_CreateCommandQueue(device,&q,IID_ID3D12CommandQueue,reinterpret_cast<void**>(&queue)),"queue");
        Hr(ID3D12Device_CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,IID_ID3D12CommandAllocator,reinterpret_cast<void**>(&allocator)),"allocator");
        Hr(ID3D12Device_CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,IID_ID3D12GraphicsCommandList,reinterpret_cast<void**>(&list)),"list");
        Hr(ID3D12GraphicsCommandList_Close(list),"close initial list");
        Hr(ID3D12Device_CreateFence(device,0,D3D12_FENCE_FLAG_NONE,IID_ID3D12Fence,reinterpret_cast<void**>(&fence)),"fence");event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Check(event!=nullptr,"event");
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=256;buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;buffer.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        Hr(ID3D12Device_CreateCommittedResource(device,&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(&output)),"output buffer");
        heap.Type=D3D12_HEAP_TYPE_READBACK;buffer.Flags=D3D12_RESOURCE_FLAG_NONE;
        Hr(ID3D12Device_CreateCommittedResource(device,&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_ID3D12Resource,reinterpret_cast<void**>(&readback)),"readback");
    }
    void Dispatch(ID3D12RootSignature* root,ID3D12PipelineState* pipeline,unsigned expected){
        Hr(ID3D12CommandAllocator_Reset(allocator),"allocator reset after exact completion");Hr(ID3D12GraphicsCommandList_Reset(list,allocator,pipeline),"list reset");
        ID3D12GraphicsCommandList_SetComputeRootSignature(list,root);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(list,0,ID3D12Resource_GetGPUVirtualAddress(output));ID3D12GraphicsCommandList_Dispatch(list,1,1,1);
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=output;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
        ID3D12GraphicsCommandList_ResourceBarrier(list,1,&barrier);ID3D12GraphicsCommandList_CopyBufferRegion(list,readback,0,output,0,4);
        std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);ID3D12GraphicsCommandList_ResourceBarrier(list,1,&barrier);Hr(ID3D12GraphicsCommandList_Close(list),"close dispatch");
        ID3D12CommandList* lists[]={reinterpret_cast<ID3D12CommandList*>(list)};ID3D12CommandQueue_ExecuteCommandLists(queue,1,lists);Hr(ID3D12CommandQueue_Signal(queue,fence,++serial),"signal");
        Hr(ID3D12Fence_SetEventOnCompletion(fence,serial,event),"wait registration");Check(WaitForSingleObject(event,10000)==WAIT_OBJECT_0,"exact GPU completion");const auto completed=ID3D12Fence_GetCompletedValue(fence);Check(completed!=UINT64_MAX&&completed>=serial,"exact signaled fence, not device removed");
        void* data=nullptr;D3D12_RANGE range{0,4};Hr(ID3D12Resource_Map(readback,0,&range,&data),"readback map");const auto value=*static_cast<unsigned*>(data);D3D12_RANGE none{};ID3D12Resource_Unmap(readback,0,&none);Check(value==expected,"cached PSO shader output must equal current source");
    }
    void Destroy(){Drop(readback);Drop(output);Drop(list);Drop(allocator);Drop(fence);Drop(queue);Drop(device);if(event)CloseHandle(event);}
};
void PipelineTests(const std::wstring& directory,bool hardware){
    Gpu gpu;gpu.Initialize(hardware);
    D3D12_ROOT_PARAMETER parameter{};parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;parameter.Descriptor.ShaderRegister=0;parameter.ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=1;rd.pParameters=&parameter;
    ID3DBlob* serialized=nullptr;ID3D12RootSignature* root=nullptr;
    Hr(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,nullptr),"serialize root");
    Hr(ID3D12Device_CreateRootSignature(gpu.device,0,ID3D10Blob_GetBufferPointer(serialized),ID3D10Blob_GetBufferSize(serialized),IID_ID3D12RootSignature,reinterpret_cast<void**>(&root)),"create root");
    Session cache;cache.Initialize(gpu.device,ID3D10Blob_GetBufferPointer(serialized),ID3D10Blob_GetBufferSize(serialized),true,directory+L"\\gpu");
    unsigned hits=0;
    for(unsigned i=0;i<4;++i){
        const unsigned value=i<2?0x12345678u:0x87654321u;
        const auto source=std::string("RWByteAddressBuffer O:register(u0);[numthreads(1,1,1)]void Main(){O.Store(0,")+std::to_string(value)+"u);}";
        ID3DBlob* shader=nullptr;Hr(D3DCompile(source.data(),source.size(),nullptr,nullptr,nullptr,"Main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shader,nullptr),"compile deterministic fixture");
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};desc.pRootSignature=root;desc.CS={ID3D10Blob_GetBufferPointer(shader),ID3D10Blob_GetBufferSize(shader)};
        ID3D12PipelineState* pipeline=nullptr;const auto result=cache.Create(gpu.device,desc,&pipeline);Hr(result.hr,"pipeline cache/original create");Check(pipeline!=nullptr,"pipeline exists");
        if(hardware){Check(result.enabled,"hardware cache must actually be enabled");Check(result.hit==(i%2==1),"same shader hit / changed shader miss");}hits+=result.hit;
        gpu.Dispatch(root,pipeline,value);std::printf("GPU backend=%s case=%u cacheEnabled=%d hit=%d written=%d output=%08x\n",hardware?"NVIDIA":"WARP",i,result.enabled,result.hit,result.written,value);Drop(pipeline);Drop(shader);
    }
    std::printf("GPU_OUTPUTS=4 CACHE_HITS=%u\n",hits);Drop(root);Drop(serialized);gpu.Destroy();
}
int wmain(int argc,wchar_t** argv){try{Check(argc==3,"usage: cache-test directory hardware|warp");const std::wstring dir=argv[1];Check(Directory(dir),"private output directory");FileTests(dir);PipelineTests(dir,std::wstring(argv[2])==L"hardware");std::printf("PASS %u assertions: bounded files/hash/lock/key/fallback policy and actual GPU outputs\n",checks);return 0;}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s; unproven GPU resources retained until process exit\n",e.what());return 3;}}
