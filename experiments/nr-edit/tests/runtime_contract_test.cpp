#include "nr_private_sr_runtime.hpp"
#include "nr_edit_policy.hpp"
#include "nr_edit_flow.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
using namespace nr_private_sr;
namespace {
struct Block{int reset=-1;};
struct Fake{
    Id complete=0;bool references_released=false;int retained=0,fences=0,creates=0,evaluates=0,releases=0,blocks=0;
    bool last_reset=false;std::vector<Block*> all;
    ~Fake(){for(auto*p:all)delete p;}
    Api Build(){Api a{};a.user=this;a.original_sr_owner_verified=true;
        a.allocate=[](void*u,void**p)->Result{auto&f=*static_cast<Fake*>(u);auto*b=new Block;f.all.push_back(b);*p=b;f.blocks++;return success;};
        a.destroy=[](void*u,void*p)->Result{auto&f=*static_cast<Fake*>(u);for(auto*&b:f.all)if(b==p){delete b;b=nullptr;f.blocks--;return success;}return 0;};
        a.set_u=[](void*,void*,const char*,unsigned){};a.set_f=[](void*,void*,const char*,float){};
        a.set_r=[](void*,void*,const char*,void*){};
        a.set_i=[](void*,void*p,const char*k,int v){if(std::string(k)=="Reset")static_cast<Block*>(p)->reset=v;};
        a.create=[](void*u,void*,int feature,void*,void**h)->Result{auto&f=*static_cast<Fake*>(u);assert(feature==1);f.creates++;*h=reinterpret_cast<void*>(100);return success;};
        a.evaluate=[](void*u,void*,void*h,void*p)->Result{auto&f=*static_cast<Fake*>(u);assert(h==reinterpret_cast<void*>(100));f.evaluates++;f.last_reset=static_cast<Block*>(p)->reset!=0;return success;};
        a.release=[](void*u,void*)->Result{static_cast<Fake*>(u)->releases++;return success;};
        a.retain_resource=[](void*u,void*){static_cast<Fake*>(u)->retained++;};a.release_resource=[](void*u,void*){static_cast<Fake*>(u)->retained--;};
        a.retain_fence=[](void*u,Id){static_cast<Fake*>(u)->fences++;};a.release_fence=[](void*u,Id){static_cast<Fake*>(u)->fences--;};
        a.poll=[](void*u,Fence f)->Completion{return {f.queue,f.object,static_cast<Fake*>(u)->complete};};
        a.recording_released=[](void*u,Recording){return static_cast<Fake*>(u)->references_released;};a.retirement_user=this;return a;
    }
};
Frame FrameAt(Id source){Frame f{};f.recording={1,2,3,source,1};f.source={4,5,source};if(source>1)f.motion_reference={4,5,source-1};
 f.color=reinterpret_cast<void*>(10);f.depth=reinterpret_cast<void*>(11);f.motion=reinterpret_cast<void*>(12);f.output=reinterpret_cast<void*>(13);
 f.mv_x=f.mv_y=1;f.reset=false;f.resource_contract_verified=true;return f;}
struct Flow{
 int* calls;bool ready=true,fatal=false,after=true;bool cancelled=false,quarantined=false;
 bool Eligible(){return ready;}bool CaptureBeforeState(){return true;}
 nr_edit_flow::Step ProducePrivateImage(){return {fatal?nr_edit_flow::Phase::Fatal:nr_edit_flow::Phase::Ready,19,true};}
 nr_edit_flow::Step PrepareEdit(){return {nr_edit_flow::Phase::Ready,0,true};}
 bool RestoreBeforeState(){return true;}
 std::uint32_t CallNative(){++*calls;return 1;}
 bool PostIdentityStillMatches(){return true;}bool CaptureAfterState(){return after;}
 nr_edit_flow::Step FinishEdit(std::uint32_t r){assert(r==1);return {nr_edit_flow::Phase::Composed,0,true};}
 bool RestoreAfterState(){return true;}void CancelPendingEdit(){cancelled=true;}void QuarantineAndAbandon(){quarantined=true;}
};
}
int main(){
 Fake f;Context c;Shape shape{640,360,1280,720,2,2,1,2,7};assert(c.Initialize(f.Build(),shape));assert(f.blocks==4);
 auto frame=FrameAt(1);auto r=c.Record(reinterpret_cast<void*>(3),frame,shape);assert(r.status==Status::CreatedNeedsSubmission&&f.creates==1);
 assert(c.Record(reinterpret_cast<void*>(3),FrameAt(2),shape).status==Status::AwaitCreateCompletion);
 assert(c.Submitted(frame.recording,{2,9,1},true));f.complete=1;
 r=c.Record(reinterpret_cast<void*>(3),FrameAt(2),shape);assert(r.evaluated&&f.last_reset);assert(f.retained==8);
 assert(c.LastBindEvidence().bound&&c.LastBindEvidence().predecessor==Source{}&&
        c.LastBindEvidence().reset_causes==(HistoryReset|PredecessorMismatch));
 assert(c.LastBindEvidence().history_causes==InitialHistory);
 assert(c.Submitted(FrameAt(2).recording,{2,9,2},true));f.complete=2;
 r=c.Record(reinterpret_cast<void*>(3),FrameAt(3),shape);assert(r.evaluated&&!f.last_reset);
 assert(c.LastBindEvidence().bound&&c.LastBindEvidence().predecessor==FrameAt(2).source&&
        c.LastBindEvidence().reset_causes==0);
 assert(c.Submitted(FrameAt(3).recording,{2,9,3},true));f.complete=3;
 assert(c.Record(reinterpret_cast<void*>(3),FrameAt(4),shape).status==Status::Busy);
 assert(!c.ReadyToRetire());f.references_released=true;assert(c.Collect());assert(f.retained==0&&f.fences==0);
 r=c.Record(reinterpret_cast<void*>(3),FrameAt(5),shape);assert(r.evaluated&&f.last_reset);
 assert(c.LastBindEvidence().predecessor==FrameAt(3).source&&
        c.LastBindEvidence().reset_causes==(HistoryReset|PredecessorMismatch));
 assert(c.LastBindEvidence().history_causes==SlotPressure);
 assert(c.Record(reinterpret_cast<void*>(3),FrameAt(5),shape).status==Status::DuplicateSource);
 assert(!c.LastBindEvidence().bound);
 assert(c.Submitted(FrameAt(5).recording,{2,9,5},true));f.complete=5;
 auto wrong_reference=FrameAt(6);wrong_reference.motion_reference=FrameAt(3).source;
 r=c.Record(reinterpret_cast<void*>(3),wrong_reference,shape);assert(r.evaluated&&f.last_reset);
 assert(c.LastBindEvidence().predecessor==FrameAt(5).source&&
        c.LastBindEvidence().reset_causes==PredecessorMismatch);
 assert(c.Submitted(wrong_reference.recording,{2,9,6},true));f.complete=6;
 auto explicit_reset=FrameAt(7);explicit_reset.reset=true;
 r=c.Record(reinterpret_cast<void*>(3),explicit_reset,shape);assert(r.evaluated&&f.last_reset);
 assert(c.LastBindEvidence().predecessor==FrameAt(6).source&&
        c.LastBindEvidence().reset_causes==RequestedReset&&c.LastBindEvidence().history_causes==0);
 assert(c.Submitted(explicit_reset.recording,{2,9,7},true));f.complete=7;
 assert(c.ReadyToRetire());assert(c.Retire());assert(f.blocks==0&&f.releases==1&&f.retained==0&&f.fences==0);
 for(unsigned bits=0;bits<8;bits++){int calls=0;Flow a{&calls};a.ready=(bits&1)!=0;a.fatal=(bits&2)!=0;a.after=(bits&4)!=0;
   auto result=nr_edit_flow::Execute(a);if(a.ready&&a.fatal){assert(calls==0&&a.quarantined&&result.fatal);}
   else{assert(calls==1&&!result.fatal);assert(result.composed==(a.ready&&a.after));}}
 nr_edit_policy::Matrix m{};for(unsigned i=0;i<4;i++)m[i*4+i]=1;nr_edit_policy::Matrix inv{};
 assert(nr_edit_policy::Invert(m,inv));m[15]=0;assert(!nr_edit_policy::Invert(m,inv));
 auto settings=nr_edit_policy::Sanitize({99,0,std::numeric_limits<float>::quiet_NaN(),2,true});
 assert(settings.mode==0&&settings.budget_mib==64&&settings.carrier_range==4&&settings.history_rate==1);
 assert(nr_edit_policy::CarrierFlags(1|4|64,true)==(2|4|8));
 std::cout<<"private SR lifecycle, exact retirement, resets, native-once flow and settings: passed\n";
}
