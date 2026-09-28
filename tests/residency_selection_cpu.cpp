// Actual FindOrCreateSlot and fence predicates, with explicit CPU resource
// service doubles. Allocation counts are not measured driver/NGX VRAM.
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include "nr_motion_slot_selection.h"
#include "nr_residency_policy.h"
using DXGI_FORMAT=unsigned;
enum class CodecColorMode { Automatic, ModelEncodedValueDomain };
struct Desc { unsigned long long Width=640; unsigned Height=360; DXGI_FORMAT Format=10; };
struct ID3D12Resource { Desc desc{}; };
struct ID3D12Device {};
struct ID3D12Fence { unsigned long long completed=0; };
struct LifecycleQueue { ID3D12Fence* fence=nullptr; };
struct NrColorSemanticContract {};
struct Key { ID3D12Resource* resource=nullptr; unsigned long long generation=0; bool operator==(const Key&) const=default; };
struct View { Key key{}; struct { DXGI_FORMAT Format=10; } typed; struct {unsigned uav=10;} plan; bool native_bounded_output=false; explicit operator bool()const{return key.resource!=nullptr;} };
struct NrSlot {
    ID3D12Resource *key=nullptr,*exposure_source=nullptr,*motion_source=nullptr,*motion_proxy=nullptr,*result=nullptr;
    ID3D12Resource owned_result{};
    void* heap=nullptr;
    Key native_format_key{};
    DXGI_FORMAT result_format=10,result_srv_format=10,source_srv_format=10,result_uav_format=10;
    CodecColorMode codec_color_mode=CodecColorMode::Automatic;
    bool owns_source=false,owns_result=true,snapshots_native_output=false;
    unsigned long long last_used_serial=0,completion_fence_value=0;
    unsigned recording_command_lists=0;
    bool awaiting_fence_signal=false,bindings_ok=true;
    LifecycleQueue* last_submission_queue=nullptr;
};
struct NrContext {
    const void* sr_handle=nullptr;
    unsigned layer_index=0,width=640,height=360;
    bool retired=false;
    int mode=2;
    unsigned long long feature_generation=1,slot_serial=0;
    unsigned slot_recycles=0;
    NrSlot slots[8]{};
};
std::atomic<bool> g_lifecycle_tracking_uncertain{false};
unsigned allocations=0,rebindings=0,checks=0,adoptions=0;
Desc ID3D12Resource_GetDesc(ID3D12Resource* r){return r->desc;}
unsigned long long ID3D12Fence_GetCompletedValue(ID3D12Fence* f){return f->completed;}
View NrResolveColorView(ID3D12Device*,ID3D12Resource* r,Desc d,unsigned long long generation,bool,int,const NrColorSemanticContract*){return {{r,generation},{d.Format},{d.Format},false};}
using PostSrStorageContract=int;
int ClassifyPostSrStorage(DXGI_FORMAT,int,bool){return 0;}
bool SupportsPostSrStorage(ID3D12Device*,decltype(View::typed),int){return true;}
CodecColorMode PostSrCodecColorMode(int){return CodecColorMode::Automatic;}
template<class...A>void Log(const char*,A...){}
namespace nr_recovery {enum class Stage {ResourceAllocation};}
unsigned NrRecoveryBegin(View,bool){return 1;}
void NrRecoveryRecord(unsigned,nr_recovery::Stage,bool){}
void NrRecoveryRetireSlot(NrSlot&,bool){}
void NrRecoveryAdoptSlot(NrSlot&,unsigned,bool){++adoptions;}
void CollectSlotDeltaStatsLocked(NrContext&,NrSlot&){}
#include "residency_lifetime.inl"
bool RebindSlotExternal(ID3D12Device*,ID3D12Resource* external,bool post,ID3D12Resource* exposure,NrSlot& s,CodecColorMode color,int,unsigned long long generation,const NrColorSemanticContract*) {
    if(!CanMutateSlotLocked(s)||!s.bindings_ok)return false;
    s.key=external;s.exposure_source=exposure;s.native_format_key={external,generation};
    s.codec_color_mode=color;
    if(post)s.result=external;
    else {s.owned_result.desc=external->desc;s.result=&s.owned_result;}
    s.awaiting_fence_signal=false;s.completion_fence_value=0;s.last_submission_queue=nullptr;
    ++rebindings;return true;
}
bool CreateSlot(ID3D12Device*,ID3D12Resource* external,unsigned,unsigned,bool post,ID3D12Resource* exposure,NrSlot& s,CodecColorMode color,int,unsigned long long generation,const NrColorSemanticContract*){
    ++allocations;s.key=external;s.exposure_source=exposure;s.native_format_key={external,generation};s.heap=&s;
    s.codec_color_mode=color;s.owns_source=post;s.owns_result=!post;s.snapshots_native_output=post;
    s.source_srv_format=s.result_format=s.result_srv_format=s.result_uav_format=external->desc.Format;
    s.owned_result.desc=external->desc;s.result=post?external:&s.owned_result;return true;
}
#include "residency_selection.inl"
void check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(3);}}
void reset(){allocations=rebindings=adoptions=0;g_lifecycle_tracking_uncertain=false;}
ID3D12Device device;
ID3D12Resource resources[128];
NrSlot* get(NrContext& c,unsigned i,bool post=false){return FindOrCreateSlot(c,&device,&resources[i],post);}
void rotation(bool post){
    reset();NrContext c;c.mode=post?1:2;
    for(unsigned i=0;i<100;++i)check(get(c,i,post)!=nullptr,"rotation unnecessarily refused");
#if BASELINE
    check(allocations==8,"baseline no longer reproduces eight-set high water");
#else
    check(allocations==1&&rebindings==99,"idle slots still grow before reuse");
#endif
    std::printf("ROTATION mode=%d allocations=%u rebindings=%u adopted=%u\n",c.mode,allocations,rebindings,adoptions);
}
void busy(){
    // Every lifetime refusal separately prevents reuse of a completed-looking
    // old pointer. The controlled double never turns an age into completion.
    for(unsigned reason=0;reason<6;++reason){reset();NrContext c;auto* s=get(c,0);check(s,"first allocation");ID3D12Fence f{4};LifecycleQueue q{&f};
        if(reason==0)s->recording_command_lists=1;
        if(reason==1)s->awaiting_fence_signal=true;
        if(reason==2){s->completion_fence_value=5;s->last_submission_queue=&q;}
        if(reason==3){s->completion_fence_value=5;}
        if(reason==4){s->completion_fence_value=5;s->last_submission_queue=&q;f.completed=UINT64_MAX;}
        if(reason==5)s->bindings_ok=false;
        auto* next=get(c,1);check(next&&next!=s&&allocations==2,"busy/unknown/binding lease overwritten");
    }
    reset();NrContext c;
    for(unsigned i=0;i<8;++i){auto* s=get(c,i);check(s,"necessary pool growth refused");s->recording_command_lists=1;}
    check(!get(c,9)&&allocations==8,"capacity or in-flight check bypassed");
    c.slots[3].recording_command_lists=0;
    check(get(c,9)==&c.slots[3]&&allocations==8,"exact retirement did not restore reuse");
    auto* exact=get(c,9);check(exact==&c.slots[3]&&allocations==8,"stable binding stopped matching");
}
void footprint(){
    reset();NrContext c;auto* first=get(c,0);
    auto old=resources[1].desc;resources[1].desc.Width=1280;
    check(get(c,1)!=first&&allocations==2,"opportunistic reuse rebuilt a different Result footprint");resources[1].desc=old;
    reset();NrContext p;p.mode=1;first=get(p,0,true);resources[1].desc.Format=20;
    check(get(p,1,true)!=first&&allocations==2,"typed storage mismatch ignored");resources[1].desc=old;
    reset();NrContext m;first=get(m,0);ID3D12Resource motion[2];first->motion_source=&motion[0];first->motion_proxy=&motion[0];first->recording_command_lists=1;
    check(FindOrCreateSlot(m,&device,&resources[0],false,nullptr,CodecColorMode::Automatic,0,nullptr,&motion[1])!=first,"busy different Motion binding overwritten");
}
void cancellation(){
    NrContext contexts[16]{};
    for(unsigned i=0;i<10;++i){contexts[i].sr_handle=&contexts;contexts[i].layer_index=i%5;contexts[i].mode=i<5?1:2;}
    check(nr_residency::RetireForCompaction(contexts,16,5,5)==0,"same selection F6 cache discarded");
    check(nr_residency::RetireForCompaction(contexts,16,5,3)==10,"all old owned contexts must drain for allocator compaction");
    check(nr_residency::RetireForCompaction(contexts,16,3,1)==10,"OFF reduction to one must retain the compaction request");
    check(contexts[0].retired&&contexts[5].retired,"old primary keeps the allocator high-water alive");
    check(nr_residency::RetireForCompaction(contexts,16,1,5)==0&&contexts[4].retired,"rapid re-enable resurrected old retirement");
    check(nr_residency::RetireForCompaction(contexts,16,5,0)==0,"invalid request changed residency");
    for(unsigned i=10;i<16;++i)check(!contexts[i].retired,"empty table entry retired");
    check(nr_residency::RetireForCompaction(contexts,16,5,5,true)==10,"explicit route change must not keep old-mode models");
}
int main(){rotation(false);rotation(true);busy();footprint();cancellation();std::printf("PASS %u assertions; actual slot selector + exact fence predicates, CPU doubles, no VRAM/NGX inference\n",checks);}
