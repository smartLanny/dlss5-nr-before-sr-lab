// Project-owned extraction of the FFX1.1.4 API/resource path from PR256.
// No GPL implementation, ExternalProvider Submit, depth or NGX dependencies.
#include "amd_of_producer.hpp"
#include "amd_of_shader.hpp"
#include <FidelityFX/host/ffx_opticalflow.h>
#include <FidelityFX/host/backends/dx12/ffx_dx12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>
#include <array>
#include <mutex>

namespace nr::final_residual {
using Microsoft::WRL::ComPtr;
namespace {
constexpr auto read=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr auto write=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
constexpr auto copy=D3D12_RESOURCE_STATE_COPY_SOURCE;
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
void hr(HRESULT r,const char* s){if(FAILED(r)){char b[256];std::snprintf(b,sizeof(b),"%s: %08lx",s,(unsigned long)r);throw std::runtime_error(b);}}
void barrier(ID3D12GraphicsCommandList* l,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
    if(a==b)return;
    D3D12_RESOURCE_BARRIER t{};t.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    t.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&t);
}
ComPtr<ID3D12Resource> texture(ID3D12Device* d,unsigned w,unsigned h,DXGI_FORMAT f,D3D12_RESOURCE_STATES state){
    D3D12_RESOURCE_DESC r{};r.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;r.Width=w;r.Height=h;
    r.DepthOrArraySize=r.MipLevels=r.SampleDesc.Count=1;r.Format=f;r.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES p{};p.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> out;
    hr(d->CreateCommittedResource(&p,D3D12_HEAP_FLAG_NONE,&r,state,nullptr,IID_PPV_ARGS(&out)),"allocate OF texture");return out;
}
bool same_device(ID3D12DeviceChild* c,ID3D12Device* d){
    if(!c)return false;ComPtr<ID3D12Device> actual;
    return SUCCEEDED(c->GetDevice(IID_PPV_ARGS(&actual)))&&actual.Get()==d;
}
struct Pass {
    ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> pso;ComPtr<ID3D12DescriptorHeap> heap;
    void init(ID3D12Device* d,bool prep){
        D3D12_DESCRIPTOR_RANGE range[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,1}};
        D3D12_ROOT_PARAMETER p[2]{};p[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[0].DescriptorTable={2,range};
        p[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[1].Constants={0,0,12};
        D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=2;desc.pParameters=p;
        ComPtr<ID3DBlob> b,e;hr(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&b,&e),"serialize root");
        hr(d->CreateRootSignature(0,b->GetBufferPointer(),b->GetBufferSize(),IID_PPV_ARGS(&root)),"root");
        D3D_SHADER_MACRO m[]={{"PREP","1"},{"PREP_RGBA","1"},{nullptr,nullptr}};
        const auto r=D3DCompile(amd_conversion_shader,sizeof(amd_conversion_shader)-1,"original-OF-conversion",prep?m:nullptr,nullptr,"CS","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&b,&e);
        if(FAILED(r)&&e)throw std::runtime_error((char*)e->GetBufferPointer());hr(r,"conversion shader");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.CS={b->GetBufferPointer(),b->GetBufferSize()};
        hr(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)),"conversion PSO");
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=2;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"conversion descriptors");
    }
    void bind(ID3D12Device* d,ID3D12Resource* src,ID3D12Resource* dst){
        auto h=heap->GetCPUDescriptorHandleForHeapStart();D3D12_SHADER_RESOURCE_VIEW_DESC s{};
        s.Format=src->GetDesc().Format;s.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;s.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;s.Texture2D.MipLevels=1;
        d->CreateShaderResourceView(src,&s,h);h.ptr+=d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_UNORDERED_ACCESS_VIEW_DESC u{};u.Format=dst->GetDesc().Format;u.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;d->CreateUnorderedAccessView(dst,nullptr,&u,h);
    }
    void run(ID3D12GraphicsCommandList* l,unsigned w,unsigned h,unsigned fw,unsigned fh,bool reset,bool prep,bool hdr=false,float scale=1.f){
        struct {unsigned w,h,fw,fh,grid,reset;float x,y;unsigned hdr;float scale;unsigned pad0,pad1;} c{w,h,fw,fh,8,unsigned(reset),float(w)/fw,float(h)/fh,unsigned(hdr),scale,0,0};
        ID3D12DescriptorHeap* heaps[]={heap.Get()};l->SetDescriptorHeaps(1,heaps);l->SetComputeRootSignature(root.Get());l->SetPipelineState(pso.Get());
        l->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());l->SetComputeRoot32BitConstants(1,12,&c,0);
        l->Dispatch(((prep?fw:w)+7)/8,((prep?fh:h)+7)/8,1);
    }
};
}
struct AmdOfProducer::Impl {
    struct Registry {
        std::mutex lock;
        std::array<Impl*,4> slots{};
        std::array<bool,4> retired{};
    };
    // Deliberately process-lifetime storage: unresolved GPU work is never
    // destroyed by static teardown. The four slots bound that retention.
    static Registry& registry(){static Registry* value=new Registry;return *value;}
    int slot=-1;
    bool retiring=false,destroy_failed=false;
    ComPtr<ID3D12Fence> consumer_done;Id consumer_value=0;
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> alloc;ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> done;
    ComPtr<ID3D12Resource> input,raw,dense,scd,source,scd_readback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT scd_footprint{};
    Pass prep,convert;std::vector<unsigned char> scratch;std::unique_ptr<FfxOpticalflowContext> amd;
    struct BorrowedSlot {Pass preparation;ComPtr<ID3D12Resource> source;ComPtr<ID3D12Fence> fence;Id value=0;};
    std::array<BorrowedSlot,3> borrowed_slots{};
    static_assert(FFX_MAX_QUEUED_FRAMES>=3,"borrowed descriptor slots exceed the pinned SDK queue capacity");
    bool borrowed=false,recording_failed=false;int borrowed_pending=-1;unsigned borrowed_next=0;
    Frame previous{};Plan pending{};FlowReceipt receipt{};
    unsigned w=0,h=0,fw=0,fh=0,age=0;Id value=0,count=0,last_token=0,generation=0,bytes=0;
    bool created=false,ready=false,fault=false,unpublished=false,held=false;
    std::string message="not initialized";
    void init(ID3D12Device* d,ID3D12CommandQueue* q,unsigned width,unsigned height,unsigned percent){
        require(d&&same_device(q,d)&&q->GetDesc().Type==D3D12_COMMAND_LIST_TYPE_DIRECT,"same-device DIRECT queue required");
        require((percent==50||percent==100)&&width&&height&&width<=4096&&height<=4096,"explicit 100/50 and bounded extent required");
        device=d;queue=q;w=width;h=height;fw=percent==100?w:(w+1)/2;fh=percent==100?h:(h+1)/2;
        require(fw>=128&&fh>=128,"OF input below 128 minimum");
        D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL_6_2};D3D12_FEATURE_DATA_D3D12_OPTIONS1 c{};
        hr(d->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&sm,sizeof(sm)),"shader model");
        hr(d->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1,&c,sizeof(c)),"wave capabilities");
        require(sm.HighestShaderModel>=D3D_SHADER_MODEL_6_2&&c.WaveOps,"actual AMD OF requires SM6.2 waves");
        if(!borrowed){
            hr(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)),"allocator");
            hr(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&list)),"list");hr(list->Close(),"initial close");
            hr(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&done)),"fence");
        }
        input=texture(d,fw,fh,DXGI_FORMAT_R8G8B8A8_UNORM,write);
        raw=texture(d,(fw+7)/8,(fh+7)/8,DXGI_FORMAT_R16G16_SINT,write);
        if(!borrowed)dense=texture(d,w,h,DXGI_FORMAT_R32G32_FLOAT,copy);
        scd=texture(d,3,1,DXGI_FORMAT_R32_UINT,write);
        if(!borrowed){
        auto scd_desc=scd->GetDesc();UINT64 bytes=0;d->GetCopyableFootprints(&scd_desc,0,1,0,&scd_footprint,nullptr,nullptr,&bytes);
        D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=bytes;bd.Height=1;
        bd.DepthOrArraySize=bd.MipLevels=bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES bh{};bh.Type=D3D12_HEAP_TYPE_READBACK;
        hr(d->CreateCommittedResource(&bh,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&scd_readback)),"SCD readback");
        prep.init(d,true);convert.init(d,false);convert.bind(d,raw.Get(),dense.Get());
        }else for(auto& slot:borrowed_slots)slot.preparation.init(d,true);
        scratch.resize(ffxGetScratchMemorySizeDX12(FFX_OPTICALFLOW_CONTEXT_COUNT));
        FfxOpticalflowContextDescription desc{};desc.resolution={fw,fh};
        require(ffxGetInterfaceDX12(&desc.backendInterface,ffxGetDeviceDX12(d),scratch.data(),scratch.size(),FFX_OPTICALFLOW_CONTEXT_COUNT)==FFX_OK,"FFX DX12 interface");
        amd=std::make_unique<FfxOpticalflowContext>();
        require(ffxOpticalflowContextCreate(amd.get(),&desc)==FFX_OK,"FFX OF context creation");created=true;
        FfxEffectMemoryUsage usage{};
        require(ffxOpticalflowContextGetGpuMemoryUsage(amd.get(),&usage)==FFX_OK,"FFX memory accounting");
        bytes=usage.totalUsageInBytes;
        for(auto* r:{input.Get(),raw.Get(),dense.Get(),scd.Get(),scd_readback.Get()})if(r){
            const auto rd=r->GetDesc();const auto allocation=d->GetResourceAllocationInfo(0,1,&rd);
            require(allocation.SizeInBytes!=UINT64_MAX,"OF allocation accounting");bytes+=allocation.SizeInBytes;
        }
        ready=true;message="actual AMD OF ready";
    }
    void poison(const char* s){fault=true;ready=false;message=s;}
    bool complete() const {return done&&done->GetCompletedValue()!=UINT64_MAX&&done->GetCompletedValue()>=value;}
    bool consumer_complete() const {
        if(!held)return true;
        if(!consumer_done||!consumer_value)return false;
        const auto v=consumer_done->GetCompletedValue();return v!=UINT64_MAX&&v>=consumer_value;
    }
    bool quarantined() const {
        bool borrowed_bad=recording_failed||borrowed_pending>=0;
        for(const auto& s:borrowed_slots)borrowed_bad|=s.fence&&s.fence->GetCompletedValue()==UINT64_MAX;
        return borrowed_bad||unpublished||destroy_failed||(held&&!consumer_done)||
            (done&&done->GetCompletedValue()==UINT64_MAX)||
            (consumer_done&&consumer_done->GetCompletedValue()==UINT64_MAX);
    }
    bool destroyable() const {
        for(const auto& s:borrowed_slots)if(s.value&&(!s.fence||s.fence->GetCompletedValue()<s.value))return false;
        return !quarantined()&&(!value||complete())&&consumer_complete();
    }
};
AmdOfProducer::AmdOfProducer(ID3D12Device* d,ID3D12CommandQueue* q,unsigned w,unsigned h,unsigned s,bool borrowed):p_(std::make_unique<Impl>()){
    p_->borrowed=borrowed;
    collect_retired();
    {auto& r=Impl::registry();std::lock_guard<std::mutex> guard(r.lock);
        for(unsigned i=0;i<r.slots.size();++i)if(!r.slots[i]){r.slots[i]=p_.get();p_->slot=int(i);break;}}
    if(p_->slot<0){p_->poison("AMD OF context capacity exhausted; pending retirement retained");return;}
    try{p_->init(d,q,w,h,s);}catch(const std::exception& e){p_->poison(e.what());}
}
AmdOfProducer::~AmdOfProducer(){
    if(!p_)return;
    if(p_->slot<0)return; // Capacity refusal happened before any GPU allocation.
    {auto& r=Impl::registry();std::lock_guard<std::mutex> guard(r.lock);
        r.retired[unsigned(p_->slot)]=true;(void)p_.release();} // Registry owns it.
    collect_retired();
}
RetirementStats AmdOfProducer::collect_retired()noexcept{
    auto& r=Impl::registry();std::lock_guard<std::mutex> guard(r.lock);
    RetirementStats stats;
    for(unsigned i=0;i<r.slots.size();++i){auto* p=r.slots[i];if(!p)continue;
        if(!r.retired[i]){++stats.active;continue;}
        if(p->destroyable()){
            if(p->created&&ffxOpticalflowContextDestroy(p->amd.get())!=FFX_OK)p->destroy_failed=true;
            else {delete p;r.slots[i]=nullptr;r.retired[i]=false;continue;}
        }
        if(p->quarantined())++stats.quarantined;else ++stats.pending;
    }
    return stats;
}
bool AmdOfProducer::initialized()const noexcept{return p_->ready&&!p_->fault;}
const char* AmdOfProducer::status()const noexcept{return p_->message.c_str();}
Id AmdOfProducer::dispatch_count()const noexcept{return p_->count;}
Id AmdOfProducer::gpu_allocation_bytes()const noexcept{return p_->bytes;}
bool AmdOfProducer::can_record()const noexcept {
    const auto& p=*p_;
    if(!p.borrowed||p.retiring||p.fault||!p.ready||p.borrowed_pending>=0)return false;
    const auto& slot=p.borrowed_slots[p.borrowed_next];
    if(!slot.value)return true;
    const auto v=slot.fence?slot.fence->GetCompletedValue():0;
    return v!=UINT64_MAX&&v>=slot.value;
}
Submit AmdOfProducer::record(const Frame& f,const Plan& plan,ID3D12GraphicsCommandList* l,
    ID3D12Resource* snapshot,D3D12_RESOURCE_STATES snapshot_state,RecordedFlow& out)noexcept {
    out={};auto& p=*p_;
    if(!p.borrowed||p.retiring||p.fault||!p.ready)return Submit::Faulted;
    if(!can_record())return Submit::Busy;
    if(!f.valid()||!plan.work()||!plan.history_generation||plan.token<=p.last_token||
       f.contract.backend!=Backend::AmdOpticalFlow||f.contract.width!=p.w||f.contract.height!=p.h||
       !same_device(l,p.device.Get())||l->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT||
       !same_device(snapshot,p.device.Get()))return Submit::Rejected;
    if(f.contract.color_domain==kResidualLinearScene&&
       (!std::isfinite(f.source_to_scene)||f.source_to_scene<1.f/1024.f||f.source_to_scene>1024.f))return Submit::Rejected;
    const bool reset=plan.action==Action::ResetBoth;
    if(reset){if(plan.reference.valid()||plan.history_generation<p.generation)return Submit::Rejected;}
    else if(!AmdOwnedSourceContinues(f,p.previous,plan,p.generation))return Submit::Rejected;
    const auto d=snapshot->GetDesc();
    if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.Width<p.w||d.Height<p.h||
       (d.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&d.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT&&
        !(f.contract.color_domain==kResidualLinearScene&&d.Format==DXGI_FORMAT_R11G11B10_FLOAT))||
       d.MipLevels!=1||d.DepthOrArraySize!=1||d.SampleDesc.Count!=1)return Submit::Rejected;
    auto& slot=p.borrowed_slots[p.borrowed_next];
    try {
        slot.source=snapshot;slot.preparation.bind(p.device.Get(),snapshot,p.input.Get());
        // No mutation of an in-flight descriptor set: each recording uses one
        // of three independently fenced sets, below the SDK's queued-frame cap.
        p.borrowed_pending=int(p.borrowed_next);p.recording_failed=true;
        barrier(l,snapshot,snapshot_state,read);
        slot.preparation.run(l,p.w,p.h,p.fw,p.fh,false,true,f.contract.color_domain==kResidualLinearScene,f.source_to_scene);
        barrier(l,snapshot,read,snapshot_state);barrier(l,p.input.Get(),write,read);
        if(p.count){barrier(l,p.raw.Get(),read,write);barrier(l,p.scd.Get(),read,write);}
        FfxOpticalflowDispatchDescription job{};job.commandList=ffxGetCommandListDX12(l);
        job.color=ffxGetResourceDX12(p.input.Get(),ffxGetResourceDescriptionDX12(p.input.Get()),L"Original SDR",FFX_RESOURCE_STATE_COMPUTE_READ);
        job.opticalFlowVector=ffxGetResourceDX12(p.raw.Get(),ffxGetResourceDescriptionDX12(p.raw.Get()),L"OF grid8",FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        job.opticalFlowSCD=ffxGetResourceDX12(p.scd.Get(),ffxGetResourceDescriptionDX12(p.scd.Get()),L"OF SCD",FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        job.reset=reset;job.backbufferTransferFunction=0;job.minMaxLuminance={0,1};
        require(ffxOpticalflowContextDispatch(p.amd.get(),&job)==FFX_OK,"borrowed FFX dispatch failed");
        barrier(l,p.input.Get(),read,write);barrier(l,p.raw.Get(),write,read);barrier(l,p.scd.Get(),write,read);
        ++p.count;p.last_token=plan.token;p.pending=plan;p.generation=plan.history_generation;
        p.previous=f;p.age=reset?0:p.age<UINT_MAX?p.age+1:UINT_MAX;p.recording_failed=false;
        out={f.source,plan.reference,plan.token,plan.history_generation,p.raw.Get(),p.scd.Get(),p.fw,p.fh,p.age,reset};
        return Submit::Recorded; // No Execute, Signal or completion receipt.
    } catch(const std::exception& e){p.poison(e.what());return Submit::Faulted;}
}
bool AmdOfProducer::commit_recording(Id token,ID3D12Fence* fence,Id value)noexcept {
    auto& p=*p_;
    if(!p.borrowed||p.fault||p.borrowed_pending<0||token!=p.last_token||!value||value==UINT64_MAX||
       !same_device(fence,p.device.Get())||fence->GetCompletedValue()==UINT64_MAX)return false;
    auto& slot=p.borrowed_slots[unsigned(p.borrowed_pending)];slot.fence=fence;slot.value=value;
    p.borrowed_pending=-1;p.borrowed_next=(p.borrowed_next+1)%p.borrowed_slots.size();return true;
}
bool AmdOfProducer::cancel_recording(Id token)noexcept {
    auto& p=*p_;
    if(!p.borrowed||p.fault||p.borrowed_pending<0||token!=p.last_token)return false;
    p.borrowed_slots[unsigned(p.borrowed_pending)].source.Reset();p.borrowed_pending=-1;
    // FFX also changed CPU-side barrier/frame tracking while recording. It
    // cannot be rolled back by resetting the application's command list.
    // Retire this context after its earlier real fences, then create afresh.
    p.retiring=true;p.ready=false;p.previous={};p.age=0;
    p.message="cancelled unsubmitted FFX recording; context requires recreation";return true;
}
bool AmdOfProducer::retire_replayed_recording(ID3D12Fence* fence,Id value)noexcept {
    auto& p=*p_;
    if(!p.borrowed||!value||value==UINT64_MAX||!same_device(fence,p.device.Get())||
       fence->GetCompletedValue()==UINT64_MAX)return false;
    // A recorded list was executed again. Its SDK commands have already run;
    // do not reuse the now-inexact CPU/SDK history. The new same-queue fence
    // covers all earlier uses and must replace the stale retirement receipts.
    for(auto& slot:p.borrowed_slots){slot.fence=fence;slot.value=value;}
    p.retiring=true;p.ready=false;
    p.message="replayed AMD recording: retire after the latest real submission";return true;
}
Submit AmdOfProducer::dispatch(const Frame& f,const Plan& plan,ID3D12Resource* snapshot)noexcept{
    auto& p=*p_;if(p.borrowed||p.retiring||p.fault||!p.ready)return Submit::Faulted;
    if(p.previous.source.valid()&&same_stream(f.source,p.previous.source)&&f.source.sequence==p.previous.source.sequence)return Submit::Duplicate;
    if(p.held)return Submit::Busy;
    if(!f.valid()||!plan.work()||!plan.history_generation||plan.token<=p.last_token||f.contract.backend!=Backend::AmdOpticalFlow||
       f.contract.width!=p.w||f.contract.height!=p.h||!same_device(snapshot,p.device.Get()))return Submit::Rejected;
    if(p.previous.source.valid()&&same_stream(f.source,p.previous.source)&&f.source.sequence<p.previous.source.sequence)return Submit::Rejected;
    const bool reset=plan.action==Action::ResetBoth;
    if(reset){if(plan.reference.valid()||plan.history_generation<p.generation)return Submit::Rejected;}
    else if(!AmdOwnedSourceContinues(f,p.previous,plan,p.generation))return Submit::Rejected;
    auto desc=snapshot->GetDesc();
    if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||desc.Width!=p.w||desc.Height!=p.h||desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM||
       desc.MipLevels!=1||desc.DepthOrArraySize!=1||desc.SampleDesc.Count!=1||p.value>=UINT64_MAX-1)return Submit::Rejected;
    try{
        hr(p.alloc->Reset(),"allocator reset");hr(p.list->Reset(p.alloc.Get(),nullptr),"list reset");
        p.source=snapshot;p.prep.bind(p.device.Get(),snapshot,p.input.Get());
        auto* l=p.list.Get();barrier(l,snapshot,copy,read);p.prep.run(l,p.w,p.h,p.fw,p.fh,false,true);barrier(l,snapshot,read,copy);barrier(l,p.input.Get(),write,read);
        if(p.count)barrier(l,p.scd.Get(),copy,write);
        FfxOpticalflowDispatchDescription job{};job.commandList=ffxGetCommandListDX12(l);
        job.color=ffxGetResourceDX12(p.input.Get(),ffxGetResourceDescriptionDX12(p.input.Get()),L"Original SDR",FFX_RESOURCE_STATE_COMPUTE_READ);
        job.opticalFlowVector=ffxGetResourceDX12(p.raw.Get(),ffxGetResourceDescriptionDX12(p.raw.Get()),L"OF grid8",FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        job.opticalFlowSCD=ffxGetResourceDX12(p.scd.Get(),ffxGetResourceDescriptionDX12(p.scd.Get()),L"OF SCD",FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        job.reset=reset;job.backbufferTransferFunction=0;job.minMaxLuminance={0,1};
        require(ffxOpticalflowContextDispatch(p.amd.get(),&job)==FFX_OK,"FFX OF dispatch failed");
        barrier(l,p.input.Get(),read,write);barrier(l,p.raw.Get(),write,read);barrier(l,p.dense.Get(),copy,write);
        p.convert.run(l,p.w,p.h,p.fw,p.fh,reset,false);barrier(l,p.dense.Get(),write,copy);barrier(l,p.raw.Get(),read,write);barrier(l,p.scd.Get(),write,copy);
        D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=p.scd_readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=p.scd_footprint;
        src.pResource=p.scd.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        hr(l->Close(),"dispatch close");ID3D12CommandList* lists[]={l};p.unpublished=true;p.queue->ExecuteCommandLists(1,lists);
        hr(p.queue->Signal(p.done.Get(),p.value+1),"dispatch Signal");++p.value;++p.count;p.unpublished=false;p.held=true;
        p.pending=plan;p.last_token=plan.token;p.generation=plan.history_generation;p.previous=f;p.age=reset?0:p.age<UINT_MAX?p.age+1:UINT_MAX;
        p.receipt={FlowStatus::Pending,Backend::AmdOpticalFlow,plan.history_generation,f.source,plan.reference,reset};return Submit::Submitted;
    }catch(const std::exception& e){p.poison(e.what());return Submit::Faulted;}
}
bool AmdOfProducer::poll(GpuFlow& out)noexcept{
    out={};auto& p=*p_;if(p.retiring||!p.held||p.fault)return false;
    const auto v=p.done->GetCompletedValue();if(v==UINT64_MAX){p.poison("device removed; retain producer resources");return false;}
    auto r=p.receipt;r.status=v>=p.value?FlowStatus::Ready:FlowStatus::Pending;
    unsigned bits=0;float scene=0;r.motion_usable=false;
    if(r.status==FlowStatus::Ready){
        unsigned* data=nullptr;D3D12_RANGE range{0,12};
        if(FAILED(p.scd_readback->Map(0,&range,(void**)&data))){p.poison("SCD readback failed");return false;}
        std::memcpy(&scene,data,sizeof(scene));bits=data[1];D3D12_RANGE written{0,0};p.scd_readback->Unmap(0,&written);
        r.motion_usable=p.age>5&&(bits&15u)==0&&std::isfinite(scene);
    }
    out={r,p.pending.token,p.dense.Get(),p.scd.Get(),p.done.Get(),p.value,p.age,bits,scene};return true;
}
bool AmdOfProducer::release_output(Id token,ID3D12Fence* fence,Id value)noexcept{
    auto& p=*p_;if(p.retiring||p.fault||!p.held||token!=p.pending.token||!value||value==UINT64_MAX||!p.complete()||!same_device(fence,p.device.Get()))return false;
    const auto v=fence->GetCompletedValue();if(v==UINT64_MAX||v<value)return false;
    p.source.Reset();p.held=false;p.pending={};return true;
}
bool AmdOfProducer::begin_retirement(Id token,ID3D12Fence* fence,Id value)noexcept{
    auto& p=*p_;
    if(p.retiring||p.unpublished||!p.held||token!=p.pending.token||!value||value==UINT64_MAX||
       !same_device(fence,p.device.Get())||fence->GetCompletedValue()==UINT64_MAX)return false;
    p.consumer_done=fence;p.consumer_value=value;p.retiring=true;p.ready=false;
    return true;
}
}
