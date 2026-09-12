#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include "nr_rr_edit_program.hpp"
#include <new>
#include <cstring>
namespace nr_edit::dx12 {
struct RrProgram {Dispatcher* stages[3]{};};
namespace {
void OrderedRead(ID3D12GraphicsCommandList* list,ID3D12Resource* resource){
    // Only the owned outputs of the immediately preceding dispatch. Their
    // arrival state is the explicit UAV precondition, not a guessed game state.
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    list->ResourceBarrier(1,&b);
}
bool Shape(const FrameKey& key,const RrShaderConstants& c,ID3D12GraphicsCommandList* list){
    return key&&list&&
        key.render_width==c.render_size[0]&&key.render_height==c.render_size[1]&&
        key.output_width==c.output_size[0]&&key.output_height==c.output_size[1]&&c.frame_pair_valid;
}
}
RrProgram* CreateRr(ID3D12Device* device,const wchar_t* shader,char* error,std::size_t size){
    auto* p=new(std::nothrow) RrProgram;if(!p)return nullptr;
    const char* names[]={"PrepareGuidesCS","AccumulateCS","ComposeCS"};
    for(unsigned i=0;i<3;++i)if(!(p->stages[i]=Create(device,shader,names[i],error,size))){DestroyRrAfterRetirement(p);return nullptr;}
    return p;
}
bool RecordRrBefore(RrProgram* p,RrRecorded& r,ID3D12GraphicsCommandList* list,const FrameKey& key,
                    const RrImages& v,const RrShaderConstants& c){
    if(!p||r.tickets[0]||r.tickets[1]||r.tickets[2]||!Shape(key,c,list)||!v.motion.resource||
       !v.hardware_depth.resource||!v.reprojection.resource||!v.view_depth.resource||!v.edited.resource||
       !v.original.resource||!v.edit.resource||!v.history_depth.resource||
       (c.history_valid&&(!v.previous_edit.resource||!v.previous_depth.resource))||
       (c.explicit_previous_depth_verified&&!v.expected_previous_depth.resource))return false;
    r.key=key;r.list=list;r.images=v;r.constants=c;r.pair.Begin();r.post_attempted=false;
    Pass a{};a.list=list;a.constants=&c;a.constant_bytes=sizeof(c);a.width=key.render_width;a.height=key.render_height;
    a.reads[2]=v.motion;a.reads[3]=v.hardware_depth;a.reads[10]=v.expected_previous_depth;
    a.writes[0]=v.reprojection;a.writes[1]=v.view_depth;
    r.tickets[0]=Record(p->stages[0],a);if(!r.tickets[0])return false;
    OrderedRead(list,v.reprojection.resource);OrderedRead(list,v.view_depth.resource);
    Pass b{};b.list=list;b.constants=&c;b.constant_bytes=sizeof(c);b.width=a.width;b.height=a.height;
    b.reads[0]=v.edited;b.reads[1]=v.original;b.reads[4]=v.previous_edit;b.reads[5]=v.previous_depth;
    b.reads[7]=v.reprojection;b.reads[9]=v.view_depth;b.writes[2]=v.edit;b.writes[3]=v.history_depth;
    r.tickets[1]=Record(p->stages[1],b);if(!r.tickets[1])return false;
    OrderedRead(list,v.edit.resource);OrderedRead(list,v.history_depth.resource);
    return r.pair.Arm(key,true);
}
bool RecordRrAfter(RrProgram* p,RrRecorded& r,ID3D12GraphicsCommandList* list,const FrameKey& key,
                   const RrImages& v,const RrShaderConstants& c,bool native_success){
    if(!p||r.post_attempted)return false;
    r.post_attempted=true;
    if(r.pair.Consume(key,native_success)!=PairResult::Ready||!Shape(key,c,list)||r.list!=list||std::memcmp(&r.constants,&c,sizeof(c))!=0||
       v.edit.resource!=r.images.edit.resource||v.view_depth.resource!=r.images.view_depth.resource||
       !v.clean.resource||!v.final.resource||!v.edit.resource||!v.view_depth.resource)return false;
    Pass out{};out.list=list;out.constants=&c;out.constant_bytes=sizeof(c);out.width=key.output_width;out.height=key.output_height;
    out.reads[6]=v.clean;out.reads[8]=v.edit;out.reads[9]=v.view_depth;out.writes[4]=v.final;
    r.tickets[2]=Record(p->stages[2],out);return bool(r.tickets[2]);
}
bool SubmitRr(RrProgram* p,RrRecorded& r,ID3D12CommandQueue* queue,ID3D12Fence* fence,std::uint64_t value,std::uint64_t queue_identity){
    if(!p||!queue||queue_identity!=r.key.queue)return false;
    bool ok=true;for(unsigned i=0;i<3;++i)if(r.tickets[i]){
        const bool sent=Submitted(p->stages[i],r.tickets[i],queue,fence,value);ok=sent&&ok;if(sent)r.tickets[i]={};}
    if(!ok)QuarantineRr(p);return ok;
}
void QuarantineRr(RrProgram* p){if(p)for(auto* s:p->stages)Quarantine(s);}
bool DestroyRrAfterRetirement(RrProgram*& p){if(!p)return true;bool ok=true;
    for(auto*& s:p->stages)ok=DestroyAfterRetirement(s)&&ok;if(ok){delete p;p=nullptr;}return ok;}
}
