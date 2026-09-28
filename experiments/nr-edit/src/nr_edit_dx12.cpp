#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include "nr_edit_dx12.hpp"
#include <algorithm>
#include <cstring>
#include <new>
namespace nr_edit_dx12 {
namespace {
template<class T>void Drop(T*& p){if(p){p->Release();p=nullptr;}}
unsigned Family(unsigned f){if(f>=1&&f<=4)return 1;if(f>=9&&f<=14)return 2;if(f>=15&&f<=18)return 3;
    if(f>=23&&f<=25)return 4;if(f==26)return 5;if(f>=27&&f<=32)return 6;if(f>=33&&f<=38)return 7;
    if(f>=39&&f<=43)return 8;if(f>=53&&f<=59)return 9;if(f>=44&&f<=47)return 10;if(f>=19&&f<=22)return 11;return 0;}
bool Typeless(unsigned f){return f==1||f==9||f==15||f==23||f==27||f==33||f==39||f==53||f==44||f==19;}
bool FloatLoad(unsigned f){return f==2||f==10||f==16||f==24||f==26||f==28||f==34||f==41||f==54||f==56||f==21||f==46;}
bool Legal(unsigned physical,unsigned typed){return typed&&!Typeless(typed)&&FloatLoad(typed)&&
    (physical==typed||(Typeless(physical)&&Family(physical)&&Family(physical)==Family(typed)));}
void Barrier(ID3D12GraphicsCommandList* l,ID3D12Resource* r,unsigned before,unsigned after,unsigned mip){
    if(before==after)return;D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource=r;const auto d=r->GetDesc();
    b.Transition.Subresource=d.MipLevels==1&&d.DepthOrArraySize==1?D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES:mip;
    b.Transition.StateBefore=static_cast<D3D12_RESOURCE_STATES>(before);b.Transition.StateAfter=static_cast<D3D12_RESOURCE_STATES>(after);
    l->ResourceBarrier(1,&b);
}
}
class Kernels {
public:
    struct Packet {ID3D12DescriptorHeap* heap=nullptr;std::array<ID3D12Resource*,16> held{};
        nr_private_sr::Recording recording{};nr_private_sr::Fence receipt{};ID3D12Fence* fence=nullptr;
        Id serial=0;bool busy=false;};
    ID3D12Device* device=nullptr;Id device_id=0,queue_id=0,next=1;
    SameDevice same=nullptr;ID3D12RootSignature* root=nullptr;
    std::array<ID3D12PipelineState*,static_cast<unsigned>(Kernel::Count)> pso{};std::array<Packet,24> packets{};
    unsigned stride=0;bool poisoned=false,post_typed_load=false;
    nr_private_sr::RecordingReleased released=nullptr;void* release_user=nullptr;
    void FreePacket(Packet& p){for(auto*& r:p.held)Drop(r);Drop(p.fence);p.recording={};p.receipt={};p.busy=false;p.serial=0;}
    void FreeCold(){for(auto& p:packets){FreePacket(p);Drop(p.heap);}for(auto*& p:pso)Drop(p);Drop(root);Drop(device);}
};
Kernels* Create(ID3D12Device* device,Id device_id,Id queue_id,const std::array<Bytecode,static_cast<unsigned>(Kernel::Count)>& kernels,SameDevice same,unsigned* error){
    if(error)*error=0;if(!device||!device_id||!queue_id||!same)return nullptr;
    auto* k=new(std::nothrow)Kernels;if(!k)return nullptr;k->device=device;device->AddRef();
    k->device_id=device_id;k->queue_id=queue_id;k->same=same;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT post_caps{};post_caps.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    k->post_typed_load=SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&post_caps,sizeof(post_caps)))&&
        (post_caps.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD)!=0;
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,11,0,0,0};
    ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,5,0,0,11};
    D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable={2,ranges};params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants={0,0,36};
    D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=2;desc.pParameters=params;
    ID3DBlob *blob=nullptr,*errors=nullptr;auto hr=D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors);Drop(errors);
    if(SUCCEEDED(hr))hr=device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&k->root));Drop(blob);
    bool any=false;
    for(unsigned i=0;SUCCEEDED(hr)&&i<kernels.size();i++){if(!kernels[i].data||!kernels[i].bytes||
        (i==static_cast<unsigned>(Kernel::PostCompose)&&!k->post_typed_load))continue;
        D3D12_COMPUTE_PIPELINE_STATE_DESC p{};p.pRootSignature=k->root;p.CS={kernels[i].data,kernels[i].bytes};
        hr=device->CreateComputePipelineState(&p,IID_PPV_ARGS(&k->pso[i]));any=true;}
    if(!any)hr=E_INVALIDARG;
    k->stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for(auto& p:k->packets){if(FAILED(hr))break;D3D12_DESCRIPTOR_HEAP_DESC h{};
        h.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;h.NumDescriptors=16;h.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr=device->CreateDescriptorHeap(&h,IID_PPV_ARGS(&p.heap));}
    if(FAILED(hr)){if(error)*error=static_cast<unsigned>(hr);k->FreeCold();delete k;return nullptr;}
    return k;
}
Kernels* Create(ID3D12Device* device,Id device_id,Id queue_id,const std::array<Bytecode,5>& code,SameDevice same,unsigned* error){
    std::array<Bytecode,static_cast<unsigned>(Kernel::Count)> all{};std::copy(code.begin(),code.end(),all.begin());
    return Create(device,device_id,queue_id,all,same,error);
}
bool Poisoned(const Kernels* k){return !k||k->poisoned;}
void SetRetirementGuard(Kernels* k,nr_private_sr::RecordingReleased f,void* u){if(k){k->released=f;k->release_user=u;}}
void MarkUncertain(Kernels* k){if(k)k->poisoned=true;}
bool Collect(Kernels* k){if(!k||k->poisoned)return false;
    if(FAILED(k->device->GetDeviceRemovedReason())){k->poisoned=true;return false;}
    for(auto& p:k->packets){if(!p.busy||!p.fence)continue;const auto value=p.fence->GetCompletedValue();
        if(value==UINT64_MAX){k->poisoned=true;return false;}
        if(nr_private_sr::Complete(p.receipt,{p.receipt.queue,reinterpret_cast<Id>(p.fence),value})&&
            (!k->released||k->released(k->release_user,p.recording)))k->FreePacket(p);}
    return true;
}
bool Check(Kernels* k,const View& v,bool write,unsigned width,unsigned height){
    if(!v.resource)return true;if(!v.state_known)return false;
    const auto d=v.resource->GetDesc();
    if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.SampleDesc.Count!=1||d.DepthOrArraySize!=1||
       v.mip>=d.MipLevels||v.mip>=32||!Legal(d.Format,v.format)||(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))return false;
    if(write&&((d.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)==0||
       std::max<UINT64>(1,d.Width>>v.mip)<width||std::max(1u,d.Height>>v.mip)<height))return false;
    ID3D12Device* actual=nullptr;const bool got=SUCCEEDED(v.resource->GetDevice(IID_PPV_ARGS(&actual)));
    const bool same=got&&actual&&k->same(k->device,actual);Drop(actual);if(!same)return false;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT caps{};caps.Format=static_cast<DXGI_FORMAT>(v.format);
    if(FAILED(k->device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&caps,sizeof(caps))))return false;
    return (caps.Support1&D3D12_FORMAT_SUPPORT1_TEXTURE2D)&&
        (write?((caps.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE)!=0):((caps.Support1&D3D12_FORMAT_SUPPORT1_SHADER_LOAD)!=0));
}
bool ValidateRead(Kernels* k,const View& v,unsigned width,unsigned height){
    if(!k||k->poisoned||!v.resource||!width||!height||v.mip>=32)return false;
    const auto d=v.resource->GetDesc();
    if(std::max<UINT64>(1,d.Width>>v.mip)!=width||std::max(1u,d.Height>>v.mip)!=height)return false;
    return Check(k,v,false,width,height);
}
Answer Record(Kernels* k,ID3D12GraphicsCommandList* list,const Work& w){
    if(!k||k->poisoned)return {Status::Poisoned};
    const auto kernel=static_cast<unsigned>(w.kernel);
    if(w.kernel==Kernel::PostCompose&&!k->post_typed_load)return {Status::Format};
    if(!list||kernel>=k->pso.size()||!k->pso[kernel]||!w.recording||
       w.recording.list!=reinterpret_cast<Id>(list)||w.recording.device!=k->device_id||w.recording.queue!=k->queue_id||
       !w.width||!w.height||w.width>16384||w.height>16384||!w.constants||
       w.constant_bytes!=(w.kernel==Kernel::NormalizeGuides?80u:kernel<=2?144u:kernel>=static_cast<unsigned>(Kernel::SparseAdvance)?64u:48u))return {Status::Invalid};
    unsigned raw_constants[36]{};std::memcpy(raw_constants,w.constants,w.constant_bytes);
    const auto read=[&](unsigned slot){return w.reads[slot].resource!=nullptr;};
    const auto write=[&](unsigned slot){return w.writes[slot].resource!=nullptr;};
    bool required=false;
    switch(w.kernel){
    case Kernel::RrGuides:required=read(2)&&read(3)&&write(0)&&write(1)&&(!raw_constants[33]||read(10));break;
    case Kernel::RrAccumulate:required=read(0)&&read(1)&&read(7)&&read(9)&&write(2)&&write(3)&&(!raw_constants[10]||(read(4)&&read(5)));break;
    case Kernel::RrCompose:required=read(6)&&read(8)&&read(9)&&write(4);break;
    case Kernel::DeferredEncode:required=read(0)&&read(1)&&write(0)&&write(1);break;
    case Kernel::DeferredCompose:required=read(2)&&read(3)&&read(4)&&write(2);break;
    case Kernel::NormalizeGuides:required=read(0)&&read(1)&&write(0)&&write(1);break;
    case Kernel::PostExtract:required=read(0)&&read(1)&&write(0)&&write(1);break;
    case Kernel::PostCompose:required=read(1)&&read(2)&&read(3)&&write(2);break;
    case Kernel::SparseAdvance:required=read(0)&&read(3)&&write(0)&&write(1)&&write(2)&&write(3)&&
        (!raw_constants[2]||read(1))&&(!raw_constants[3]||(read(2)&&read(4)&&read(5)&&read(6)&&read(7)));break;
    default:break;
    }
    if(!required)return {Status::Invalid};
    for(unsigned i=0;i<w.reads.size();++i)for(unsigned j=0;j<i;++j)
        if(w.reads[i].resource&&w.reads[i].resource==w.reads[j].resource&&w.reads[i].mip==w.reads[j].mip&&w.reads[i].state!=w.reads[j].state)return {Status::Invalid};
    ID3D12Device* actual=nullptr;const bool got=SUCCEEDED(list->GetDevice(IID_PPV_ARGS(&actual)));
    const bool same=got&&actual&&k->same(k->device,actual);Drop(actual);
    if(!same||list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT)return {Status::DeviceMismatch};
    for(const auto& r:w.reads)if(!Check(k,r,false,w.width,w.height))return {Status::Format};
    for(unsigned i=0;i<w.writes.size();i++){const auto& v=w.writes[i];if(!Check(k,v,true,w.width,w.height))return {Status::Format};
        if(!v.resource)continue;for(const auto& r:w.reads)if(v.resource==r.resource&&v.mip==r.mip)return {Status::Invalid};
        for(unsigned j=0;j<i;j++)if(v.resource==w.writes[j].resource&&v.mip==w.writes[j].mip)return {Status::Invalid};}
    if(!Collect(k))return {Status::Poisoned};unsigned index=0;
    while(index<k->packets.size()&&k->packets[index].busy)++index;
    if(index==k->packets.size())return {Status::Busy};if(k->next==UINT64_MAX)return {Status::Invalid};
    auto& packet=k->packets[index];auto cpu=packet.heap->GetCPUDescriptorHandleForHeapStart();
    for(unsigned i=0;i<w.reads.size();i++){const auto& v=w.reads[i];D3D12_SHADER_RESOURCE_VIEW_DESC d{};
        d.Format=v.resource?static_cast<DXGI_FORMAT>(v.format):DXGI_FORMAT_R32G32B32A32_FLOAT;
        d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        d.Texture2D.MostDetailedMip=v.mip;d.Texture2D.MipLevels=1;k->device->CreateShaderResourceView(v.resource,&d,cpu);
        cpu.ptr+=k->stride;if(v.resource){packet.held[i]=v.resource;v.resource->AddRef();}}
    for(unsigned i=0;i<w.writes.size();i++){const auto& v=w.writes[i];D3D12_UNORDERED_ACCESS_VIEW_DESC d{};
        d.Format=v.resource?static_cast<DXGI_FORMAT>(v.format):DXGI_FORMAT_R32G32B32A32_FLOAT;
        d.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;d.Texture2D.MipSlice=v.mip;
        k->device->CreateUnorderedAccessView(v.resource,nullptr,&d,cpu);cpu.ptr+=k->stride;
        if(v.resource){packet.held[11+i]=v.resource;v.resource->AddRef();}}
    packet.recording=w.recording;packet.serial=k->next++;packet.busy=true;
    for(unsigned i=0;i<w.reads.size();i++){const auto& v=w.reads[i];if(!v.resource)continue;bool duplicate=false;
        for(unsigned j=0;j<i;j++)duplicate|=v.resource==w.reads[j].resource&&v.mip==w.reads[j].mip;
        if(!duplicate)Barrier(list,v.resource,v.state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,v.mip);}
    for(const auto& v:w.writes)if(v.resource)Barrier(list,v.resource,v.state,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,v.mip);
    ID3D12DescriptorHeap* heaps[]={packet.heap};list->SetDescriptorHeaps(1,heaps);list->SetComputeRootSignature(k->root);
    list->SetComputeRootDescriptorTable(0,packet.heap->GetGPUDescriptorHandleForHeapStart());
    unsigned constants[36]{};std::memcpy(constants,w.constants,w.constant_bytes);list->SetComputeRoot32BitConstants(1,36,constants,0);
    list->SetPipelineState(k->pso[kernel]);list->Dispatch((w.width+7)/8,(w.height+7)/8,1);
    for(const auto& v:w.writes)if(v.resource){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=v.resource;
        list->ResourceBarrier(1,&b);Barrier(list,v.resource,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,v.state,v.mip);}
    for(unsigned i=0;i<w.reads.size();i++){const auto& v=w.reads[i];if(!v.resource)continue;bool duplicate=false;
        for(unsigned j=0;j<i;j++)duplicate|=v.resource==w.reads[j].resource&&v.mip==w.reads[j].mip;
        if(!duplicate)Barrier(list,v.resource,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,v.state,v.mip);}
    return {Status::Ready,{packet.serial,index},true};
}
bool Submitted(Kernels* k,nr_private_sr::Recording r,ID3D12Fence* fence,nr_private_sr::Fence receipt,bool success){
    if(!k||k->poisoned)return false;bool found=false;
    for(auto& p:k->packets)if(p.busy&&p.recording==r){found=true;
        if(!success||!fence||!receipt||receipt.queue!=k->queue_id||receipt.object!=reinterpret_cast<Id>(fence)||p.fence){k->poisoned=true;return false;}
        p.receipt=receipt;p.fence=fence;fence->AddRef();}
    return found;
}
bool Discarded(Kernels* k,nr_private_sr::Recording r){if(!k||k->poisoned)return false;
    for(const auto& p:k->packets)if(p.busy&&p.recording==r&&p.fence)return false;
    for(auto& p:k->packets)if(p.busy&&p.recording==r)k->FreePacket(p);return true;
}
bool ReadyToRetire(Kernels* k){if(!k)return true;if(!Collect(k))return false;
    for(const auto& p:k->packets)if(p.busy)return false;return true;}
bool DestroyAfterRetirement(Kernels*& k){if(!k)return true;if(!Collect(k))return false;
    for(const auto& p:k->packets)if(p.busy)return false;k->FreeCold();delete k;k=nullptr;return true;}
ID3D12Resource* CreatePlane(ID3D12Device* device,unsigned w,unsigned h,unsigned format,std::uint64_t budget,std::uint64_t* bytes){
    if(bytes)*bytes=0;if(!device||!bytes||!w||!h||w>16384||h>16384||!FloatLoad(format))return nullptr;
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;
    d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=static_cast<DXGI_FORMAT>(format);d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const auto a=device->GetResourceAllocationInfo(0,1,&d);if(!a.SizeInBytes||a.SizeInBytes==UINT64_MAX||a.SizeInBytes>budget)return nullptr;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
    ID3D12Resource* out=nullptr;if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&out))))return nullptr;
    *bytes=a.SizeInBytes;return out;
}
}
