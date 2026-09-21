#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include "nr_deferred_dx12.hpp"
#include <cmath>
#include <new>
namespace nr_deferred_dx12 {
using namespace nr_edit_dx12;
namespace {
void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* r,unsigned a,unsigned b){
    if(a==b)return;D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    v.Transition.pResource=r;v.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    v.Transition.StateBefore=static_cast<D3D12_RESOURCE_STATES>(a);v.Transition.StateAfter=static_cast<D3D12_RESOURCE_STATES>(b);list->ResourceBarrier(1,&v);
}
void Uav(ID3D12GraphicsCommandList* list,ID3D12Resource* r){D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;v.UAV.pResource=r;list->ResourceBarrier(1,&v);}
bool Plane(const View& v,unsigned w,unsigned h){if(!v.resource||!v.state_known||v.mip)return false;
    const auto d=v.resource->GetDesc();return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.Width==w&&d.Height==h&&
        d.MipLevels==1&&d.DepthOrArraySize==1&&d.SampleDesc.Count==1&&v.format==10&&
        (d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT||d.Format==DXGI_FORMAT_R16G16B16A16_TYPELESS);}
View Owned(ID3D12Resource* p,unsigned format){return {p,format,0,D3D12_RESOURCE_STATE_COMMON,true};}
}
class Context {
public:
    nr_private_sr::Context sr;nr_private_sr::Shape shape{};Kernels* kernels=nullptr;
    std::array<ID3D12Resource*,4> planes{};
    nr_private_sr::Recording awaiting_submission{};Input pending{};nr_private_sr::Source last_attempt{};
    bool armed=false,poisoned=false,retiring=false;std::uint64_t bytes=0;
    float carrier_range=0;std::uint64_t history_config=0,history_domain=0,history_view=0;
    void FreePlanes(){for(auto*& p:planes){if(p)p->Release();p=nullptr;}}
};
Context* Create(ID3D12Device* device,nr_private_sr::Api api,nr_private_sr::Shape shape,
        const std::array<Bytecode,5>& binaries,SameDevice same,std::uint64_t budget,std::uint64_t* reserved){
    if(reserved)*reserved=0;if(!device||!reserved||!api.Valid()||!shape.Valid())return nullptr;
    auto* c=new(std::nothrow)Context;if(!c)return nullptr;c->shape=shape;
    c->kernels=nr_edit_dx12::Create(device,shape.device,shape.queue,binaries,same);
    if(!c->kernels){delete c;return nullptr;}
    for(unsigned i=0;i<4;i++){std::uint64_t bytes=0;
        c->planes[i]=CreatePlane(device,i<2?shape.render_width:shape.output_width,i<2?shape.render_height:shape.output_height,
            i==1?2u:10u,budget-c->bytes,&bytes);
        if(!c->planes[i]){c->FreePlanes();DestroyAfterRetirement(c->kernels);delete c;return nullptr;}c->bytes+=bytes;}
    if(!c->sr.Initialize(api,shape)){c->poisoned=true;*reserved=c->bytes;return c;}
    *reserved=c->bytes;return c;
}
bool Valid(const Context& c,const Input& i){const auto& s=c.shape;const auto& k=i.constants;
    return i.sr.recording&&i.sr.recording.device==s.device&&i.sr.recording.queue==s.queue&&i.sr.source&&
        i.scene_linear_verified&&i.zero_origin_rects_verified&&i.ordered_queue_verified&&i.view&&i.config_generation&&i.color_domain&&
        Plane(i.original,s.render_width,s.render_height)&&Plane(i.edited,s.render_width,s.render_height)&&Plane(i.clean_output,s.output_width,s.output_height)&&
        i.depth.resource&&i.motion.resource&&i.depth.state_known&&i.motion.state_known&&i.depth.mip==0&&i.motion.mip==0&&i.depth.resource!=i.motion.resource&&
        (i.depth.format==41||i.depth.format==54)&&(i.motion.format==16||i.motion.format==34)&&
        ValidateRead(c.kernels,i.depth,s.render_width,s.render_height)&&ValidateRead(c.kernels,i.motion,s.render_width,s.render_height)&&
        i.depth.resource!=i.original.resource&&i.depth.resource!=i.edited.resource&&i.depth.resource!=i.clean_output.resource&&
        i.motion.resource!=i.original.resource&&i.motion.resource!=i.edited.resource&&i.motion.resource!=i.clean_output.resource&&
        (i.clean_output.resource->GetDesc().Format==DXGI_FORMAT_R16G16B16A16_FLOAT||i.clean_output.resource->GetDesc().Format==DXGI_FORMAT_R16G16B16A16_TYPELESS)&&
        k.render_size[0]==s.render_width&&k.render_size[1]==s.render_height&&k.output_size[0]==s.output_width&&k.output_size[1]==s.output_height&&
        std::isfinite(k.input_pre_exposure)&&std::isfinite(k.output_pre_exposure)&&k.input_pre_exposure>1e-6f&&k.output_pre_exposure>1e-6f&&
        std::isfinite(k.carrier_range)&&k.carrier_range>1e-6f&&k.carrier_range<=1e6f&&k.frame_pair_valid&&!k.force_identity;
}
bool Same(const Input& a,const Input& b){return a.sr.recording==b.sr.recording&&a.sr.source==b.sr.source&&
    a.view==b.view&&a.config_generation==b.config_generation&&a.color_domain==b.color_domain&&
    a.clean_output.resource==b.clean_output.resource&&a.original.resource==b.original.resource&&a.edited.resource==b.edited.resource&&
    a.depth.resource==b.depth.resource&&a.motion.resource==b.motion.resource&&a.constants.carrier_range==b.constants.carrier_range&&
    a.constants.input_pre_exposure==b.constants.input_pre_exposure&&a.constants.output_pre_exposure==b.constants.output_pre_exposure&&
    a.depth.format==b.depth.format&&a.motion.format==b.motion.format&&a.sr.motion_reference==b.sr.motion_reference&&
    a.sr.jitter_x==b.sr.jitter_x&&a.sr.jitter_y==b.sr.jitter_y&&a.sr.mv_x==b.sr.mv_x&&a.sr.mv_y==b.sr.mv_y&&a.sr.reset==b.sr.reset;}
Result Begin(Context* c,ID3D12GraphicsCommandList* list,const Input& i){
    if(!c||c->poisoned||c->sr.Poisoned())return {Status::Poisoned};
    if(c->retiring||c->armed||c->awaiting_submission)return {Status::Busy};
    if(!nr_edit_dx12::Collect(c->kernels)){c->poisoned=true;return {Status::Poisoned};}if(!Valid(*c,i))return {Status::Invalid};
    if(c->last_attempt.stream==i.sr.source.stream&&c->last_attempt.epoch==i.sr.source.epoch&&i.sr.source.sequence<=c->last_attempt.sequence)return {Status::Invalid};
    if(c->carrier_range!=i.constants.carrier_range||c->history_config!=i.config_generation||c->history_domain!=i.color_domain||c->history_view!=i.view){
        c->sr.ResetHistory();c->carrier_range=i.constants.carrier_range;c->history_config=i.config_generation;c->history_domain=i.color_domain;c->history_view=i.view;}
    Work w{};w.kernel=Kernel::DeferredEncode;w.recording=i.sr.recording;w.width=c->shape.render_width;w.height=c->shape.render_height;
    w.constants=&i.constants;w.constant_bytes=sizeof(Constants);w.reads[0]=i.edited;w.reads[1]=i.original;
    w.writes[0]=Owned(c->planes[0],10);w.writes[1]=Owned(c->planes[1],2);
    const auto a=Record(c->kernels,list,w);if(!a.recorded)return {a.status==nr_edit_dx12::Status::Busy?Status::Busy:Status::Invalid};
    c->last_attempt=i.sr.source;c->pending=i;c->armed=true;c->awaiting_submission=i.sr.recording;return {Status::Prepared,true,false};
}
Result Finish(Context* c,ID3D12GraphicsCommandList* list,const Input& i,std::uint32_t native_result){
    if(!c||c->poisoned||c->sr.Poisoned())return {Status::Poisoned};
    if(!c->armed)return {Status::Invalid};c->armed=false;
    if(!list||i.sr.recording.list!=reinterpret_cast<std::uint64_t>(list)||!Same(c->pending,i)||!Valid(*c,i)){c->sr.ResetHistory();return {Status::Invalid};}
    if(native_result!=nr_private_sr::success){c->sr.ResetHistory();return {Status::NativeFailed};}
    auto frame=c->pending.sr;frame.color=c->planes[0];frame.output=c->planes[2];frame.depth=i.depth.resource;frame.motion=i.motion.resource;
    frame.resource_contract_verified=i.sr.resource_contract_verified;if(!frame.Valid(c->shape))return {Status::Invalid};
    Transition(list,c->planes[0],D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(list,c->planes[2],D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(list,i.depth.resource,i.depth.state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(list,i.motion.resource,i.motion.state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    const auto sr=c->sr.Record(list,frame,c->shape);
    if(sr.status==nr_private_sr::Status::Poisoned){c->poisoned=true;return {Status::Poisoned,true,false};}
    if(sr.evaluated)Uav(list,c->planes[2]);
    Transition(list,c->planes[0],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
    Transition(list,c->planes[2],D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON);
    Transition(list,i.depth.resource,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,i.depth.state);
    Transition(list,i.motion.resource,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,i.motion.state);
    if(!sr.evaluated)return {Status::WaitingPrivateSr,true,false};
    auto constants=i.constants;constants.private_sr_succeeded=1;
    Work w{};w.kernel=Kernel::DeferredCompose;w.recording=i.sr.recording;w.width=c->shape.output_width;w.height=c->shape.output_height;
    w.constants=&constants;w.constant_bytes=sizeof(constants);w.reads[2]=Owned(c->planes[2],10);w.reads[3]=Owned(c->planes[1],2);
    w.reads[4]=i.clean_output;w.writes[2]=Owned(c->planes[3],10);
    const auto composed=Record(c->kernels,list,w);if(!composed.recorded){c->sr.ResetHistory();return {Status::WaitingPrivateSr,true,false};}
    Transition(list,c->planes[3],D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(list,i.clean_output.resource,i.clean_output.state,D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=c->planes[3];from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource=i.clean_output.resource;to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    Transition(list,i.clean_output.resource,D3D12_RESOURCE_STATE_COPY_DEST,i.clean_output.state);
    Transition(list,c->planes[3],D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
    return {Status::Composed,true,true};
}
void SetRetirementGuard(Context* c,nr_private_sr::RecordingReleased f,void* u){if(c)nr_edit_dx12::SetRetirementGuard(c->kernels,f,u);}
void MarkUncertain(Context* c){if(c){c->poisoned=true;nr_edit_dx12::MarkUncertain(c->kernels);}}
bool IsPoisoned(const Context* c){return c&&(c->poisoned||c->sr.Poisoned());}
void Cancel(Context* c){if(!c)return;c->armed=false;c->sr.ResetHistory();}
bool Submitted(Context* c,nr_private_sr::Recording r,ID3D12Fence* fence,nr_private_sr::Fence receipt,bool success){
    if(!c||c->poisoned)return false;
    const bool shaders=nr_edit_dx12::Submitted(c->kernels,r,fence,receipt,success);
    const bool sr=c->sr.Submitted(r,receipt,success);
    if(!success||c->sr.Poisoned()||nr_edit_dx12::Poisoned(c->kernels)){c->poisoned=true;return false;}
    if(c->awaiting_submission==r)c->awaiting_submission={};return shaders||sr;
}
void Discarded(Context* c,nr_private_sr::Recording r){if(!c)return;c->sr.Discarded(r);
    if(!nr_edit_dx12::Discarded(c->kernels,r)||c->sr.Poisoned())c->poisoned=true;c->armed=false;
    if(c->awaiting_submission==r)c->awaiting_submission={};}
bool ReadyToRetire(Context* c){return !c||(!c->poisoned&&!c->armed&&!c->awaiting_submission&&
    nr_edit_dx12::ReadyToRetire(c->kernels)&&c->sr.ReadyToRetire());}
bool Retire(Context*& c){if(!c)return true;if(c->poisoned||c->armed)return false;c->retiring=true;
    if(!c->sr.Retire()||!DestroyAfterRetirement(c->kernels))return false;c->FreePlanes();delete c;c=nullptr;return true;}
}
