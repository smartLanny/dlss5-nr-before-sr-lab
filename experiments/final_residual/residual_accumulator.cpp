#include "residual_accumulator.hpp"
#include "residual_accumulator_shader.hpp"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <mutex>
#include <stdexcept>
#include <string>

namespace nr::final_residual {
using Microsoft::WRL::ComPtr;
namespace {
constexpr auto copy=D3D12_RESOURCE_STATE_COPY_SOURCE;
constexpr auto read=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr auto write=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
void hr(HRESULT r,const char* why){require(SUCCEEDED(r),why);}
bool same_device(ID3D12DeviceChild* child,ID3D12Device* device){
    if(!child)return false;ComPtr<ID3D12Device> actual;
    return SUCCEEDED(child->GetDevice(IID_PPV_ARGS(&actual)))&&actual.Get()==device;
}
bool complete(ID3D12Fence* f,Id value){if(!f||!value||value==UINT64_MAX)return false;
    const auto v=f->GetCompletedValue();return v!=UINT64_MAX&&v>=value;}
void barrier(ID3D12GraphicsCommandList* l,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
    D3D12_RESOURCE_BARRIER t{};t.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    t.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&t);
}
bool shape(ID3D12Resource* r,ID3D12Device* d,unsigned w,unsigned h,DXGI_FORMAT format){
    if(!same_device(r,d))return false;const auto desc=r->GetDesc();
    return desc.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&desc.Width==w&&desc.Height==h&&
        desc.DepthOrArraySize==1&&desc.MipLevels==1&&desc.SampleDesc.Count==1&&desc.Format==format;
}
}
struct ResidualAccumulator::Impl {
    struct Registry {std::mutex lock;std::array<Impl*,4> slots{};std::array<bool,4> retired{};};
    static Registry& registry(){static auto* r=new Registry;return *r;}
    int slot=-1;bool ready=false,fault=false,unpublished=false,held=false,retiring=false,valid=false;
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> done,consumer;Id value=0,consumer_value=0,count=0,bytes=0,last_token=0;
    ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> pso;ComPtr<ID3D12DescriptorHeap> heap;
    std::array<ComPtr<ID3D12Resource>,2> residual,original;
    ComPtr<ID3D12Resource> output,input_original,input_enhanced,input_flow,input_anchor;
    Frame previous{};Id generation=0;unsigned width=0,height=0,index=1;bool accumulated=false;
    std::string message="uninitialized";
    ComPtr<ID3D12Resource> texture(DXGI_FORMAT format,bool uav){
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width=width;desc.Height=height;desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
        desc.Format=format;desc.Flags=uav?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> resource;
        hr(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,copy,nullptr,IID_PPV_ARGS(&resource)),"residual texture allocation");
        const auto allocation=device->GetResourceAllocationInfo(0,1,&desc);
        require(allocation.SizeInBytes!=UINT64_MAX,"residual allocation info");bytes+=allocation.SizeInBytes;
        return resource;
    }
    void init(ID3D12Device* d,ID3D12CommandQueue* q,unsigned w,unsigned h){
        require(d&&same_device(q,d)&&q->GetDesc().Type==D3D12_COMMAND_LIST_TYPE_DIRECT,"same-device DIRECT queue required");
        require(w&&h&&w<=4096&&h<=4096,"bounded nonzero source extent required");
        device=d;queue=q;width=w;height=h;
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};support.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
        hr(d->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support)),"FP16 format query");
        require((support.Support1&D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE)&&
            (support.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE),"FP16 sample/store unsupported");
        hr(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"residual allocator");
        hr(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"residual list");
        hr(list->Close(),"initial residual close");hr(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&done)),"residual fence");
        for(auto& r:residual)r=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,true);
        for(auto& r:original)r=texture(DXGI_FORMAT_R8G8B8A8_UNORM,false);
        output=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,true);
        D3D12_DESCRIPTOR_RANGE ranges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,6,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,2,0,0,6}};
        D3D12_ROOT_PARAMETER parameters[2]{};
        parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[0].DescriptorTable={2,ranges};
        parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[1].Constants={0,0,6};
        D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=2;desc.pParameters=parameters;
        desc.NumStaticSamplers=1;desc.pStaticSamplers=&sampler;
        ComPtr<ID3DBlob> blob,error;
        hr(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"residual root serialization");
        hr(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),"residual root");
        const auto result=D3DCompile(residual_accumulator_shader,sizeof(residual_accumulator_shader)-1,"original-final-residual",nullptr,nullptr,"CS","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&blob,&error);
        if(FAILED(result)&&error)throw std::runtime_error(static_cast<const char*>(error->GetBufferPointer()));hr(result,"residual shader");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};
        hr(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)),"residual PSO");
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=8;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"residual descriptors");ready=true;message="signed final residual ready";
    }
    bool quarantined() const {return unpublished||(held&&!consumer)||(done&&done->GetCompletedValue()==UINT64_MAX)||
        (consumer&&consumer->GetCompletedValue()==UINT64_MAX);}
    bool destroyable() const {return !quarantined()&&(!value||complete(done.Get(),value))&&(!held||complete(consumer.Get(),consumer_value));}
    void poison(const char* why){fault=true;ready=false;message=why;}
    void bind(unsigned next){
        auto cpu=heap->GetCPUDescriptorHandleForHeapStart();const auto step=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        ID3D12Resource* inputs[]={input_original.Get(),input_enhanced.Get(),input_flow.Get(),original[index].Get(),residual[index].Get(),input_anchor?input_anchor.Get():input_original.Get()};
        for(auto* r:inputs){D3D12_SHADER_RESOURCE_VIEW_DESC v{};v.Format=r->GetDesc().Format;v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
            v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;v.Texture2D.MipLevels=1;
            device->CreateShaderResourceView(r,&v,cpu);cpu.ptr+=step;}
        for(auto* r:{residual[next].Get(),output.Get()}){D3D12_UNORDERED_ACCESS_VIEW_DESC v{};v.Format=r->GetDesc().Format;
            v.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;device->CreateUnorderedAccessView(r,nullptr,&v,cpu);cpu.ptr+=step;}
    }
};
ResidualAccumulator::ResidualAccumulator(ID3D12Device* d,ID3D12CommandQueue* q,unsigned w,unsigned h):p_(std::make_unique<Impl>()){
    collect_retired();{auto& r=Impl::registry();std::lock_guard<std::mutex> guard(r.lock);
        for(unsigned i=0;i<r.slots.size();++i)if(!r.slots[i]){r.slots[i]=p_.get();p_->slot=int(i);break;}}
    if(p_->slot<0){p_->poison("residual context capacity exhausted");return;}
    try{p_->init(d,q,w,h);}catch(const std::exception& e){p_->poison(e.what());}
}
ResidualAccumulator::~ResidualAccumulator(){if(!p_||p_->slot<0)return;
    {auto& r=Impl::registry();std::lock_guard<std::mutex> guard(r.lock);r.retired[unsigned(p_->slot)]=true;(void)p_.release();}collect_retired();}
RetirementStats ResidualAccumulator::collect_retired()noexcept{
    auto& r=Impl::registry();std::lock_guard<std::mutex> guard(r.lock);RetirementStats stats;
    for(unsigned i=0;i<r.slots.size();++i){auto* p=r.slots[i];if(!p)continue;if(!r.retired[i]){++stats.active;continue;}
        if(p->destroyable()){delete p;r.slots[i]=nullptr;r.retired[i]=false;continue;}
        if(p->quarantined())++stats.quarantined;else ++stats.pending;}
    return stats;
}
bool ResidualAccumulator::initialized()const noexcept{return p_->ready&&!p_->fault;}
const char* ResidualAccumulator::status()const noexcept{return p_->message.c_str();}
Id ResidualAccumulator::dispatch_count()const noexcept{return p_->count;}
Id ResidualAccumulator::allocated_texture_bytes()const noexcept{return p_->bytes;}
ResidualSubmit ResidualAccumulator::submit(bool enabled,const Frame& frame,const Plan& plan,const GpuFlow& flow,const ResidualInputs& inputs)noexcept{
    auto& p=*p_;
    if(!enabled){p.valid=false;return ResidualSubmit::Disabled;}
    if(!p.ready||p.fault||p.retiring)return ResidualSubmit::Faulted;
    if(p.previous.source.valid()&&frame.source==p.previous.source)return ResidualSubmit::Duplicate;
    if(p.held)return ResidualSubmit::Busy;
    const bool reset=plan.action==Action::ResetBoth;const auto& f=flow.receipt;
    if(!inputs.settings.enabled||!inputs.settings.valid()||inputs.settings.scope!=frame.contract.scope||
        !frame.valid()||frame.contract.color_domain!=kResidualSdrCodeValues||!plan.work()||!plan.history_generation||
        plan.token<=p.last_token||flow.token!=plan.token||f.status!=FlowStatus::Ready||
        (f.backend!=Backend::AmdOpticalFlow&&f.backend!=Backend::NativeMotion)||frame.contract.backend!=f.backend||
        f.history_generation!=plan.history_generation||!(f.current==frame.source)||!(f.reference==plan.reference)||f.reset_executed!=reset||
        !(inputs.original_source==frame.source)||!(inputs.enhanced_source==frame.source)||
        frame.contract.width!=p.width||frame.contract.height!=p.height||
        !same_device(flow.completion,p.device.Get())||!complete(flow.completion,flow.value)||
        !shape(inputs.original,p.device.Get(),p.width,p.height,DXGI_FORMAT_R8G8B8A8_UNORM)||
        !shape(inputs.enhanced,p.device.Get(),p.width,p.height,DXGI_FORMAT_R16G16B16A16_FLOAT)||
        !shape(flow.vectors,p.device.Get(),p.width,p.height,DXGI_FORMAT_R32G32_FLOAT)||p.value>=UINT64_MAX-1)return ResidualSubmit::Rejected;
    const bool later=inputs.settings.scope==Scope::LaterLayers;
    if(later&&(!(inputs.anchor_source==frame.source)||
        !shape(inputs.anchor,p.device.Get(),p.width,p.height,DXGI_FORMAT_R16G16B16A16_FLOAT)))return ResidualSubmit::Rejected;
    if(reset){if(plan.reference.valid()||plan.history_generation<p.generation)return ResidualSubmit::Rejected;}
    else if(!p.valid||!p.previous.valid()||frame.scene_reset||!(frame.contract==p.previous.contract)||
        !same_stream(frame.source,p.previous.source)||frame.source.sequence!=p.previous.source.sequence+1||
        frame.source_time<=p.previous.source_time||!(plan.reference==p.previous.source)||plan.history_generation!=p.generation)return ResidualSubmit::Rejected;
    if(p.previous.source.valid()&&same_stream(frame.source,p.previous.source)&&frame.source.sequence<p.previous.source.sequence)return ResidualSubmit::Rejected;
    // Caller-owned immutable inputs must not alias a rotating private target.
    for(auto* owned:{p.residual[0].Get(),p.residual[1].Get(),p.original[0].Get(),p.original[1].Get(),p.output.Get()})
        if(inputs.original==owned||inputs.enhanced==owned||flow.vectors==owned||(later&&inputs.anchor==owned))return ResidualSubmit::Rejected;
    if(later&&(inputs.anchor==inputs.original||inputs.anchor==inputs.enhanced||inputs.anchor==flow.vectors))return ResidualSubmit::Rejected;
    try{
        hr(p.allocator->Reset(),"residual allocator Reset");hr(p.list->Reset(p.allocator.Get(),nullptr),"residual list Reset");
        p.input_original=inputs.original;p.input_enhanced=inputs.enhanced;p.input_flow=flow.vectors;
        p.input_anchor=later?inputs.anchor:nullptr;
        const unsigned next=1-p.index;p.bind(next);auto* l=p.list.Get();
        ID3D12Resource* reads[]={p.input_original.Get(),p.input_enhanced.Get(),p.input_flow.Get(),p.original[p.index].Get(),p.residual[p.index].Get()};
        for(auto* r:reads)barrier(l,r,copy,read);
        if(later)barrier(l,p.input_anchor.Get(),copy,read);
        barrier(l,p.residual[next].Get(),copy,write);barrier(l,p.output.Get(),copy,write);
        const float weight=HistoryWeight(inputs.settings,inputs.source_delta_ms);
        const bool accumulate=!reset&&f.motion_usable&&weight>0;
        struct {unsigned w,h,accumulate,later;float weight,tolerance;} params{p.width,p.height,unsigned(accumulate),unsigned(later),weight,inputs.settings.color_tolerance};
        ID3D12DescriptorHeap* heaps[]={p.heap.Get()};l->SetDescriptorHeaps(1,heaps);l->SetComputeRootSignature(p.root.Get());l->SetPipelineState(p.pso.Get());
        l->SetComputeRootDescriptorTable(0,p.heap->GetGPUDescriptorHandleForHeapStart());l->SetComputeRoot32BitConstants(1,6,&params,0);
        l->Dispatch((p.width+7)/8,(p.height+7)/8,1);
        for(auto* r:reads)barrier(l,r,read,copy);
        if(later)barrier(l,p.input_anchor.Get(),read,copy);
        barrier(l,p.residual[next].Get(),write,copy);barrier(l,p.output.Get(),write,copy);
        barrier(l,p.original[next].Get(),copy,D3D12_RESOURCE_STATE_COPY_DEST);l->CopyResource(p.original[next].Get(),p.input_original.Get());
        barrier(l,p.original[next].Get(),D3D12_RESOURCE_STATE_COPY_DEST,copy);
        hr(l->Close(),"residual Close");ID3D12CommandList* lists[]={l};p.unpublished=true;p.queue->ExecuteCommandLists(1,lists);
        hr(p.queue->Signal(p.done.Get(),p.value+1),"residual Signal");++p.value;++p.count;p.unpublished=false;p.held=true;
        p.previous=frame;p.generation=plan.history_generation;p.last_token=plan.token;p.index=next;p.valid=true;p.accumulated=accumulate;
        return ResidualSubmit::Submitted;
    }catch(const std::exception& e){p.poison(e.what());return ResidualSubmit::Faulted;}
}
bool ResidualAccumulator::poll(ResidualOutput& out)noexcept{
    out={};auto& p=*p_;if(!p.held||p.retiring||p.fault)return false;
    if(p.done->GetCompletedValue()==UINT64_MAX){p.poison("device lost; retain residual resources");return false;}
    out={p.last_token,p.generation,p.output.Get(),p.residual[p.index].Get(),p.done.Get(),p.value,p.accumulated};return true;
}
bool ResidualAccumulator::release_output(Id token,ID3D12Fence* fence,Id value)noexcept{
    auto& p=*p_;if(p.retiring||p.fault||!p.held||token!=p.last_token||!complete(p.done.Get(),p.value)||
        !same_device(fence,p.device.Get())||!complete(fence,value))return false;
    p.held=false;p.input_original.Reset();p.input_enhanced.Reset();p.input_flow.Reset();p.input_anchor.Reset();return true;
}
bool ResidualAccumulator::begin_retirement(Id token,ID3D12Fence* fence,Id value)noexcept{
    auto& p=*p_;if(p.retiring||p.unpublished||!p.held||token!=p.last_token||!value||value==UINT64_MAX||
        !same_device(fence,p.device.Get())||fence->GetCompletedValue()==UINT64_MAX)return false;
    p.consumer=fence;p.consumer_value=value;p.retiring=true;p.ready=false;return true;
}
}
