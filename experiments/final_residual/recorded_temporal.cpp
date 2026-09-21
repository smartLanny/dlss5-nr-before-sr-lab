#include "recorded_temporal.hpp"
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
constexpr auto cs=D3D12_RESOURCE_STATE_COPY_SOURCE;
constexpr auto sr=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr auto ua=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
void need(bool b,const char* s){if(!b)throw std::runtime_error(s);}
void checked(HRESULT h,const char* s){need(SUCCEEDED(h),s);}
bool device_is(ID3D12DeviceChild* p,ID3D12Device* d){
    ComPtr<ID3D12Device> actual;return p&&SUCCEEDED(p->GetDevice(IID_PPV_ARGS(&actual)))&&actual.Get()==d;
}
bool reached(ID3D12Fence* f,Id v){if(!v)return true;if(!f)return false;const auto n=f->GetCompletedValue();return n!=UINT64_MAX&&n>=v;}
void state(ID3D12GraphicsCommandList* l,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
    if(a==b)return;D3D12_RESOURCE_BARRIER t{};t.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    t.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&t);
}
bool region(ID3D12Resource* p,ID3D12Device* d,DXGI_FORMAT f,unsigned x,unsigned y,unsigned w,unsigned h){
    if(!device_is(p,d)||!w||!h)return false;const auto a=p->GetDesc();
    return a.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&a.MipLevels==1&&a.DepthOrArraySize==1&&
        a.SampleDesc.Count==1&&a.Format==f&&x<=a.Width&&y<=a.Height&&w<=a.Width-x&&h<=a.Height-y;
}
bool target_format(DXGI_FORMAT f){return f==DXGI_FORMAT_R16G16B16A16_FLOAT||f==DXGI_FORMAT_R8G8B8A8_UNORM||f==DXGI_FORMAT_R11G11B10_FLOAT;}
struct Constants {
    unsigned width,height,accumulate,later;float weight,tolerance;
    unsigned flow_width,flow_height,flow_x,flow_y;float unit_x,unit_y;
    unsigned amd,age,target_x,target_y;
};
static_assert(sizeof(Constants)==16*4);
}
struct RecordedTemporal::Impl {
    struct Registry {std::mutex mutex;std::array<Impl*,4> objects{};std::array<bool,4> retired{};};
    static Registry& registry(){static auto* r=new Registry;return *r;}
    struct Slot {
        ComPtr<ID3D12DescriptorHeap> heap;
        std::array<ComPtr<ID3D12Resource>,3> inputs;
        ComPtr<ID3D12Fence> fence;Id value=0,recording=0;
    };
    int registry_slot=-1,pending_slot=-1;
    unsigned width=0,height=0,next_slot=0,index=1;
    Backend backend=Backend::None;Scope scope=Scope::WholeChain;
    bool ready=false,fault=false,recording_unsafe=false,retiring=false;
    std::string message="not initialized";
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> copy_pso,accumulate_pso;
    std::array<ComPtr<ID3D12Resource>,2> originals,residuals;
    ComPtr<ID3D12Resource> snapshot,first;
    std::array<Slot,3> slots;
    std::unique_ptr<AmdOfProducer> amd;
    HistoryContract history;Plan plan{};Frame pending_frame{},previous{};
    TemporalStats counters{};
    D3D12_RESOURCE_DESC description(){D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width=width;d.Height=height;d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;
        d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;return d;}
    ComPtr<ID3D12Resource> texture(D3D12_RESOURCE_STATES initial=cs){
        auto d=description();D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> r;
        checked(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,initial,nullptr,IID_PPV_ARGS(&r)),"temporal texture allocation");
        return r;
    }
    void init(ID3D12Device* d,ID3D12CommandQueue* q,unsigned w,unsigned h,Backend b,Scope s,Id budget){
        need(d&&device_is(q,d)&&q->GetDesc().Type==D3D12_COMMAND_LIST_TYPE_DIRECT,"temporal same-device DIRECT queue required");
        need(w&&h&&w<=4096&&h<=4096,"temporal bounded active rectangle required");
        need(b==Backend::NativeMotion||b==Backend::AmdOpticalFlow,"temporal backend unavailable");
        need(s==Scope::WholeChain||s==Scope::LaterLayers,"temporal scope unavailable");
        device=d;queue=q;width=w;height=h;backend=b;scope=s;
        auto td=description();const auto one=d->GetResourceAllocationInfo(0,1,&td).SizeInBytes;
        need(one&&one!=UINT64_MAX,"temporal allocation estimate");
        const Id own=one*(scope==Scope::LaterLayers?6:5);
        // Conservative creation allowance for the pinned FFX pyramid/context.
        // Actual SDK usage is checked as well, before any GPU recording.
        const Id sdk_allowance=backend==Backend::AmdOpticalFlow?Id(w)*h*64+16*1024*1024:0;
        need(own<=budget&&sdk_allowance<=budget-own,"temporal creation budget exhausted");
        if(backend==Backend::AmdOpticalFlow){
            amd=std::make_unique<AmdOfProducer>(d,q,w,h,100,true);need(amd->initialized(),amd->status());
            need(amd->gpu_allocation_bytes()<=budget-own,"actual FFX allocation exceeds temporal budget");
        }
        for(auto& r:originals)r=texture();for(auto& r:residuals)r=texture();snapshot=texture();
        if(scope==Scope::LaterLayers)first=texture(ua);
        counters.bytes=own+(amd?amd->gpu_allocation_bytes():0);
        D3D12_DESCRIPTOR_RANGE ranges[]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,9,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,3,0,0,9}};
        D3D12_ROOT_PARAMETER parameters[2]{};parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[0].DescriptorTable={2,ranges};parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[1].Constants={0,0,16};
        D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;sampler.MaxLOD=D3D12_FLOAT32_MAX;
        D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=2;desc.pParameters=parameters;desc.NumStaticSamplers=1;desc.pStaticSamplers=&sampler;
        ComPtr<ID3DBlob> code,error;checked(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&code,&error),"temporal root serialization");
        checked(d->CreateRootSignature(0,code->GetBufferPointer(),code->GetBufferSize(),IID_PPV_ARGS(&root)),"temporal root");
        const D3D_SHADER_MACRO macros[]={{"RECORDING","1"},{nullptr,nullptr}};
        auto pipeline=[&](const char* entry,ComPtr<ID3D12PipelineState>& pso){
            const auto result=D3DCompile(residual_accumulator_shader,sizeof(residual_accumulator_shader)-1,
                "recorded-temporal",macros,nullptr,entry,"cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error);
            if(FAILED(result)&&error)throw std::runtime_error(static_cast<const char*>(error->GetBufferPointer()));checked(result,"temporal shader");
            D3D12_COMPUTE_PIPELINE_STATE_DESC p{};p.pRootSignature=root.Get();p.CS={code->GetBufferPointer(),code->GetBufferSize()};
            checked(d->CreateComputePipelineState(&p,IID_PPV_ARGS(&pso)),"temporal pipeline");
        };
        pipeline("CopyCandidate",copy_pso);pipeline("CS",accumulate_pso);
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=12;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        for(auto& slot:slots)checked(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&slot.heap)),"temporal descriptor set");
        ready=true;message="ready; no temporal work recorded";
    }
    bool quarantined() const {
        if(recording_unsafe||pending_slot>=0)return true;
        for(const auto& s:slots)if(s.fence&&s.fence->GetCompletedValue()==UINT64_MAX)return true;
        return false;
    }
    bool destroyable() const {if(quarantined())return false;for(const auto& s:slots)if(s.recording||!reached(s.fence.Get(),s.value))return false;return true;}
    void fail(const char* why){fault=true;ready=false;counters.faulted=true;message=why;}
    void bind(const RecordedInputs& in,const RecordedFlow& flow,unsigned next){
        auto cpu=slots[next_slot].heap->GetCPUDescriptorHandleForHeapStart();const auto step=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        auto srv=[&](ID3D12Resource* r,DXGI_FORMAT format){D3D12_SHADER_RESOURCE_VIEW_DESC v{};v.Format=format;
            v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;v.Texture2D.MipLevels=1;
            device->CreateShaderResourceView(r,&v,cpu);cpu.ptr+=step;};
        const auto fp=DXGI_FORMAT_R16G16B16A16_FLOAT;
        srv(in.original,fp);srv(snapshot.Get(),fp);
        srv(backend==Backend::NativeMotion?in.native.resource:nullptr,backend==Backend::NativeMotion?in.native.format:DXGI_FORMAT_R32G32_FLOAT);
        srv(originals[index].Get(),fp);srv(residuals[index].Get(),fp);srv(first?first.Get():in.original,fp);
        srv(flow.grid,DXGI_FORMAT_R16G16_SINT);srv(flow.scene_data,DXGI_FORMAT_R32_UINT);srv(in.candidate,in.candidate_format);
        auto uav=[&](ID3D12Resource* r,DXGI_FORMAT format){D3D12_UNORDERED_ACCESS_VIEW_DESC v{};v.Format=format;v.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
            device->CreateUnorderedAccessView(r,nullptr,&v,cpu);cpu.ptr+=step;};
        uav(residuals[next].Get(),fp);uav(in.candidate,in.candidate_format);uav(snapshot.Get(),fp);
    }
};
RecordedTemporal::RecordedTemporal(ID3D12Device* d,ID3D12CommandQueue* q,unsigned w,unsigned h,Backend b,Scope s,Id budget):p_(std::make_unique<Impl>()){
    collect_retired();auto& r=Impl::registry();
    {std::lock_guard<std::mutex> lock(r.mutex);for(unsigned i=0;i<r.objects.size();++i)if(!r.objects[i]){r.objects[i]=p_.get();p_->registry_slot=int(i);break;}}
    if(p_->registry_slot<0){p_->fail("temporal context capacity exhausted");return;}
    try{p_->init(d,q,w,h,b,s,budget);}catch(const std::exception& e){p_->fail(e.what());}
}
RecordedTemporal::~RecordedTemporal(){if(!p_||p_->registry_slot<0)return;auto& r=Impl::registry();
    {std::lock_guard<std::mutex> lock(r.mutex);r.retired[unsigned(p_->registry_slot)]=true;(void)p_.release();}collect_retired();}
RetirementStats RecordedTemporal::collect_retired() noexcept {
    auto& r=Impl::registry();std::lock_guard<std::mutex> lock(r.mutex);RetirementStats stats;
    for(unsigned i=0;i<r.objects.size();++i){auto* p=r.objects[i];if(!p)continue;
        if(!r.retired[i]){++stats.active;continue;}if(p->destroyable()){delete p;r.objects[i]=nullptr;r.retired[i]=false;continue;}
        if(p->quarantined())++stats.quarantined;else ++stats.pending;
    }AmdOfProducer::collect_retired();return stats;
}
bool RecordedTemporal::initialized()const noexcept{return p_->ready&&!p_->fault;}
const char* RecordedTemporal::status()const noexcept{return p_->message.c_str();}
TemporalStats RecordedTemporal::stats()const noexcept{return p_->counters;}
ID3D12Resource* RecordedTemporal::anchor()const noexcept{return p_->first.Get();}
bool RecordedTemporal::can_record()const noexcept {
    const auto& p=*p_;return p.ready&&!p.fault&&!p.retiring&&p.pending_slot<0&&
        !p.slots[p.next_slot].recording&&reached(p.slots[p.next_slot].fence.Get(),p.slots[p.next_slot].value)&&(!p.amd||p.amd->can_record());
}
RecordResult RecordedTemporal::record(ID3D12GraphicsCommandList* l,const RecordedInputs& in)noexcept {
    auto& p=*p_;if(!in.settings.enabled)return RecordResult::Disabled;
    if(p.fault||!p.ready)return RecordResult::Faulted;
    if(p.previous.source.valid()&&in.frame.source==p.previous.source)return RecordResult::Duplicate;
    if(!can_record()){p.message="temporal recording or GPU resource slot is still occupied";return RecordResult::Busy;}
    const auto& frame=in.frame;const bool later=p.scope==Scope::LaterLayers;
    if(!in.settings.valid()||in.settings.scope!=p.scope||!frame.valid()||frame.contract.backend!=p.backend||frame.contract.scope!=p.scope||
        frame.contract.color_domain!=kResidualSdrCodeValues||frame.contract.width!=p.width||frame.contract.height!=p.height||
        in.layers<1||in.layers>5||(later&&(in.layers<2||!(in.anchor_source==frame.source)))||
        !device_is(l,p.device.Get())||l->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT||
        !region(in.original,p.device.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,0,0,p.width,p.height)||
        in.original->GetDesc().Width!=p.width||in.original->GetDesc().Height!=p.height||
        !target_format(in.candidate_format)||!region(in.candidate,p.device.Get(),in.candidate_format,in.candidate_x,in.candidate_y,p.width,p.height)||
        !(in.candidate->GetDesc().Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)||in.candidate==in.original){
        p.message="temporal source, scope, SDR format or active rectangle unavailable";return RecordResult::Unavailable;
    }
    if(p.backend==Backend::NativeMotion){const auto& m=in.native;
        if(!(m.source==frame.source)||!m.phase_proven||!std::isfinite(m.scale_x)||!std::isfinite(m.scale_y)||m.scale_x==0||m.scale_y==0||
            (m.format!=DXGI_FORMAT_R16G16_FLOAT&&m.format!=DXGI_FORMAT_R32G32_FLOAT)||
            !region(m.resource,p.device.Get(),m.format,m.x,m.y,m.width,m.height)||m.resource==in.original||m.resource==in.candidate){
            p.message="native motion identity, phase, units or readable format unavailable";return RecordResult::Unavailable;
        }
    }
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};support.Format=in.candidate_format;
    if(FAILED(p.device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support)))||
        !(support.Support1&D3D12_FORMAT_SUPPORT1_SHADER_LOAD)||!(support.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE)){
        p.message="temporal candidate typed read/write unavailable";return RecordResult::Unavailable;
    }
    Frame next_frame=frame;
    const double delta=p.previous.valid()&&frame.source_time>p.previous.source_time?double(frame.source_time-p.previous.source_time)/1000.:0;
    if(delta>in.settings.maximum_gap_ms)next_frame.scene_reset=true;
    p.plan=p.history.prepare(next_frame);
    if(!p.plan.work()){p.message="temporal source identity did not admit a new frame";return p.plan.action==Action::Duplicate?RecordResult::Duplicate:RecordResult::Unavailable;}
    try {
        RecordedFlow flow{};
        if(p.amd){const auto result=p.amd->record(next_frame,p.plan,l,in.original,in.original_state,flow);
            if(result!=Submit::Recorded){p.history.cancel_before_flow(p.plan.token);p.plan={};
                if(result==Submit::Faulted){p.recording_unsafe=true;p.fail(p.amd->status());return RecordResult::Faulted;}
                p.message=p.amd->status();return RecordResult::Unavailable;
            }
            p.recording_unsafe=true;
        }
        need(p.history.observe_ordered_recording(p.plan.token,{p.backend,p.plan.history_generation,frame.source,p.plan.reference,p.plan.action==Action::ResetBoth}),"temporal recording contract rejected");
        const unsigned next=1-p.index;p.bind(in,flow,next);
        auto& slot=p.slots[p.next_slot];slot.inputs={in.original,in.candidate,in.native.resource};
        p.pending_slot=int(p.next_slot);p.recording_unsafe=true;
        const float weight=HistoryWeight(in.settings,delta);
        Constants c{p.width,p.height,unsigned(p.plan.action==Action::Continue&&weight>0),unsigned(later),weight,in.settings.color_tolerance,
            p.amd?flow.flow_width:in.native.width,p.amd?flow.flow_height:in.native.height,
            p.amd?0:in.native.x,p.amd?0:in.native.y,p.amd?float(p.width)/flow.flow_width:in.native.scale_x,
            p.amd?float(p.height)/flow.flow_height:in.native.scale_y,unsigned(bool(p.amd)),flow.frames_since_reset,in.candidate_x,in.candidate_y};
        ID3D12DescriptorHeap* heaps[]={slot.heap.Get()};l->SetDescriptorHeaps(1,heaps);l->SetComputeRootSignature(p.root.Get());
        l->SetComputeRootDescriptorTable(0,slot.heap->GetGPUDescriptorHandleForHeapStart());l->SetComputeRoot32BitConstants(1,16,&c,0);
        state(l,in.candidate,in.candidate_state,sr);state(l,p.snapshot.Get(),cs,ua);
        l->SetPipelineState(p.copy_pso.Get());l->Dispatch((p.width+7)/8,(p.height+7)/8,1);
        state(l,p.snapshot.Get(),ua,sr);state(l,in.candidate,sr,ua);state(l,in.original,in.original_state,sr);
        state(l,p.originals[p.index].Get(),cs,sr);state(l,p.residuals[p.index].Get(),cs,sr);state(l,p.residuals[next].Get(),cs,ua);
        if(later)state(l,p.first.Get(),ua,sr);if(!p.amd)state(l,in.native.resource,in.native.state,sr);
        l->SetPipelineState(p.accumulate_pso.Get());l->Dispatch((p.width+7)/8,(p.height+7)/8,1);
        state(l,p.snapshot.Get(),sr,cs);state(l,in.candidate,ua,in.candidate_state);
        if(in.candidate_state==ua){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=in.candidate;l->ResourceBarrier(1,&b);}
        state(l,p.originals[p.index].Get(),sr,cs);state(l,p.residuals[p.index].Get(),sr,cs);state(l,p.residuals[next].Get(),ua,cs);
        if(later)state(l,p.first.Get(),sr,ua);if(!p.amd)state(l,in.native.resource,sr,in.native.state);
        state(l,in.original,sr,cs);state(l,p.originals[next].Get(),cs,D3D12_RESOURCE_STATE_COPY_DEST);
        l->CopyResource(p.originals[next].Get(),in.original);
        state(l,p.originals[next].Get(),D3D12_RESOURCE_STATE_COPY_DEST,cs);state(l,in.original,cs,in.original_state);
        p.pending_frame=next_frame;p.index=next;p.recording_unsafe=false;
        ++p.counters.recorded;if(!c.accumulate)++p.counters.seeded;p.counters.weight=weight;p.counters.pending_token=p.plan.token;
        p.message="temporal work recorded; awaiting actual owner submission";return RecordResult::Recorded;
    }catch(const std::exception& e){p.fail(e.what());return RecordResult::Faulted;}
}
bool RecordedTemporal::submitted(ID3D12Fence* fence,Id value,Id recording)noexcept {
    auto& p=*p_;if(p.fault||p.pending_slot<0||!value||value==UINT64_MAX||!device_is(fence,p.device.Get())||fence->GetCompletedValue()==UINT64_MAX)return false;
    if(p.amd&&!p.amd->commit_recording(p.plan.token,fence,value)){p.recording_unsafe=true;p.fail("AMD actual submission receipt rejected");return false;}
    if(p.history.finish(p.plan.token,p.plan.history_generation,ResidualStatus::SubmittedAdopted)!=Completion::Adopted){
        p.recording_unsafe=true;p.fail("temporal actual adoption rejected");return false;
    }
    auto& slot=p.slots[unsigned(p.pending_slot)];slot.fence=fence;slot.value=value;slot.recording=recording;
    p.previous=p.pending_frame;p.pending_slot=-1;p.next_slot=(p.next_slot+1)%p.slots.size();p.plan={};
    p.counters.pending_token=0;++p.counters.submitted;p.message="temporal result submitted and adopted; completion tracked separately";return true;
}
void RecordedTemporal::end_recording(Id recording)noexcept {
    if(!recording)return;for(auto& slot:p_->slots)if(slot.recording==recording)slot.recording=0;
}
bool RecordedTemporal::cancelled_unsubmitted()noexcept {
    auto& p=*p_;if(p.fault||p.pending_slot<0)return false;
    if(p.amd&&!p.amd->cancel_recording(p.plan.token))return false;
    if(p.history.finish(p.plan.token,p.plan.history_generation,ResidualStatus::NotSubmitted)!=Completion::ResetRequired)return false;
    p.slots[unsigned(p.pending_slot)].inputs={};p.pending_slot=-1;p.retiring=true;p.ready=false;
    p.counters.pending_token=0;++p.counters.cancelled;p.message="unsubmitted recording cancelled; retire and recreate temporal context";return true;
}
}
