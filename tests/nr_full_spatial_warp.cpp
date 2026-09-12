#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include "../src/nr_dual_spatial_d3d12.hpp"
#include "../src/nr_full_pass.hpp"
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <cmath>
unsigned checks=0;
void Check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
void Hr(HRESULT hr,const char* why){Check(SUCCEEDED(hr),why);}
template<class T>void Drop(T*& p){if(p){p->Release();p=nullptr;}}
struct Harness {
    ID3D12Device* device=nullptr;ID3D12CommandQueue* queue=nullptr;
    ID3D12CommandAllocator* allocator=nullptr;ID3D12GraphicsCommandList* list=nullptr;
    ID3D12Fence* fence=nullptr;HANDLE event=nullptr;UINT64 serial=0;
    Harness(){IDXGIFactory4* factory=nullptr;IDXGIAdapter* adapter=nullptr;
        Hr(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)),"DXGI factory");
        Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)),"WARP adapter");
        Hr(D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"WARP device");
        Drop(adapter);Drop(factory);D3D12_COMMAND_QUEUE_DESC q{};
        Hr(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"queue");
        Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"allocator");
        Hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,IID_PPV_ARGS(&list)),"list");
        Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        Check(event!=nullptr,"event");
    }
    void Submit(){Hr(list->Close(),"close");ID3D12CommandList* commands[]={list};queue->ExecuteCommandLists(1,commands);
        Hr(queue->Signal(fence,++serial),"signal");Hr(fence->SetEventOnCompletion(serial,event),"completion event");
        Check(WaitForSingleObject(event,10000)==WAIT_OBJECT_0,"bounded exact completion");
        Check(fence->GetCompletedValue()>=serial,"real fence completed");
    }
    void Reset(){Hr(allocator->Reset(),"allocator reset");Hr(list->Reset(allocator,nullptr),"list reset");}
    ~Harness(){if(event)CloseHandle(event);Drop(fence);Drop(list);Drop(allocator);Drop(queue);Drop(device);}
};
ID3D12Resource* Texture(ID3D12Device* device){
    D3D12_HEAP_PROPERTIES h{};h.Type=D3D12_HEAP_TYPE_DEFAULT;h.CreationNodeMask=h.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=d.Height=8;
    d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;ID3D12Resource* resource=nullptr;
    Hr(device->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&resource)),"texture");return resource;
}
void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* r,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){
    if(from==to)return;D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};list->ResourceBarrier(1,&b);
}
struct ModelStub {
    ID3D12RootSignature* root=nullptr;ID3D12PipelineState* pso=nullptr;ID3D12DescriptorHeap* heap=nullptr;
    unsigned calls=0;bool fail=false;
    static std::uint32_t Evaluate(void* user,ID3D12GraphicsCommandList* list,const void*,const void*){
        auto& self=*static_cast<ModelStub*>(user);++self.calls;
        if(self.fail)return 0xBAD00005u;
        list->SetDescriptorHeaps(1,&self.heap);list->SetComputeRootSignature(self.root);
        list->SetComputeRootDescriptorTable(0,self.heap->GetGPUDescriptorHandleForHeapStart());
        list->SetPipelineState(self.pso);list->Dispatch(1,1,1);return 1;
    }
    void Init(ID3D12Device* device,ID3D12Resource* input,ID3D12Resource* output){
        D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,1}};
        D3D12_ROOT_PARAMETER p{};p.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p.DescriptorTable={2,ranges};
        D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=1;rd.pParameters=&p;ID3DBlob *blob=nullptr,*error=nullptr;
        Hr(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"model stub root serialize");Drop(error);
        Hr(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),"model stub root");Drop(blob);
        const char shader[]="Texture2D<float4> a:register(t0);RWTexture2D<float4> b:register(u0);[numthreads(8,8,1)]void main(uint3 p:SV_DispatchThreadID){b[p.xy]=a.Load(int3(p.xy,0))+float4(.125,.125,.125,0);}";
        Hr(D3DCompile(shader,sizeof(shader)-1,"explicit-model-stub",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&blob,&error),"model stub shader");Drop(error);
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root;pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};
        Hr(device->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)),"model stub pipeline");Drop(blob);
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=2;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        Hr(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"model stub heap");
        auto cpu=heap->GetCPUDescriptorHandleForHeapStart();D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
        device->CreateShaderResourceView(input,&srv,cpu);cpu.ptr+=device->GetDescriptorHandleIncrementSize(hd.Type);
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=srv.Format;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(output,nullptr,&uav,cpu);
    }
    ~ModelStub(){Drop(heap);Drop(pso);Drop(root);}
};
bool Retired(void*){return true;}
std::vector<unsigned char> ReadTexture(Harness& h,ID3D12Resource* source) {
    const auto desc=source->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;
    h.device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=bytes;
    buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES memory{};memory.Type=D3D12_HEAP_TYPE_READBACK;memory.CreationNodeMask=memory.VisibleNodeMask=1;
    ID3D12Resource* readback=nullptr;
    Hr(h.device->CreateCommittedResource(&memory,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)),"readback");
    Transition(h.list,source,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=source;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=readback;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;
    h.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    Transition(h.list,source,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);h.Submit();
    void* mapped=nullptr;D3D12_RANGE range{0,static_cast<SIZE_T>(bytes)};Hr(readback->Map(0,&range,&mapped),"map output");
    const unsigned row=unsigned(desc.Width)*(desc.Format==DXGI_FORMAT_R32G32B32A32_FLOAT?16:8);
    std::vector<unsigned char> data(row*desc.Height);
    for(unsigned y=0;y<desc.Height;++y)std::memcpy(data.data()+y*row,static_cast<unsigned char*>(mapped)+y*footprint.Footprint.RowPitch,row);
    D3D12_RANGE none{0,0};readback->Unmap(0,&none);Drop(readback);h.Reset();return data;
}
int main(){try {
    Harness h;
    for(auto domain:{nr_dual::ModelDomain::DirectValue,nr_dual::ModelDomain::SrgbProxy})
    for(auto scale:{nr_dual::Scale{1,1},nr_dual::Scale{1,2},nr_dual::Scale{3,4}})
    for(unsigned passes=2;passes<=5;++passes){
        auto* primary=Texture(h.device);ID3D12DescriptorHeap *gpu=nullptr,*cpu=nullptr;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=1;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        Hr(h.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&gpu)),"clear GPU heap");hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        Hr(h.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&cpu)),"clear CPU heap");D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
        u.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;u.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        h.device->CreateUnorderedAccessView(primary,nullptr,&u,gpu->GetCPUDescriptorHandleForHeapStart());
        h.device->CreateUnorderedAccessView(primary,nullptr,&u,cpu->GetCPUDescriptorHandleForHeapStart());
        h.list->SetDescriptorHeaps(1,&gpu);const float zero[4]{};
        h.list->ClearUnorderedAccessViewFloat(gpu->GetGPUDescriptorHandleForHeapStart(),cpu->GetCPUDescriptorHandleForHeapStart(),primary,zero,0,nullptr);
        Transition(h.list,primary,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        h.Submit();h.Reset();Drop(gpu);Drop(cpu);
        std::string error;auto* pipelines=nr_dual::dx12::CreatePipelines(h.device,error);Check(pipelines!=nullptr,"real spatial pipelines");
        const auto plan=nr_full::MakePlan({true,passes,scale},{8,8});
        nr_dual::dx12::SpatialSlot* spatial[nr_full::kAuxLayers]{};ModelStub models[nr_full::kAuxLayers];
        ID3D12Resource* source=primary;unsigned source_state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        for(unsigned layer=0;layer<passes-1;++layer){
            nr_dual::dx12::Surfaces base{};
            const bool cumulative=plan.reduced&&layer>0&&layer==passes-2;
            if(cumulative)base=nr_dual::dx12::GetSurfaces(spatial[0]);
            spatial[layer]=nr_dual::dx12::CreateSpatialSlot(pipelines,source,plan.layers[layer],domain,1024*1024,error,nullptr,nullptr,cumulative?&base:nullptr);
            Check(spatial[layer]!=nullptr,"independent spatial slot");
            const auto surfaces=nr_dual::dx12::GetSurfaces(spatial[layer]);models[layer].Init(h.device,surfaces.input,surfaces.output);
            Check(surfaces.secondary_extent==plan.auxiliary,"every auxiliary holds the same second-pass raster");
            Check((surfaces.input==source)==(layer>0||!plan.reduced)&&surfaces.output!=source,"only first auxiliary downsamples; later input is preceding complete output");
            Check(nr_dual::dx12::MatchesResidualBase(spatial[layer],cumulative?&base:nullptr),"immutable cumulative base ownership");
            if(cumulative){auto bad=base;bad.secondary_extent.width++;
                auto* rejected=nr_dual::dx12::CreateSpatialSlot(pipelines,source,plan.layers[layer],domain,1024*1024,error,nullptr,nullptr,&bad);
                Check(rejected==nullptr,"mismatched cumulative raster refused before GPU work");}
            nr_dual::Admission a{};a.primary_feature=primary;a.secondary_feature=&models[layer];a.primary_params=primary;a.secondary_params=&models[layer];
            a.secondary_create_extent=plan.auxiliary;a.creation_completion_proven=h.fence->GetCompletedValue()>=h.serial;
            a.recording_and_queue_admitted=a.dependencies_pinned=a.current_inputs_and_guides_bound=true;
            const auto result=nr_dual::dx12::RecordSecond(pipelines,spatial[layer],h.list,source_state,plan.layers[layer],a,ModelStub::Evaluate,&models[layer]);
            Check(result.selection!=nr_dual::Selection::Primary&&models[layer].calls==1,"one actual dispatch per auxiliary layer");
            source=surfaces.output;source_state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        }
        const auto data=ReadTexture(h,source);
        const unsigned short expected[]={0,0x3000,0x3400,0x3600,0x3800};
        for(unsigned i=0;i<data.size()/8;++i){const auto* value=reinterpret_cast<const unsigned short*>(data.data())+i*4;
            for(unsigned ch=0;ch<3;++ch)Check(value[ch]==expected[passes-1],"all final pixels include every layer increment");
            Check(value[3]==(plan.reduced?0x3c00:0),"auxiliary alpha preserved");}
        if(plan.reduced){
            auto final=nr_dual::dx12::GetSurfaces(spatial[passes-2]);Check(final.low_delta!=nullptr,"final composition receives cumulative residual");
            const auto delta=ReadTexture(h,final.low_delta);const auto* values=reinterpret_cast<const float*>(delta.data());
            const float encoded=.125f*(passes-1);const float expected_delta=domain==nr_dual::ModelDomain::DirectValue?encoded:std::pow((encoded+.055f)/1.055f,2.4f);
            for(unsigned i=0;i<delta.size()/16;++i){for(unsigned ch=0;ch<3;++ch)
                Check(std::abs(values[i*4+ch]-expected_delta)<0.00001f,"residual uses last result minus first reduced input, retaining all edits");
                Check(values[i*4+3]==1,"valid cumulative residual");}
            const auto first=ReadTexture(h,nr_dual::dx12::GetSurfaces(spatial[0]).input);
            for(unsigned i=0;i<first.size()/8;++i)for(unsigned ch=0;ch<3;++ch)
                Check(reinterpret_cast<const unsigned short*>(first.data())[i*4+ch]==0,"original reduced baseline remains immutable");
        }
        for(auto& slot:spatial)Check(nr_dual::dx12::DestroySpatialSlotAfterRetirement(slot,Retired,nullptr),"retire exact-completed layer");
        Check(nr_dual::dx12::DestroyPipelinesAfterRetirement(pipelines,Retired,nullptr),"retire shared spatial pipeline");Drop(primary);
    }
    Hr(h.list->Close(),"final close");
    std::cout<<"PASS "<<checks<<" WARP spatial-chain checks (2-5 layers, 50/75/100%, both model domains; explicit shader substitute, no NVIDIA NGX)\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
