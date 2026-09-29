#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include "nr_rr_dx12.hpp"
#include "nr_rr_admission.hpp"
#include <cmath>
#include <new>
namespace nr_rr_dx12 {
using namespace nr_edit_dx12;
namespace {
View Owned(ID3D12Resource* p,unsigned format){return {p,format,0,D3D12_RESOURCE_STATE_COMMON,true};}
bool Plane(const View& v,unsigned w,unsigned h){if(!v.resource||!v.state_known||v.mip)return false;const auto d=v.resource->GetDesc();
    return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.Width==w&&d.Height==h&&d.MipLevels==1&&d.DepthOrArraySize==1&&d.SampleDesc.Count==1;}
void Transition(ID3D12GraphicsCommandList* l,ID3D12Resource* r,unsigned before,unsigned after){if(before==after)return;
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=r;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore=static_cast<D3D12_RESOURCE_STATES>(before);b.Transition.StateAfter=static_cast<D3D12_RESOURCE_STATES>(after);l->ResourceBarrier(1,&b);}
}
class Context {
public:
    nr_private_sr::Shape shape{};Kernels* kernels=nullptr;
    // reprojection RGBA32, current-Z R32, edit histories RGBA32 x2,
    // depth histories R32 x2, final output RGBA16F. No duplicated NR model.
    std::array<ID3D12Resource*,7> planes{};
    nr_private_sr::Recording awaiting_submission{};
    Input pending{};nr_private_sr::Source previous{},last_attempt{};
    unsigned history_index=0;bool history_valid=false,armed=false,poisoned=false,retiring=false;
    std::uint64_t bytes=0,history_view=0,history_configuration=0,history_domain=0;
    float previous_ab[2]{};
    void Free(){for(auto*& p:planes){if(p)p->Release();p=nullptr;}}
};
Context* Create(ID3D12Device* device,nr_private_sr::Shape shape,const std::array<Bytecode,5>& code,SameDevice same,
                std::uint64_t available,std::uint64_t* reserved){
    if(reserved)*reserved=0;if(!device||!shape.Valid()||!reserved)return nullptr;
    auto* c=new(std::nothrow)Context;if(!c)return nullptr;c->shape=shape;
    c->kernels=nr_edit_dx12::Create(device,shape.device,shape.queue,code,same);if(!c->kernels){delete c;return nullptr;}
    const unsigned formats[]={2,41,2,2,41,41,10};
    for(unsigned i=0;i<c->planes.size();i++){std::uint64_t bytes=0;
        c->planes[i]=CreatePlane(device,i==6?shape.output_width:shape.render_width,i==6?shape.output_height:shape.render_height,
            formats[i],available-c->bytes,&bytes);
        if(!c->planes[i]){c->Free();DestroyAfterRetirement(c->kernels);delete c;return nullptr;}c->bytes+=bytes;}
    *reserved=c->bytes;return c;
}
bool Valid(const Context& c,const Input& i){const auto& s=c.shape;const auto& k=i.constants;
    if(AdmitScalars(i,s)!=Admission::Accepted||
       !Plane(i.edited,s.render_width,s.render_height)||!Plane(i.original,s.render_width,s.render_height)||
       !Plane(i.motion_pixels,s.render_width,s.render_height)||!Plane(i.hardware_depth,s.render_width,s.render_height)||
       !Plane(i.clean_output,s.output_width,s.output_height)||i.clean_output.format!=10||
       (i.clean_output.resource->GetDesc().Format!=DXGI_FORMAT_R16G16B16A16_FLOAT&&i.clean_output.resource->GetDesc().Format!=DXGI_FORMAT_R16G16B16A16_TYPELESS))return false;
    if(k.explicit_previous_depth_verified&&!Plane(i.explicit_previous_depth,s.render_width,s.render_height))return false;
    return true;
}
bool Same(const Input& a,const Input& b){return a.recording==b.recording&&a.source==b.source&&a.view==b.view&&
    a.configuration==b.configuration&&a.color_domain==b.color_domain&&a.clean_output.resource==b.clean_output.resource&&
    a.edited.resource==b.edited.resource&&a.original.resource==b.original.resource&&
    a.motion_pixels.resource==b.motion_pixels.resource&&a.hardware_depth.resource==b.hardware_depth.resource&&
    a.explicit_previous_depth.resource==b.explicit_previous_depth.resource&&
    a.constants.input_pre_exposure==b.constants.input_pre_exposure&&
    a.constants.output_pre_exposure==b.constants.output_pre_exposure&&
    a.edited.format==b.edited.format&&a.original.format==b.original.format&&
    a.clean_output.format==b.clean_output.format&&a.motion_pixels.format==b.motion_pixels.format&&
    a.hardware_depth.format==b.hardware_depth.format&&a.motion_reference==b.motion_reference;}
Result Begin(Context* c,ID3D12GraphicsCommandList* list,const Input& i){
    if(!c||c->poisoned)return {Status::Poisoned};if(c->retiring||c->armed||c->awaiting_submission)return {Status::Busy};
    if(!nr_edit_dx12::Collect(c->kernels)){c->poisoned=true;return {Status::Poisoned};}if(!Valid(*c,i))return {Status::Invalid};
    if(c->last_attempt.stream==i.source.stream&&c->last_attempt.epoch==i.source.epoch&&i.source.sequence<=c->last_attempt.sequence)return {Status::Invalid};
    auto k=i.constants;
    k.history_valid=c->history_valid&&!i.camera_cut&&(k.camera_mapping_verified||k.explicit_previous_depth_verified)&&i.motion_reference==c->previous&&
        i.view==c->history_view&&i.configuration==c->history_configuration&&i.color_domain==c->history_domain;
    k.previous_projection_ab[0]=c->previous_ab[0];k.previous_projection_ab[1]=c->previous_ab[1];
    Work w{};w.recording=i.recording;w.width=c->shape.render_width;w.height=c->shape.render_height;w.constants=&k;w.constant_bytes=sizeof(k);
    w.kernel=Kernel::RrGuides;w.reads[2]=i.motion_pixels;w.reads[3]=i.hardware_depth;
    if(k.explicit_previous_depth_verified)w.reads[10]=i.explicit_previous_depth;
    w.writes[0]=Owned(c->planes[0],2);w.writes[1]=Owned(c->planes[1],41);
    const auto guide=Record(c->kernels,list,w);if(!guide.recorded){c->history_valid=false;return {guide.status==nr_edit_dx12::Status::Busy?Status::Busy:Status::Invalid};}
    c->awaiting_submission=i.recording;c->last_attempt=i.source;
    w.reads={};w.writes={};w.kernel=Kernel::RrAccumulate;w.reads[0]=i.edited;w.reads[1]=i.original;
    w.reads[7]=Owned(c->planes[0],2);w.reads[9]=Owned(c->planes[1],41);
    if(k.history_valid){w.reads[4]=Owned(c->planes[2+c->history_index],2);w.reads[5]=Owned(c->planes[4+c->history_index],41);}
    const unsigned next=c->history_index^1u;w.writes[2]=Owned(c->planes[2+next],2);w.writes[3]=Owned(c->planes[4+next],41);
    const auto accumulate=Record(c->kernels,list,w);
    if(!accumulate.recorded){c->history_valid=false;return {Status::Busy,true,false};}
    c->pending=i;c->pending.constants=k;c->armed=true;return {Status::Prepared,true,false};
}
Result Finish(Context* c,ID3D12GraphicsCommandList* list,const Input& i,std::uint32_t native){
    if(!c||c->poisoned)return {Status::Poisoned};if(!c->armed)return {Status::Invalid};c->armed=false;
    if(!list||i.recording.list!=reinterpret_cast<std::uint64_t>(list)||!Same(c->pending,i)||!Valid(*c,i)||native!=nr_private_sr::success){c->history_valid=false;return {native!=nr_private_sr::success?Status::NativeFailed:Status::Invalid};}
    auto k=c->pending.constants;const unsigned next=c->history_index^1u;
    Work w{};w.kernel=Kernel::RrCompose;w.recording=i.recording;w.width=c->shape.output_width;w.height=c->shape.output_height;
    w.constants=&k;w.constant_bytes=sizeof(k);w.reads[6]=i.clean_output;w.reads[8]=Owned(c->planes[2+next],2);
    w.reads[9]=Owned(c->planes[1],41);w.writes[4]=Owned(c->planes[6],10);
    const auto a=Record(c->kernels,list,w);if(!a.recorded){c->history_valid=false;return {Status::Busy};}
    Transition(list,c->planes[6],D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(list,i.clean_output.resource,i.clean_output.state,D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.Type=to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    from.pResource=c->planes[6];to.pResource=i.clean_output.resource;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    Transition(list,i.clean_output.resource,D3D12_RESOURCE_STATE_COPY_DEST,i.clean_output.state);
    Transition(list,c->planes[6],D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
    c->history_index=next;c->history_valid=true;c->previous=i.source;c->previous_ab[0]=k.current_projection_ab[0];c->previous_ab[1]=k.current_projection_ab[1];
    c->history_view=i.view;c->history_configuration=i.configuration;c->history_domain=i.color_domain;
    return {Status::Composed,true,true};
}
void SetRetirementGuard(Context* c,nr_private_sr::RecordingReleased f,void* u){if(c)nr_edit_dx12::SetRetirementGuard(c->kernels,f,u);}
void MarkUncertain(Context* c){if(c){c->poisoned=true;nr_edit_dx12::MarkUncertain(c->kernels);}}
bool IsPoisoned(const Context* c){return c&&c->poisoned;}
void Cancel(Context* c){if(!c)return;c->armed=false;c->history_valid=false;}
bool Submitted(Context* c,nr_private_sr::Recording r,ID3D12Fence* fence,nr_private_sr::Fence receipt,bool success){
    if(!c||c->poisoned)return false;const bool ok=nr_edit_dx12::Submitted(c->kernels,r,fence,receipt,success);
    if(!success||nr_edit_dx12::Poisoned(c->kernels)){c->poisoned=true;c->history_valid=false;}
    if(ok&&c->awaiting_submission==r)c->awaiting_submission={};return ok;
}
void Discarded(Context* c,nr_private_sr::Recording r){if(!c)return;c->history_valid=false;c->armed=false;
    if(!nr_edit_dx12::Discarded(c->kernels,r))c->poisoned=true;
    if(c->awaiting_submission==r)c->awaiting_submission={};}
bool ReadyToRetire(Context* c){return !c||(!c->poisoned&&!c->armed&&!c->awaiting_submission&&nr_edit_dx12::ReadyToRetire(c->kernels));}
bool Retire(Context*& c){if(!c)return true;if(c->poisoned||c->armed)return false;c->retiring=true;
    if(!DestroyAfterRetirement(c->kernels))return false;c->Free();delete c;c=nullptr;return true;}
}
