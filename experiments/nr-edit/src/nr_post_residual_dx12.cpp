#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include "nr_post_residual_dx12.hpp"
#include <cmath>
#include <algorithm>
namespace nr_post_residual_dx12 {
namespace {
using namespace nr_edit_dx12;
bool Contains(Kernels* k,const View& v,Rect r,unsigned format){
    if(!v.resource||v.mip||v.format!=format||!r.width||!r.height)return false;
    const auto d=v.resource->GetDesc();
    return d.Width<=16384&&d.Height<=16384&&r.x<=d.Width&&r.y<=d.Height&&
        r.width<=d.Width-r.x&&r.height<=d.Height-r.y&&
        ValidateRead(k,v,static_cast<unsigned>(d.Width),d.Height);
}
bool Exact(Kernels* k,const View& v,unsigned w,unsigned h,unsigned format){
    return v.resource&&Contains(k,v,{0,0,w,h},format)&&
        v.resource->GetDesc().Width==w&&v.resource->GetDesc().Height==h;
}
bool Valid(Kernels* k,const Pair& p){
    const auto r=p.base_rect;
    return (p.domain==Domain::SdrEncodedFp16||p.domain==Domain::NativeOutputValueFp16)&&p.same_encoding_verified&&
        p.base_source&&p.base_source==p.candidate_source&&p.base.resource!=p.candidate.resource&&
        r.width==p.candidate_rect.width&&r.height==p.candidate_rect.height&&
        p.work_width&&p.work_height&&p.work_width<=r.width&&p.work_height<=r.height&&
        p.work_width>=std::max(1u,r.width/4)&&p.work_height>=std::max(1u,r.height/4)&&
        std::isfinite(p.carrier_range)&&p.carrier_range>1e-6f&&p.carrier_range<=1e6f&&
        Contains(k,p.base,r,10)&&Contains(k,p.candidate,p.candidate_rect,10);
}
Constants Make(const Pair& p){
    Constants c{};c.full_size[0]=p.base_rect.width;c.full_size[1]=p.base_rect.height;
    c.work_size[0]=p.work_width;c.work_size[1]=p.work_height;
    c.base_origin[0]=p.base_rect.x;c.base_origin[1]=p.base_rect.y;
    c.candidate_origin[0]=p.candidate_rect.x;c.candidate_origin[1]=p.candidate_rect.y;
    c.carrier_range=p.carrier_range;c.pair_valid=1;return c;
}
}
Encoded Encode(nr_edit_dx12::Kernels* k,ID3D12GraphicsCommandList* list,nr_private_sr::Recording recording,
               const Pair& p,nr_edit_dx12::View carrier,nr_edit_dx12::View tail){
    Encoded out{};
    if(!Valid(k,p)||!Exact(k,carrier,p.work_width,p.work_height,10)||
        !Exact(k,tail,p.work_width,p.work_height,2))return out;
    auto c=Make(p);nr_edit_dx12::Work w{};w.kernel=nr_edit_dx12::Kernel::PostExtract;
    w.recording=recording;w.width=p.work_width;w.height=p.work_height;w.constants=&c;w.constant_bytes=sizeof(c);
    w.reads[0]=p.candidate;w.reads[1]=p.base;w.writes[0]=carrier;w.writes[1]=tail;
    out.dispatch=nr_edit_dx12::Record(k,list,w);
    if(out.dispatch.recorded){out.pair=p;out.carrier=carrier;out.tail=tail;out.recording=recording;}
    return out;
}
nr_edit_dx12::Answer Compose(nr_edit_dx12::Kernels* k,ID3D12GraphicsCommandList* list,
                            const Encoded& e,const Reconstruction& r){
    const auto& p=e.pair;const auto full=p.base_rect;
    if(!e||!Valid(k,p)||!(r.recording==e.recording)||!(r.source==p.base_source)||
        r.encoded_token.serial!=e.dispatch.token.serial||r.encoded_token.slot!=e.dispatch.token.slot||
        !r.succeeded||!r.target_contains_candidate||r.target.resource==p.base.resource||
        r.target.resource==e.carrier.resource||
        r.target_rect.width!=full.width||r.target_rect.height!=full.height||
        !Exact(k,r.carrier,full.width,full.height,10)||
        !Exact(k,e.tail,p.work_width,p.work_height,2)||
        (r.per_channel_support&&!Exact(k,e.carrier,p.work_width,p.work_height,10))||
        !Contains(k,r.target,r.target_rect,10))return {};
    if(r.target.resource==p.candidate.resource&&(r.target_rect.x!=p.candidate_rect.x||
        r.target_rect.y!=p.candidate_rect.y))return {};
    auto c=Make(p);c.target_origin[0]=r.target_rect.x;c.target_origin[1]=r.target_rect.y;c.reconstruction_succeeded=1;
    c.reserved=r.per_channel_support?1u:0u;
    nr_edit_dx12::Work w{};w.kernel=nr_edit_dx12::Kernel::PostCompose;
    w.recording=r.recording;w.width=full.width;w.height=full.height;w.constants=&c;w.constant_bytes=sizeof(c);
    if(r.per_channel_support)w.reads[0]=e.carrier;
    w.reads[1]=p.base;w.reads[2]=r.carrier;w.reads[3]=e.tail;w.writes[2]=r.target;
    return nr_edit_dx12::Record(k,list,w);
}
}
