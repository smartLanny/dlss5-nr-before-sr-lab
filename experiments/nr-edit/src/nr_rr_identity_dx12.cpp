#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include "nr_rr_identity_dx12.hpp"
#include <cmath>
#include <cstring>
#include <new>
namespace nr_rr_identity {
namespace {
constexpr Result fatal=0xBAD00005u;
struct Value {Result result=0;void* value=nullptr;friend bool operator==(const Value&,const Value&)=default;};
Value Read(Parameters p,const char* key,bool typed){Value v;v.result=p.read(p.object,key,typed,&v.value);return v;}
bool Success(Value v){return v.result==nr_private_sr::success;}
bool SameDevice(ID3D12Device* a,ID3D12Device* b){
    IUnknown *x=nullptr,*y=nullptr;
    const bool ok=a&&b&&SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x)))&&
        SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y)))&&x==y;
    if(x)x->Release();if(y)y->Release();return ok;
}
void Transition(ID3D12GraphicsCommandList* l,ID3D12Resource* r,unsigned a,unsigned b){
    if(a==b)return;
    D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    v.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        static_cast<D3D12_RESOURCE_STATES>(a),static_cast<D3D12_RESOURCE_STATES>(b)};
    l->ResourceBarrier(1,&v);
}
bool SameDesc(const D3D12_RESOURCE_DESC& a,const D3D12_RESOURCE_DESC& b){
    return a.Dimension==b.Dimension&&a.Alignment==b.Alignment&&a.Width==b.Width&&
        a.Height==b.Height&&a.DepthOrArraySize==b.DepthOrArraySize&&a.MipLevels==b.MipLevels&&
        a.Format==b.Format&&a.SampleDesc.Count==b.SampleDesc.Count&&
        a.SampleDesc.Quality==b.SampleDesc.Quality&&a.Layout==b.Layout&&a.Flags==b.Flags;
}
}
class Context {
public:
    ID3D12Device* device=nullptr;
    ID3D12Resource* copy=nullptr;
    LeaseAdapter leases{};void* lease=nullptr;
    bool poisoned=false,copy_state_known=true;D3D12_RESOURCE_DESC desc{};Source last{};
    bool Collect(){
        if(poisoned)return false;
        if(FAILED(device->GetDeviceRemovedReason())||
           (lease&&!leases.healthy(leases.user,lease))){Poison();return false;}
        if(lease){if(!leases.mutable_now(leases.user,lease))return false;
            leases.release(leases.user,lease);lease=nullptr;}
        return true;
    }
    void Poison(){poisoned=true;if(lease)leases.quarantine(leases.user,lease);}
};
Context* Create(ID3D12Device* device,LeaseAdapter a){
    if(!device||!a.current||!a.record||!a.native_consumer||!a.healthy||!a.mutable_now||!a.release||!a.quarantine)return nullptr;
    auto* c=new(std::nothrow)Context;if(!c)return nullptr;
    c->device=device;device->AddRef();c->leases=a;return c;
}
bool Retire(Context*& c){
    if(!c)return true;if(!c->Collect())return false;
    if(c->copy)c->copy->Release();c->device->Release();delete c;c=nullptr;return true;
}
Outcome Evaluate(Context* c,bool enabled,ID3D12GraphicsCommandList* list,const Frame& f,
    Parameters p,Envelope envelope,std::uint64_t budget,Native native,void* user){
    Outcome out{};
    const auto call=[&](){++out.native_calls;out.result=native(user,list,p);return out;};
    const auto decline=[&](Reason r){out.reason=r;return call();};
    if(!native){out.result=fatal;return out;}
    // Off path: no resource query/allocation, lease, parameter read or GPU work.
    if(c&&c->poisoned){out.reason=Reason::OpaqueFailure;out.result=fatal;return out;}
    if(!enabled)return decline(Reason::Disabled);
    if(!c)return decline(Reason::Contract);
    if(!list||!f.recording||!f.source||!f.view||!f.module_generation||!f.feature_generation||
       !f.native_contract_known||!f.transparency_absent_known||!f.all_color_aliases_known||
       !f.single_layer||!f.fg_off_known||!std::isfinite(f.pre_exposure)||f.pre_exposure<=0||
       !std::isfinite(f.exposure_scale)||f.exposure_scale<=0)return decline(Reason::Contract);
    if(f.recording.list!=reinterpret_cast<Id>(list)||
       !c->leases.current(c->leases.user,f))return decline(Reason::Recording);
    if(c->last.stream==f.source.stream&&c->last.epoch==f.source.epoch&&
       f.source.sequence<=c->last.sequence)return decline(Reason::Recording);
    if(!p.object||!p.read||!p.write)return decline(Reason::Parameters);
    const auto typed=Read(p,"Color",true),untyped=Read(p,"Color",false);
    if((!Success(typed)&&!Success(untyped))||
       (Success(typed)&&typed.value!=f.color.resource)||
       (Success(untyped)&&untyped.value!=f.color.resource))return decline(Reason::Parameters);
    // No other resource key may require a coordinated Color replacement.
    // Unknown absence/semantics was rejected by the immutable contract above.
    for(const char* key:{"Output","Depth","MotionVectors",
        "DLSS.Input.DiffuseAlbedo","DLSS.Input.SpecularAlbedo","GBuffer.Normals",
        "GBuffer.Roughness","GBuffer.SpecularMvec","DLSSD.SpecularHitDistance",
        "DLSS.TransparencyLayer","DLSS.TransparencyLayerOpacity","DLSS.TransparencyLayerMvecs",
        "DLSSD.ColorBeforeTransparency","DLSSD.Alpha","DLSSD.OutputAlpha","DLSSD.ReflectedAlbedo",
        "DLSSD.ColorBeforeParticles","DLSSD.ColorAfterParticles","DLSSD.ColorAfterTransparency",
        "DLSSD.ColorBeforeFog","DLSSD.ColorAfterFog","DLSSD.ScreenSpaceSubsurfaceScatteringGuide",
        "DLSSD.ColorBeforeScreenSpaceSubsurfaceScattering","DLSSD.ColorAfterScreenSpaceSubsurfaceScattering",
        "DLSSD.ScreenSpaceRefractionGuide","DLSSD.ColorBeforeScreenSpaceRefraction",
        "DLSSD.ColorAfterScreenSpaceRefraction","DLSSD.DepthOfFieldGuide","DLSSD.ColorBeforeDepthOfField",
        "DLSSD.ColorAfterDepthOfField","DLSSD.DiffuseHitDistance","DLSSD.DiffuseRayDirection",
        "DLSSD.SpecularRayDirection","DLSSD.DiffuseRayDirectionHitDistance","DLSSD.SpecularRayDirectionHitDistance",
        "DLSSD.ResponsivityMask","DLSS.DisocclusionMask","DLSS.Input.Bias.Current.Color.Mask",
        "TransparencyMask","ExposureTexture"}){
        const auto a=Read(p,key,true),b=Read(p,key,false);
        const bool dependent_color=std::strstr(key,"ColorBefore")||std::strstr(key,"ColorAfter")||
            std::strstr(key,"TransparencyLayer");
        if((Success(a)&&a.value==f.color.resource)||(Success(b)&&b.value==f.color.resource)||
           (dependent_color&&((Success(a)&&a.value)||(Success(b)&&b.value))))return decline(Reason::Alias);
    }
    constexpr unsigned read=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    constexpr unsigned allowed=read|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_COPY_SOURCE;
    if(!f.color.resource||!f.color.state_known||!(f.color.state&read)||(f.color.state&~allowed)||
       !envelope.capture||!envelope.restore)return decline(Reason::State);
    const auto d=f.color.resource->GetDesc();
    if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.MipLevels!=1||d.DepthOrArraySize!=1||
       d.SampleDesc.Count!=1||d.SampleDesc.Quality||f.color.mip||
       (d.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT&&d.Format!=DXGI_FORMAT_R32G32B32A32_FLOAT)||
       f.color.format!=static_cast<unsigned>(d.Format)||!f.render_width||!f.render_height||
       f.base_x>d.Width||f.render_width>d.Width-f.base_x||f.base_y>d.Height||
       f.render_height>d.Height-f.base_y||!f.output_width||!f.output_height)return decline(Reason::Format);
    if(FAILED(c->device->GetDeviceRemovedReason())){
        c->Poison();out.reason=Reason::OpaqueFailure;out.result=fatal;return out;}
    ID3D12Device *resource_device=nullptr,*list_device=nullptr;
    const bool device_ok=SUCCEEDED(f.color.resource->GetDevice(IID_PPV_ARGS(&resource_device)))&&
        SUCCEEDED(list->GetDevice(IID_PPV_ARGS(&list_device)))&&SameDevice(c->device,resource_device)&&
        SameDevice(c->device,list_device);
    if(resource_device)resource_device->Release();if(list_device)list_device->Release();
    if(!device_ok)return decline(Reason::Device);
    if(!c->Collect()){
        if(c->poisoned){out.result=fatal;out.reason=Reason::OpaqueFailure;return out;}
        return decline(Reason::Busy);
    }
    const auto allocation=c->device->GetResourceAllocationInfo(0,1,&d);
    if(!allocation.SizeInBytes||allocation.SizeInBytes==UINT64_MAX||allocation.SizeInBytes>budget)
        return decline(Reason::Budget);
    if(!c->copy_state_known&&c->copy){c->copy->Release();c->copy=nullptr;}
    if(!c->copy||!SameDesc(c->desc,d)){
        ID3D12Resource* next=nullptr;D3D12_HEAP_PROPERTIES heap{};
        heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        if(FAILED(c->device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&next))))return decline(Reason::Budget);
        if(c->copy)c->copy->Release();c->copy=next;c->desc=d;c->copy_state_known=true;
    }
    if(!envelope.capture(envelope.user,list))return decline(Reason::State);
    c->lease=c->leases.record(c->leases.user,f,f.color.resource,c->copy);
    if(!c->lease){
        if(!envelope.restore(envelope.user)){c->Poison();out.result=fatal;out.reason=Reason::RestoreFailed;return out;}
        return decline(Reason::Lease);
    }
    Transition(list,f.color.resource,f.color.state,D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(list,c->copy,read,D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyResource(c->copy,f.color.resource);
    Transition(list,c->copy,D3D12_RESOURCE_STATE_COPY_DEST,read);
    Transition(list,f.color.resource,D3D12_RESOURCE_STATE_COPY_SOURCE,f.color.state);
    out.copied=true;c->last=f.source;
    if(!envelope.restore(envelope.user)){c->Poison();out.result=fatal;out.reason=Reason::RestoreFailed;return out;}
    const bool use_typed=Success(typed);
    const auto restore=[&](){
        p.write(p.object,"Color",use_typed,use_typed?typed.value:untyped.value);
        return Read(p,"Color",true)==typed&&Read(p,"Color",false)==untyped;
    };
    p.write(p.object,"Color",use_typed,c->copy);
    const auto changed_t=Read(p,"Color",true),changed_u=Read(p,"Color",false);
    const bool changed=(Success(typed)?Success(changed_t)&&changed_t.value==c->copy:changed_t==typed)&&
        (Success(untyped)?Success(changed_u)&&changed_u.value==c->copy:changed_u==untyped);
    if(!changed){
        if(!restore()){c->Poison();out.result=fatal;out.reason=Reason::RestoreFailed;return out;}
        out.restored=true;return decline(Reason::Parameters);
    }
    out.substituted=true;out.reason=Reason::Ready;
    if(!c->leases.native_consumer(c->leases.user,c->lease)){
        out.restored=restore();c->Poison();out.result=fatal;out.reason=Reason::RestoreFailed;return out;
    }
    ++out.native_calls;out.result=native(user,list,p);
    unsigned observed=0;
    c->copy_state_known=c->leases.copy_read_state&&
        c->leases.copy_read_state(c->leases.user,c->lease,&observed)&&observed==read;
    out.restored=restore();
    if(!out.restored){c->Poison();out.result=fatal;out.reason=Reason::RestoreFailed;}
    else if(out.result!=nr_private_sr::success||!c->leases.current(c->leases.user,f)){
        c->Poison();out.reason=Reason::OpaqueFailure;}
    return out;
}
}
