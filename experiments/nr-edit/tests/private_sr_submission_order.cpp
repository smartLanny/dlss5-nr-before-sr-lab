// CPU-only contract test of the actual private SR owner. No GPU/model claims.
#include "nr_private_sr_runtime.hpp"
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
using namespace nr_private_sr;
namespace {
unsigned checks=0;
void Need(bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);}
struct Block{bool reset=false;};
struct Rig {
    unsigned creates=0,evaluates=0,releases=0,blocks=0,resources=0,fences=0;
    Id completed=0;std::set<Id> retired;bool last_reset=false;
    Api ApiValue(){
        Api a{};a.user=this;a.retirement_user=this;a.original_sr_owner_verified=true;
        a.allocate=[](void* u,void** p)->Result{++static_cast<Rig*>(u)->blocks;*p=new Block;return success;};
        a.destroy=[](void* u,void* p)->Result{--static_cast<Rig*>(u)->blocks;delete static_cast<Block*>(p);return success;};
        a.set_u=[](void*,void*,const char*,unsigned){};
        a.set_f=[](void*,void*,const char*,float){};
        a.set_r=[](void*,void*,const char*,void*){};
        a.set_i=[](void*,void* p,const char* k,int v){if(std::string(k)=="Reset")static_cast<Block*>(p)->reset=v!=0;};
        a.create=[](void* u,void*,int feature,void*,void** p)->Result{auto& r=*static_cast<Rig*>(u);Need(feature==1,"private feature is SR");++r.creates;*p=u;return success;};
        a.evaluate=[](void* u,void*,void*,void* p)->Result{auto& r=*static_cast<Rig*>(u);++r.evaluates;r.last_reset=static_cast<Block*>(p)->reset;return success;};
        a.release=[](void* u,void*)->Result{++static_cast<Rig*>(u)->releases;return success;};
        a.retain_resource=[](void* u,void*){++static_cast<Rig*>(u)->resources;};
        a.release_resource=[](void* u,void*){auto& r=*static_cast<Rig*>(u);Need(r.resources!=0,"balanced resource refs");--r.resources;};
        a.retain_fence=[](void* u,Id){++static_cast<Rig*>(u)->fences;};
        a.release_fence=[](void* u,Id){auto& r=*static_cast<Rig*>(u);Need(r.fences!=0,"balanced fence refs");--r.fences;};
        a.poll=[](void* u,Fence f)->Completion{return {f.queue,f.object,static_cast<Rig*>(u)->completed};};
        a.recording_released=[](void* u,Recording r){return static_cast<Rig*>(u)->retired.count(r.lifetime)!=0;};
        return a;
    }
};
Frame FrameAt(Id n){
    Frame f{};f.recording={1,2,100+n,1000+n,1};f.source={3,4,n};
    if(n>1)f.motion_reference={3,4,n-1};
    f.color=reinterpret_cast<void*>(10);f.depth=reinterpret_cast<void*>(11);
    f.motion=reinterpret_cast<void*>(12);f.output=reinterpret_cast<void*>(13);
    f.mv_x=f.mv_y=1;f.resource_contract_verified=true;f.reset=false;return f;
}
Outcome Record(Context& c,Id n,const Shape& s){auto f=FrameAt(n);return c.Record(reinterpret_cast<void*>(f.recording.list),f,s);}
}
int main(){try{
    Rig r;Context c;Shape s{32,24,64,48,0,3,1,2,1};
    Need(c.Initialize(r.ApiValue(),s),"initialize");
    Need(Record(c,1,s).status==Status::CreatedNeedsSubmission,"creation only");
    Need(Record(c,2,s).status==Status::AwaitCreateCompletion,"creation requires submission");
    Need(c.Submitted(FrameAt(1).recording,{2,9,1},true),"submit creation");
    r.completed=1;r.retired.insert(1001);Need(c.Collect(),"retire creation");
    Need(Record(c,2,s).evaluated&&r.evaluates==1&&r.last_reset,"first evaluate resets");
    const auto held=r.resources;
    // List 2 has NOT been enqueued. Recording list 3 now would let the caller
    // submit 3 before 2 even on the same direct queue and corrupt private history.
    auto next=Record(c,3,s);
    Need(next.status==Status::Busy&&!next.recorded&&!next.evaluated&&r.evaluates==1,
         "unsubmitted predecessor must block a different recording before Evaluate");
    Need(r.resources==held,"blocked recording pins no resources");
    Need(c.Submitted(FrameAt(2).recording,{2,9,2},true),"submit predecessor");
    // Its GPU work is still in flight. A queue-order receipt, not a CPU wait,
    // permits the next list to record on that same queue.
    Need(r.completed==1,"no completion wait was added");
    Need(Record(c,3,s).evaluated&&r.evaluates==2,"submitted in-flight predecessor permits successor");
    Need(c.Submitted(FrameAt(3).recording,{2,9,3},true),"submit second evaluate");
    Need(Record(c,4,s).evaluated&&r.evaluates==3,"third independent flight available");
    Need(c.Submitted(FrameAt(4).recording,{2,9,4},true),"submit third evaluate");
    Need(Record(c,5,s).status==Status::Busy,"three live flights remain bounded");
    r.completed=4;Need(c.Collect()&&!c.ReadyToRetire(),"fence alone does not retire replayable recordings");
    r.retired.insert(1002);r.retired.insert(1003);r.retired.insert(1004);
    Need(c.Collect()&&r.resources==0&&r.fences==0,"exact recording retirement releases flights");
    Need(Record(c,6,s).evaluated&&r.last_reset,"skipped source resets history");
    Need(c.Submitted(FrameAt(6).recording,{2,9,6},true),"submit after gap");
    r.completed=6;r.retired.insert(1006);
    Need(c.Retire()&&r.blocks==0&&r.resources==0&&r.fences==0&&r.releases==1,"complete explicit retirement");
    std::cout<<"PASS private SR submission order checks="<<checks<<"; actual owner, fixture API, no GPU or game\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
