// Standalone C++ TU: do not compile with the Core's CINTERFACE macros.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include "nr_edit_dx12_dispatch.hpp"
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
namespace nr_edit::dx12 {
namespace {
template<class T> void Drop(T*& p){if(p){p->Release();p=nullptr;}}
bool SameDevice(ID3D12Device* a,ID3D12Device* b){
    IUnknown *x=nullptr,*y=nullptr;
    const bool ok=a&&b&&SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x)))&&
        SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y)))&&x==y;
    Drop(x);Drop(y);return ok;
}
bool Belongs(ID3D12DeviceChild* r,ID3D12Device* d){
    ID3D12Device* owner=nullptr;const bool ok=r&&SUCCEEDED(r->GetDevice(IID_PPV_ARGS(&owner)))&&SameDevice(d,owner);
    Drop(owner);return ok;
}
void Error(char* s,std::size_t n,const char* stage,HRESULT hr){if(s&&n)std::snprintf(s,n,"%s HRESULT=0x%08X",stage,unsigned(hr));}
}
struct Dispatcher {
    struct Flight {dispatch::Ticket ticket{};ID3D12DescriptorHeap* heap=nullptr;ID3D12Resource* cb=nullptr;
        std::array<ID3D12Resource*,16> resources{};ID3D12CommandQueue* queue=nullptr;ID3D12Fence* fence=nullptr;std::uint64_t value=0;};
    ID3D12Device* device=nullptr;ID3D12RootSignature* root=nullptr;ID3D12PipelineState* pso=nullptr;
    dispatch::Slots<12> slots;std::array<Flight,12> flights{};unsigned stride=0;
};
namespace {
void ClearFlight(Dispatcher::Flight& f){for(auto*& p:f.resources)Drop(p);Drop(f.queue);Drop(f.fence);f.ticket={};f.value=0;}
void DestroyCold(Dispatcher*& d){if(!d)return;for(auto& f:d->flights){ClearFlight(f);Drop(f.heap);Drop(f.cb);}Drop(d->pso);Drop(d->root);Drop(d->device);delete d;d=nullptr;}
bool ValidView(Dispatcher* d,const View& v,bool write){
    if(!v.resource)return true;
    if(!Belongs(v.resource,d->device)||!v.format)return false;
    const auto r=v.resource->GetDesc();
    if(r.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||r.DepthOrArraySize!=1||r.MipLevels!=1||r.SampleDesc.Count!=1||
       v.mip>=r.MipLevels||r.Format!=DXGI_FORMAT(v.format)||(write&&(r.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)==0))return false;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT s{};s.Format=DXGI_FORMAT(v.format);
    if(FAILED(d->device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&s,sizeof(s))))return false;
    // This executor admits exact typed private textures only. Source TYPELESS
    // normalization stays in the Core adapter; no guessed family reinterpretation.
    return (s.Support1&D3D12_FORMAT_SUPPORT1_TEXTURE2D)&&
        (write?((s.Support1&D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW)&&(s.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE)):(s.Support1&D3D12_FORMAT_SUPPORT1_SHADER_LOAD));
}
}
Dispatcher* Create(ID3D12Device* device,const wchar_t* path,const char* entry,char* error,std::size_t size){
    if(error&&size)error[0]=0;
    if(!device||!path||!entry||device->GetNodeCount()!=1){Error(error,size,"arguments",E_INVALIDARG);return nullptr;}
    auto* d=new(std::nothrow) Dispatcher;if(!d)return nullptr;d->device=device;device->AddRef();
    ID3DBlob *code=nullptr,*messages=nullptr,*root=nullptr;
    auto hr=D3DCompileFromFile(path,nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,"cs_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&messages);
    if(FAILED(hr)){Error(error,size,"shader compile",hr);Drop(messages);Drop(code);DestroyCold(d);return nullptr;}Drop(messages);
    D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,11,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,5,0,0,11}};
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[0].DescriptorTable={2,ranges};
    params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;params[1].Descriptor={0,0};
    D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=2;rd.pParameters=params;
    hr=D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&root,&messages);Drop(messages);
    if(SUCCEEDED(hr))hr=device->CreateRootSignature(0,root->GetBufferPointer(),root->GetBufferSize(),IID_PPV_ARGS(&d->root));Drop(root);
    if(SUCCEEDED(hr)){D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=d->root;pd.CS={code->GetBufferPointer(),code->GetBufferSize()};
        hr=device->CreateComputePipelineState(&pd,IID_PPV_ARGS(&d->pso));}Drop(code);
    d->stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for(auto& f:d->flights){if(FAILED(hr))break;
        D3D12_DESCRIPTOR_HEAP_DESC h{};h.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;h.NumDescriptors=16;h.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr=device->CreateDescriptorHeap(&h,IID_PPV_ARGS(&f.heap));if(FAILED(hr))break;
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_UPLOAD;hp.CreationNodeMask=hp.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC cb{};cb.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;cb.Width=256;cb.Height=1;cb.DepthOrArraySize=cb.MipLevels=1;
        cb.SampleDesc.Count=1;cb.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        hr=device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&cb,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&f.cb));
    }
    if(FAILED(hr)){Error(error,size,"pipeline/resources",hr);DestroyCold(d);}return d;
}
void Collect(Dispatcher* d){if(!d||d->slots.Poisoned())return;
    if(FAILED(d->device->GetDeviceRemovedReason())){Quarantine(d);return;}
    for(auto& f:d->flights)if(f.ticket&&f.fence){
        auto done=f.fence->GetCompletedValue();
        if(d->slots.Complete(f.ticket,{reinterpret_cast<std::uint64_t>(f.queue),reinterpret_cast<std::uint64_t>(f.fence),done}))ClearFlight(f);
    }
}
dispatch::Ticket Record(Dispatcher* d,const Pass& p){
    if(!d||!p.list||!p.constants||!p.constant_bytes||p.constant_bytes>256||!p.width||!p.height||
       p.width>16384||p.height>16384||!Belongs(p.list,d->device)||p.list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT)return {};
    for(const auto& v:p.reads)if(!ValidView(d,v,false))return {};
    bool output=false;for(const auto& v:p.writes){if(!ValidView(d,v,true))return {};output|=v.resource!=nullptr;
        if(v.resource)for(const auto& r:p.reads)if(r.resource==v.resource&&r.mip==v.mip)return {};}
    if(!output)return {};
    for(const auto& v:p.writes)if(v.resource){const auto r=v.resource->GetDesc();
        if(p.width>std::max<UINT64>(1,r.Width>>v.mip)||p.height>std::max<UINT>(1,r.Height>>v.mip))return {};}
    Collect(d);auto t=d->slots.Begin();if(!t)return {};auto& f=d->flights[t.slot];
    void* mapped=nullptr;const D3D12_RANGE no_read{0,0};auto hr=f.cb->Map(0,&no_read,&mapped);
    if(FAILED(hr)||!mapped){d->slots.CancelBeforeRecord(t);return {};}
    std::memset(mapped,0,256);std::memcpy(mapped,p.constants,p.constant_bytes);const D3D12_RANGE written{0,256};f.cb->Unmap(0,&written);
    auto cpu=f.heap->GetCPUDescriptorHandleForHeapStart();
    for(unsigned i=0;i<16;++i){auto& v=i<11?p.reads[i]:p.writes[i-11];
        f.resources[i]=v.resource;if(v.resource)v.resource->AddRef();
        const auto format=v.resource?DXGI_FORMAT(v.format):DXGI_FORMAT_R32G32B32A32_FLOAT;
        if(i<11){D3D12_SHADER_RESOURCE_VIEW_DESC s{};s.Format=format;s.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
            s.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;s.Texture2D.MostDetailedMip=v.mip;s.Texture2D.MipLevels=1;
            d->device->CreateShaderResourceView(v.resource,&s,cpu);
        }else{D3D12_UNORDERED_ACCESS_VIEW_DESC u{};u.Format=format;u.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;u.Texture2D.MipSlice=v.mip;
            d->device->CreateUnorderedAccessView(v.resource,nullptr,&u,cpu);}
        cpu.ptr+=d->stride;
    }
    f.ticket=t;
    ID3D12DescriptorHeap* heaps[]={f.heap};p.list->SetDescriptorHeaps(1,heaps);p.list->SetComputeRootSignature(d->root);
    p.list->SetComputeRootDescriptorTable(0,f.heap->GetGPUDescriptorHandleForHeapStart());p.list->SetComputeRootConstantBufferView(1,f.cb->GetGPUVirtualAddress());
    p.list->SetPipelineState(d->pso);p.list->Dispatch((p.width+7)/8,(p.height+7)/8,1);
    // Caller records transitions/ordering and restores its real command state.
    // There are deliberately no hidden Execute/Signal/Wait/Present calls here.
    return t;
}
bool Submitted(Dispatcher* d,dispatch::Ticket t,ID3D12CommandQueue* queue,ID3D12Fence* fence,std::uint64_t value){
    if(!d||!queue||!fence||!Belongs(queue,d->device)||!Belongs(fence,d->device)||queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)return false;
    if(!d->slots.Submit(t,{reinterpret_cast<std::uint64_t>(queue),reinterpret_cast<std::uint64_t>(fence),value}))return false;
    auto& f=d->flights[t.slot];f.queue=queue;queue->AddRef();f.fence=fence;fence->AddRef();f.value=value;return true;
}
void Quarantine(Dispatcher* d){if(d)d->slots.Poison();}
bool DestroyAfterRetirement(Dispatcher*& d){if(!d)return true;Collect(d);if(!d->slots.Idle())return false;DestroyCold(d);return true;}
}
