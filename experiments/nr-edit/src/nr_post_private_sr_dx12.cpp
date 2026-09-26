#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include "nr_post_private_sr_dx12.hpp"
#include <cmath>
#include <new>

namespace nr_post_private_sr_dx12 {
using nr_edit_dx12::View;
using nr_private_sr::Recording;
using nr_private_sr::Source;
namespace {
View Owned(ID3D12Resource* p,unsigned format) {
    return {p,format,0,D3D12_RESOURCE_STATE_COMMON,true};
}
void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* p,unsigned from,unsigned to) {
    if(from==to)return;
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={p,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        static_cast<D3D12_RESOURCE_STATES>(from),static_cast<D3D12_RESOURCE_STATES>(to)};
    list->ResourceBarrier(1,&b);
}
bool Contains(nr_edit_dx12::Kernels* k,const View& v,nr_post_residual_dx12::Rect r,unsigned format) {
    if(!v.resource||!v.state_known||v.mip||v.format!=format||!r.width||!r.height)return false;
    const auto d=v.resource->GetDesc();
    return d.Width<=16384&&d.Height<=16384&&r.x<=d.Width&&r.y<=d.Height&&
        r.width<=d.Width-r.x&&r.height<=d.Height-r.y&&
        nr_edit_dx12::ValidateRead(k,v,static_cast<unsigned>(d.Width),d.Height);
}
bool Guide(nr_edit_dx12::Kernels* k,const View& v,unsigned w,unsigned h,bool depth) {
    if(!v.resource||!v.state_known||v.mip||
       (depth?(v.format!=41&&v.format!=54):(v.format!=16&&v.format!=34)))return false;
    const auto d=v.resource->GetDesc();
    return d.Width==w&&d.Height==h&&nr_edit_dx12::ValidateRead(k,v,w,h);
}
}
class Context {
public:
    struct Slot {
        ID3D12Resource *carrier=nullptr,*tail=nullptr,*upscaled=nullptr;
        Recording recording{};
        ID3D12Fence* fence=nullptr;
        nr_private_sr::Fence completion{};
        bool sr_recorded=false;
    };
    std::array<Slot,3> slots{};
    nr_private_sr::Api api{};
    nr_private_sr::Context sr;
    nr_private_sr::Shape shape{};
    nr_edit_dx12::Kernels* kernels=nullptr;
    Source last_attempt{};
    std::uint64_t last_view=0,last_configuration=0,last_encoding=0,plane_bytes=0;
    float last_range=0;
    bool poisoned=false,retiring=false;
    void Poison() {
        poisoned=true;sr.MarkUncertain();nr_edit_dx12::MarkUncertain(kernels);
    }
    void FreePlanes() {
        for(auto& s:slots)for(auto** p:{&s.carrier,&s.tail,&s.upscaled}) {
            if(*p)(*p)->Release();*p=nullptr;
        }
    }
};
Context* Create(ID3D12Device* device,nr_private_sr::Api api,nr_private_sr::Shape shape,
    const std::array<nr_edit_dx12::Bytecode,static_cast<unsigned>(nr_edit_dx12::Kernel::Count)>& code,
    nr_edit_dx12::SameDevice same,std::uint64_t budget,std::uint64_t* bytes) {
    if(bytes)*bytes=0;
    // Normalized N-grid guides require the low-resolution-MV flag. Autoexposure,
    // jittered MV and unreviewed flags are not valid for a bounded edit signal.
    if(!device||device->GetNodeCount()!=1||!bytes||!api.Valid()||!api.recording_released||!same||!shape.Valid()||
       (shape.create_flags&2)==0||(shape.create_flags&~unsigned(1|2|8))!=0||
       std::uint64_t(shape.render_width)*4<shape.output_width||
       std::uint64_t(shape.render_height)*4<shape.output_height)return nullptr;
    auto* c=new(std::nothrow)Context;
    if(!c)return nullptr;
    c->api=api;c->shape=shape;
    c->kernels=nr_edit_dx12::Create(device,shape.device,shape.queue,code,same);
    if(!c->kernels){delete c;return nullptr;}
    nr_edit_dx12::SetRetirementGuard(c->kernels,api.recording_released,api.retirement_user);
    for(auto& s:c->slots) {
        ID3D12Resource** planes[]={&s.carrier,&s.tail,&s.upscaled};
        for(unsigned i=0;i<3;++i) {
            std::uint64_t n=0;
            *planes[i]=nr_edit_dx12::CreatePlane(device,
                i==2?shape.output_width:shape.render_width,i==2?shape.output_height:shape.render_height,
                i==1?2u:10u,budget-c->plane_bytes,&n);
            if(!*planes[i]) {c->FreePlanes();nr_edit_dx12::DestroyAfterRetirement(c->kernels);delete c;return nullptr;}
            c->plane_bytes+=n;
        }
    }
    // Return an identifiable quarantined owner on opaque allocation failure.
    // Never drop the only owner of possibly live vendor state.
    if(!c->sr.Initialize(api,shape))c->Poison();
    *bytes=c->plane_bytes;
    return c;
}
bool Valid(const Context& c,const Input& i) {
    const auto& s=c.shape;const auto& p=i.pair;
    if(i.native_result!=nr_private_sr::success||!i.target_contains_candidate||
       !i.ordered_queue_verified||!i.post_sr_phase_verified||!i.normalized_guide_units_verified||
       !i.view||!i.configuration||!i.encoding_generation||!i.recording||
       i.recording.device!=s.device||i.recording.queue!=s.queue||!p.base_source||
       !(p.base_source==p.candidate_source)||!(p.base_source==i.guide_source)||
       p.domain!=nr_post_residual_dx12::Domain::SdrEncodedFp16||!p.same_encoding_verified||
       p.work_width!=s.render_width||p.work_height!=s.render_height||
       p.base_rect.width!=s.output_width||p.base_rect.height!=s.output_height||
       p.candidate_rect.width!=s.output_width||p.candidate_rect.height!=s.output_height||
       i.target_rect.width!=s.output_width||i.target_rect.height!=s.output_height||
       !std::isfinite(p.carrier_range)||p.carrier_range<=1e-6f||p.carrier_range>1e6f||
       !std::isfinite(i.motion_scale_x)||!std::isfinite(i.motion_scale_y)||
       i.motion_scale_x==0||i.motion_scale_y==0||p.base.resource==p.candidate.resource||
       i.target.resource==p.base.resource||i.depth.resource==i.motion.resource)return false;
    if(i.target.resource==p.candidate.resource&&
       (i.target_rect.x!=p.candidate_rect.x||i.target_rect.y!=p.candidate_rect.y))return false;
    for(auto* g:{i.depth.resource,i.motion.resource})
        if(g==p.base.resource||g==p.candidate.resource||g==i.target.resource)return false;
    return Contains(c.kernels,p.base,p.base_rect,10)&&Contains(c.kernels,p.candidate,p.candidate_rect,10)&&
        Contains(c.kernels,i.target,i.target_rect,10)&&
        Guide(c.kernels,i.depth,s.render_width,s.render_height,true)&&
        Guide(c.kernels,i.motion,s.render_width,s.render_height,false);
}
bool Collect(Context* c) {
    if(!c)return true;
    if(c->poisoned||!c->sr.Collect()||!nr_edit_dx12::Collect(c->kernels)) {c->Poison();return false;}
    for(auto& s:c->slots) {
        if(!s.fence)continue;
        const auto value=s.fence->GetCompletedValue();
        if(value==UINT64_MAX){c->Poison();return false;}
        if(value>=s.completion.value&&c->api.recording_released(c->api.retirement_user,s.recording)) {
            s.fence->Release();s.fence=nullptr;s.recording={};s.completion={};s.sr_recorded=false;
        }
    }
    return true;
}
Result Record(Context* c,ID3D12GraphicsCommandList* list,const Input& i) {
    if(!c||c->poisoned||c->sr.Poisoned())return {Status::Poisoned};
    if(!list||list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT||
       reinterpret_cast<std::uint64_t>(list)!=i.recording.list||!Valid(*c,i))return {};
    if(c->retiring)return {Status::Busy};
    if(!Collect(c))return {Status::Poisoned};
    if(c->last_attempt.stream==i.pair.base_source.stream&&c->last_attempt.epoch==i.pair.base_source.epoch&&
       i.pair.base_source.sequence<=c->last_attempt.sequence)return {};
    Context::Slot* slot=nullptr;unsigned index=0;
    for(unsigned n=0;n<c->slots.size();++n) {
        // One source per command-list recording. A replay/duplicate submission
        // must not be mistaken for another frame or update the same history twice.
        if(c->slots[n].recording==i.recording)return {Status::Busy};
        if(!slot&&!c->slots[n].recording){slot=&c->slots[n];index=n;}
    }
    if(!slot){c->sr.ResetHistory();return {Status::Busy};}
    if(c->last_view!=i.view||c->last_configuration!=i.configuration||
       c->last_encoding!=i.encoding_generation||c->last_range!=i.pair.carrier_range)c->sr.ResetHistory();
    const auto encoded=nr_post_residual_dx12::Encode(c->kernels,list,i.recording,i.pair,
        Owned(slot->carrier,10),Owned(slot->tail,2));
    if(!encoded)return {nr_edit_dx12::Poisoned(c->kernels)?Status::Poisoned:Status::Busy};
    slot->recording=i.recording;c->last_attempt=i.pair.base_source;
    c->last_view=i.view;c->last_configuration=i.configuration;c->last_encoding=i.encoding_generation;
    c->last_range=i.pair.carrier_range;
    nr_private_sr::Frame frame{};
    frame.recording=i.recording;frame.source=i.pair.base_source;frame.motion_reference=i.motion_reference;
    frame.color=slot->carrier;frame.output=slot->upscaled;frame.depth=i.depth.resource;frame.motion=i.motion.resource;
    frame.jitter_x=frame.jitter_y=0;frame.mv_x=i.motion_scale_x;frame.mv_y=i.motion_scale_y;
    frame.reset=i.reset;frame.resource_contract_verified=true;
    Transition(list,slot->carrier,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(list,slot->upscaled,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(list,i.depth.resource,i.depth.state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(list,i.motion.resource,i.motion.state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    const auto sr=c->sr.Record(list,frame,c->shape);
    slot->sr_recorded=sr.recorded;
    if(c->sr.Poisoned()) {c->Poison();return {Status::Poisoned,true,false,sr.result,index};}
    if(sr.evaluated) {
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=slot->upscaled;
        list->ResourceBarrier(1,&b);
    }
    Transition(list,slot->carrier,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
    Transition(list,slot->upscaled,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON);
    Transition(list,i.depth.resource,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,i.depth.state);
    Transition(list,i.motion.resource,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,i.motion.state);
    if(!sr.evaluated)return {Status::WaitingForSr,true,false,sr.result,index};
    nr_post_residual_dx12::Reconstruction r{};
    r.carrier=Owned(slot->upscaled,10);r.target=i.target;r.target_rect=i.target_rect;
    r.recording=i.recording;r.source=i.pair.base_source;r.encoded_token=encoded.dispatch.token;
    r.succeeded=true;r.target_contains_candidate=true;
    const auto composed=nr_post_residual_dx12::Compose(c->kernels,list,encoded,r);
    if(!composed.recorded) {
        if(nr_edit_dx12::Poisoned(c->kernels)){c->Poison();return {Status::Poisoned,true,false,sr.result,index};}
        c->sr.ResetHistory();return {Status::WaitingForSr,true,false,sr.result,index};
    }
    return {Status::Composed,true,true,sr.result,index};
}
bool Submitted(Context* c,Recording r,ID3D12Fence* fence,nr_private_sr::Fence proof,bool success) {
    if(!c||c->poisoned)return false;
    Context::Slot* slot=nullptr;
    for(auto& s:c->slots)if(s.recording==r&&r){if(slot){c->Poison();return false;}slot=&s;}
    if(!slot)return false;
    if(!success||!fence||!proof||proof.queue!=c->shape.queue||proof.queue!=r.queue||
       proof.object!=reinterpret_cast<std::uint64_t>(fence)||slot->fence) {c->Poison();return false;}
    if(!nr_edit_dx12::Submitted(c->kernels,r,fence,proof,true)||
       (slot->sr_recorded&&!c->sr.Submitted(r,proof,true))) {c->Poison();return false;}
    fence->AddRef();slot->fence=fence;slot->completion=proof;return true;
}
void Discarded(Context* c,Recording r) {
    if(c)for(const auto& s:c->slots)if(r&&s.recording==r){c->Poison();return;}
}
void Cancel(Context* c) {if(c){c->retiring=true;c->sr.ResetHistory();}}
bool Poisoned(const Context* c) {return c&&(c->poisoned||c->sr.Poisoned()||nr_edit_dx12::Poisoned(c->kernels));}
bool ReadyToRetire(Context* c) {
    if(!c)return true;
    if(!Collect(c))return false;
    for(const auto& s:c->slots)if(s.recording||s.fence)return false;
    return c->sr.ReadyToRetire()&&nr_edit_dx12::ReadyToRetire(c->kernels);
}
bool Retire(Context*& c) {
    if(!c)return true;
    c->retiring=true;
    if(!ReadyToRetire(c))return false;
    if(!c->sr.Retire()||!nr_edit_dx12::DestroyAfterRetirement(c->kernels)){c->Poison();return false;}
    c->FreePlanes();delete c;c=nullptr;return true;
}
}
