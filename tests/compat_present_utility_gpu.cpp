// Actual production Present conversion and RT blit on WARP (or --hardware).
// The edit is an explicit deterministic shader substitute, never an NGX test.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "nr_compat_present_shader.h"
static void Check(HRESULT hr,const char* action){if(FAILED(hr)){char s[160];std::snprintf(s,sizeof(s),"%s: 0x%08lx",action,static_cast<unsigned long>(hr));throw std::runtime_error(s);}}
struct Objects {
    std::vector<IUnknown*> values;
    template<class T>T* Keep(T* p){values.push_back(p);return p;}
    ~Objects(){for(auto i=values.rbegin();i!=values.rend();++i)(*i)->Release();}
};
struct Host {
    Objects objects;ID3D12Device* device{};ID3D12CommandQueue* queue{};
    ID3D12CommandAllocator* allocator{};ID3D12GraphicsCommandList* list{};ID3D12Fence* fence{};
    HANDLE event{};UINT64 serial=0;
    explicit Host(bool hardware){
        IDXGIFactory4* factory=nullptr;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"factory");objects.Keep(factory);
        IDXGIAdapter* adapter=nullptr;if(!hardware){Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)),"WARP");objects.Keep(adapter);}
        Check(D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"device");objects.Keep(device);
        D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"queue");objects.Keep(queue);
        Check(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&allocator)),"allocator");objects.Keep(allocator);
        Check(device->CreateCommandList(0,q.Type,allocator,nullptr,IID_PPV_ARGS(&list)),"list");objects.Keep(list);Check(list->Close(),"initial close");
        Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");objects.Keep(fence);
        event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("event");
    }
    ~Host(){if(event)CloseHandle(event);}
    void Begin(){Check(allocator->Reset(),"reset allocator");Check(list->Reset(allocator,nullptr),"reset list");}
    void Wait(){Check(list->Close(),"close");ID3D12CommandList* lists[]={list};queue->ExecuteCommandLists(1,lists);Check(queue->Signal(fence,++serial),"signal");Check(fence->SetEventOnCompletion(serial,event),"completion event");if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0)throw std::runtime_error("GPU completion timeout");}
    ID3D12Resource* Resource(Objects& owner,const D3D12_RESOURCE_DESC& desc,D3D12_HEAP_TYPE heap,D3D12_RESOURCE_STATES state){
        D3D12_HEAP_PROPERTIES h{};h.Type=heap;h.CreationNodeMask=h.VisibleNodeMask=1;ID3D12Resource* r=nullptr;
        Check(device->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&r)),"resource");return owner.Keep(r);
    }
    ID3D12Resource* Buffer(Objects& o,UINT64 bytes,D3D12_HEAP_TYPE heap,D3D12_RESOURCE_STATES state){D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;return Resource(o,d,heap,state);}
    void Barrier(ID3D12Resource* r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&b);}
};
static ID3DBlob* Compile(Objects& o,const std::string& text,const char* entry,const char* target){
    ID3DBlob *b=nullptr,*errors=nullptr;const auto hr=D3DCompile(text.data(),text.size(),"production-present",nullptr,nullptr,entry,target,D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&b,&errors);
    if(errors){if(FAILED(hr))std::fwrite(errors->GetBufferPointer(),1,errors->GetBufferSize(),stderr);errors->Release();}Check(hr,entry);return o.Keep(b);
}
static void Case(Host& h,DXGI_FORMAT format,unsigned encoding,bool edit){
    Objects o;constexpr unsigned w=7,height=5;const unsigned bytes_per_pixel=format==DXGI_FORMAT_R16G16B16A16_FLOAT?8:4;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=height;d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=format;
    auto* raw=h.Resource(o,d,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    d.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;auto* output=h.Resource(o,d,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RENDER_TARGET);
    d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    auto* canonical=h.Resource(o,d,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto* composed=h.Resource(o,d,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    const auto raw_desc=raw->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows=0;UINT64 row_bytes=0,total=0;
    h.device->GetCopyableFootprints(&raw_desc,0,1,0,&footprint,&rows,&row_bytes,&total);
    auto* upload=h.Buffer(o,total,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    auto* readback=h.Buffer(o,total,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    std::vector<std::uint8_t> expected(height*w*bytes_per_pixel);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<w;++x){auto* p=expected.data()+(y*w+x)*bytes_per_pixel;
        if(bytes_per_pixel==8){const std::uint16_t values[]={std::uint16_t((x&1)?0xb400:0x3e00),std::uint16_t(0x3800+y*64),0x4000,std::uint16_t(0x3000+x*64)};std::memcpy(p,values,8);}
        else if(format==DXGI_FORMAT_R10G10B10A2_UNORM){const std::uint32_t v=(100+x*73)|((300+y*40)<<10)|(800u<<20)|(((x+y)&3u)<<30);std::memcpy(p,&v,4);}
        else {p[0]=std::uint8_t(30+x*25);p[1]=std::uint8_t(50+y*30);p[2]=200;p[3]=std::uint8_t(10+x*30);}
    }
    void* mapped=nullptr;D3D12_RANGE none{0,0};Check(upload->Map(0,&none,&mapped),"upload map");
    for(unsigned y=0;y<height;++y)std::memcpy(static_cast<char*>(mapped)+y*footprint.Footprint.RowPitch,expected.data()+y*w*bytes_per_pixel,w*bytes_per_pixel);upload->Unmap(0,nullptr);
    D3D12_DESCRIPTOR_RANGE ranges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,3,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,3,0,0,3}};
    D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[0].DescriptorTable={2,ranges};params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[1].Constants={0,0,4};
    D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=2;rd.pParameters=params;ID3DBlob *serialized=nullptr,*error=nullptr;
    Check(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&error),"serialize root");o.Keep(serialized);if(error)o.Keep(error);
    ID3D12RootSignature* root=nullptr;Check(h.device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&root)),"root");o.Keep(root);
    const std::string text=std::string(kCompatPresentShader)+"\n[numthreads(8,8,1)] void TestEditCS(uint3 p:SV_DispatchThreadID){if(p.x<Width&&p.y<Height){float4 c=Canonical.Load(int3(p.xy,0));Destination[p.xy]=float4(c.rgb*0.5,c.a);}}";
    auto* cs=Compile(o,text,"ConvertCS","cs_5_0");auto* edit_cs=Compile(o,text,"TestEditCS","cs_5_0");auto* vs=Compile(o,text,"FullscreenVS","vs_5_0");auto* ps=Compile(o,text,"BlitPS","ps_5_0");
    ID3D12PipelineState *convert=nullptr,*edit_pso=nullptr,*blit=nullptr;D3D12_COMPUTE_PIPELINE_STATE_DESC compute{};compute.pRootSignature=root;compute.CS={cs->GetBufferPointer(),cs->GetBufferSize()};Check(h.device->CreateComputePipelineState(&compute,IID_PPV_ARGS(&convert)),"convert PSO");o.Keep(convert);compute.CS={edit_cs->GetBufferPointer(),edit_cs->GetBufferSize()};Check(h.device->CreateComputePipelineState(&compute,IID_PPV_ARGS(&edit_pso)),"edit substitute PSO");o.Keep(edit_pso);
    D3D12_GRAPHICS_PIPELINE_STATE_DESC gp{};gp.pRootSignature=root;gp.VS={vs->GetBufferPointer(),vs->GetBufferSize()};gp.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    gp.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;gp.SampleMask=UINT_MAX;gp.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;gp.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;gp.RasterizerState.DepthClipEnable=TRUE;gp.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;gp.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;gp.NumRenderTargets=1;gp.RTVFormats[0]=format;gp.SampleDesc.Count=1;
    Check(h.device->CreateGraphicsPipelineState(&gp,IID_PPV_ARGS(&blit)),"blit PSO");o.Keep(blit);
    ID3D12DescriptorHeap *heap=nullptr,*edited_heap=nullptr,*rtv=nullptr;D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=6;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    Check(h.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"heap");o.Keep(heap);Check(h.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&edited_heap)),"edited heap");o.Keep(edited_heap);
    const auto step=h.device->GetDescriptorHandleIncrementSize(hd.Type);
    for(unsigned pass=0;pass<2;++pass){auto cpu=(pass?edited_heap:heap)->GetCPUDescriptorHandleForHeapStart();ID3D12Resource* reads[]={raw,canonical,composed};
        for(unsigned i=0;i<3;++i){D3D12_SHADER_RESOURCE_VIEW_DESC s{};s.Format=i?DXGI_FORMAT_R16G16B16A16_FLOAT:format;s.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;s.Texture2D.MipLevels=1;s.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;h.device->CreateShaderResourceView(reads[i],&s,cpu);cpu.ptr+=step;}
        for(unsigned i=0;i<3;++i){D3D12_UNORDERED_ACCESS_VIEW_DESC u{};u.Format=i==0?DXGI_FORMAT_R16G16B16A16_FLOAT:i==1?DXGI_FORMAT_R32_FLOAT:DXGI_FORMAT_R16G16_FLOAT;u.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;h.device->CreateUnorderedAccessView(i?nullptr:pass?composed:canonical,nullptr,&u,cpu);cpu.ptr+=step;}
    }
    hd={};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;Check(h.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv)),"RTV");o.Keep(rtv);const auto target=rtv->GetCPUDescriptorHandleForHeapStart();h.device->CreateRenderTargetView(output,nullptr,target);
    h.Begin();D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=upload;from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=footprint;to.pResource=raw;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;h.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    const auto read_state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;h.Barrier(raw,D3D12_RESOURCE_STATE_COPY_DEST,read_state);
    const unsigned constants[]={w,height,encoding,0};h.list->SetDescriptorHeaps(1,&heap);h.list->SetComputeRootSignature(root);h.list->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());h.list->SetComputeRoot32BitConstants(1,4,constants,0);h.list->SetPipelineState(convert);h.list->Dispatch(1,1,1);
    h.Barrier(canonical,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);h.list->CopyResource(composed,canonical);h.Barrier(canonical,D3D12_RESOURCE_STATE_COPY_SOURCE,read_state);
    if(edit){h.Barrier(composed,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);h.list->SetDescriptorHeaps(1,&edited_heap);h.list->SetComputeRootDescriptorTable(0,edited_heap->GetGPUDescriptorHandleForHeapStart());h.list->SetPipelineState(edit_pso);h.list->Dispatch(1,1,1);h.Barrier(composed,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);}
    else h.Barrier(composed,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    h.list->SetDescriptorHeaps(1,&heap);h.list->SetGraphicsRootSignature(root);h.list->SetGraphicsRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());h.list->SetGraphicsRoot32BitConstants(1,4,constants,0);h.list->SetPipelineState(blit);h.list->OMSetRenderTargets(1,&target,FALSE,nullptr);
    D3D12_VIEWPORT viewport{0,0,float(w),float(height),0,1};D3D12_RECT rect{0,0,LONG(w),LONG(height)};h.list->RSSetViewports(1,&viewport);h.list->RSSetScissorRects(1,&rect);h.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);h.list->DrawInstanced(3,1,0,0);
    h.Barrier(output,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);from={};from.pResource=output;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to={};to.pResource=readback;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;h.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);h.Wait();
    D3D12_RANGE range{0,SIZE_T(total)};Check(readback->Map(0,&range,&mapped),"readback map");unsigned changed=0;
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<w;++x){const auto* actual=static_cast<const std::uint8_t*>(mapped)+y*footprint.Footprint.RowPitch+x*bytes_per_pixel;const auto* wanted=expected.data()+(y*w+x)*bytes_per_pixel;
        if(!edit&&std::memcmp(actual,wanted,bytes_per_pixel))throw std::runtime_error("identity RT blit changed original encoded pixel/alpha");
        if(edit){changed+=std::memcmp(actual,wanted,bytes_per_pixel)!=0;
            const bool alpha=bytes_per_pixel==8?std::memcmp(actual+6,wanted+6,2)==0:format==DXGI_FORMAT_R10G10B10A2_UNORM?(actual[3]&0xc0)==(wanted[3]&0xc0):actual[3]==wanted[3];if(!alpha)throw std::runtime_error("edited RT blit changed source alpha");}
    }
    readback->Unmap(0,&none);if(edit&&changed!=w*height)throw std::runtime_error("edited output was not consumed by the RT blit");
    std::printf("format=%u encoding=%u %s: %u pixels passed\n",unsigned(format),encoding,edit?"edited + alpha":"exact identity",w*height);
}
int main(int argc,char** argv){try{const bool hardware=argc>1&&std::strcmp(argv[1],"--hardware")==0;Host host(hardware);for(bool edit:{false,true}){Case(host,DXGI_FORMAT_R8G8B8A8_UNORM,1,edit);Case(host,DXGI_FORMAT_B8G8R8A8_UNORM,1,edit);Case(host,DXGI_FORMAT_R10G10B10A2_UNORM,3,edit);Case(host,DXGI_FORMAT_R16G16B16A16_FLOAT,2,edit);}std::printf("%s production Present utilities: 8 cases passed; deterministic edit substitute, no NGX\n",hardware?"Hardware":"WARP");return 0;}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
