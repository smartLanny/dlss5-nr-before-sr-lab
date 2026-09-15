#include "history_contract.hpp"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
using namespace nr::final_residual;
unsigned checks=0;
void check(bool ok,const char* message){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",message);std::exit(1);}}
Frame frame(Id sequence=1){return {{10,20,sequence},{1,1,1,1280,720,Backend::AmdOpticalFlow},sequence*10,false};}
FlowReceipt receipt(const Plan& p,const Frame& f){return {FlowStatus::Ready,f.contract.backend,p.history_generation,
    f.source,p.reference,p.action==Action::ResetBoth,true};}
Plan begin(HistoryContract& h,const Frame& f){auto p=h.prepare(f);check(p.work(),"valid unique source starts transaction");return p;}
void adopt(HistoryContract& h,const Frame& f){const auto p=begin(h,f);
    check(h.observe_flow(p.token,receipt(p,f))==(p.action==Action::ResetBoth?FlowDecision::Seed:FlowDecision::Accumulate),"flow accepts exact pair and reset generation");
    check(h.finish(p.token,p.history_generation,ResidualStatus::SubmittedAdopted)==Completion::Adopted,"real residual adoption advances shared reference");}
int main(){
    {HistoryContract h;adopt(h,frame());
     for(Id seq=2;seq<=7;++seq){auto f=frame(seq);auto p=begin(h,f);auto r=receipt(p,f);r.motion_usable=seq==7;
         check(p.action==Action::Continue,"warmup must advance without repeated vendor reset");
         check(h.observe_flow(p.token,r)==(seq==7?FlowDecision::Accumulate:FlowDecision::ReseedResidual),"unusable OF warmup/SCD cannot accumulate");
         check(h.finish(p.token,p.history_generation,ResidualStatus::SubmittedAdopted)==Completion::Adopted,"current-only seed keeps the exact pair reference");}
     check(h.history_generation()==1&&h.reference()==frame(7).source,"warmup completes in one vendor reset epoch");}
    {HistoryContract h;auto f=frame();auto p=begin(h,f);check(p.action==Action::ResetBoth&&!p.reference.valid(),"first frame resets both histories");
     check(h.prepare(f).action==Action::Busy,"parallel duplicate cannot acquire another ticket");
     check(h.observe_flow(p.token,{})==FlowDecision::Wait&&!h.has_history(),"pending OF never advances residual history");
     check(!h.cancel_before_flow(p.token),"pending OF cannot cancel as unrecorded work");
     check(h.observe_flow(p.token,receipt(p,f))==FlowDecision::Seed,"initial complete reset seeds history");
     check(!h.cancel_before_flow(p.token),"advanced OF cannot use prework cancellation");
     check(h.finish(p.token,p.history_generation,ResidualStatus::SubmittedAdopted)==Completion::Adopted,"seed adopted");
     const auto generation=h.history_generation();
     for(unsigned i=0;i<20;++i)check(h.prepare(f).action==Action::Duplicate,"FG repeats are not new source work");
     check(h.history_generation()==generation&&h.reference()==f.source,"duplicates do not rotate or reset histories");
     f=frame(2);p=begin(h,f);check(p.action==Action::Continue&&p.reference==frame(1).source,"adjacent real source uses prior adopted source");
     check(h.observe_flow(p.token,receipt(p,f))==FlowDecision::Accumulate,"real AMD pair permits accumulation");
     check(h.finish(p.token,p.history_generation,ResidualStatus::SubmittedAdopted)==Completion::Adopted,"second source adopted");
     check(h.prepare(frame(1)).action==Action::Stale,"older source cannot become a new history");}

    // Each changed contract component independently requires a new reset epoch.
    for(unsigned change=0;change<11;++change){HistoryContract h;adopt(h,frame());auto f=frame(2);
        switch(change){case 0:f.scene_reset=true;break;case 1:f.contract.color_domain=2;break;
        case 2:f.contract.width=640;break;case 3:f.contract.height=360;break;case 4:f.contract.config_generation=2;break;
        case 5:f.contract.device_generation=2;break;case 6:f.contract.backend=Backend::NvidiaOpticalFlow;break;
        case 7:f.source.sequence=3;break;case 8:f.source_time=1;break;
        case 9:++f.source.epoch;break;case 10:++f.source.stream;break;}
        auto p=begin(h,f);check(p.action==Action::ResetBoth&&p.history_generation>1&&!p.reference.valid(),"scene/gap/time/grid/domain/config/device/backend change resets both");
        check(h.observe_flow(p.token,receipt(p,f))==FlowDecision::Seed,"changed contract needs factual reset acknowledgement");
        check(h.finish(p.token,p.history_generation,ResidualStatus::SubmittedAdopted)==Completion::Adopted,"new contract seeds its own history");}

    for(unsigned mismatch=0;mismatch<5;++mismatch){HistoryContract h;adopt(h,frame());auto f=frame(2);auto p=begin(h,f);auto r=receipt(p,f);
        switch(mismatch){case 0:r.reference=frame(9).source;break;case 1:r.current=frame(3).source;break;
        case 2:++r.history_generation;break;case 3:r.reset_executed=true;break;case 4:r.backend=Backend::NvidiaOpticalFlow;break;}
        check(h.observe_flow(p.token,r)==FlowDecision::ResetRequired&&!h.has_history(),"wrong frame/reference/backend/OF epoch cannot accumulate");
        check(h.prepare(f).action==Action::Duplicate,"already attempted OF source cannot be retried as a fresh image");
        f=frame(3);p=begin(h,f);check(p.action==Action::ResetBoth&&p.history_generation>1,"mismatch forces a fresh shared reset epoch");
        auto stale=receipt(p,f);--stale.history_generation;
        check(h.observe_flow(p.token,stale)==FlowDecision::ResetRequired,"old OF generation cannot satisfy new residual reset");
        adopt(h,frame(4));check(h.has_history(),"both histories recover only after matching reset and adoption");}

    for(auto status:{ResidualStatus::NotSubmitted,ResidualStatus::SubmittedNotAdopted}){
        HistoryContract h;adopt(h,frame());auto f=frame(2);auto p=begin(h,f);
        check(h.observe_flow(p.token,receipt(p,f))==FlowDecision::Accumulate,"OF can advance before consumer refuses");
        check(h.finish(p.token,p.history_generation,status)==Completion::ResetRequired&&!h.has_history(),"non-adopted residual invalidates pair");
        f=frame(3);p=begin(h,f);check(p.action==Action::ResetBoth&&!p.reference.valid(),"next source resets OF and residual after consumer refusal");
        check(h.observe_flow(p.token,receipt(p,f))==FlowDecision::Seed,"reset OF cannot sample abandoned residual frame");
        check(h.finish(p.token,p.history_generation,ResidualStatus::SubmittedAdopted)==Completion::Adopted,"reset recovery adopts current only");}

    {HistoryContract h;adopt(h,frame());auto f=frame(2);auto p=begin(h,f);
     check(h.observe_flow(p.token,receipt(p,f))==FlowDecision::Accumulate,"prepare residual-generation mismatch");
     check(h.finish(p.token,p.history_generation+1,ResidualStatus::SubmittedAdopted)==Completion::ResetRequired,"residual epoch mismatch cannot commit even after submit");
     adopt(h,frame(3));check(h.reference()==frame(3).source,"recovery does not revive mismatched pixel history");}

    for(bool after_flow:{false,true})for(bool device:{false,true}){HistoryContract h;adopt(h,frame());auto f=frame(2);auto p=begin(h,f);
        if(after_flow){check(h.observe_flow(p.token,receipt(p,f))==FlowDecision::Accumulate,"prepare fatal residual submission");
            check(h.finish(p.token,p.history_generation,device?ResidualStatus::DeviceLost:ResidualStatus::SubmitFailed)==Completion::Quarantined,"residual device/submit failure quarantines session");}
        else {auto r=receipt(p,f);r.status=device?FlowStatus::DeviceLost:FlowStatus::SubmitFailed;
            check(h.observe_flow(p.token,r)==FlowDecision::Quarantined,"OF device/submit failure quarantines session");}
        check(h.quarantined()&&!h.has_history()&&h.prepare(frame(3)).action==Action::Quarantined,"no accumulation after failed device or unknown submission");}

    {HistoryContract h;adopt(h,frame());auto f=frame(2);auto p=begin(h,f);const auto generation=h.history_generation();
     check(h.cancel_before_flow(p.token)&&h.has_history()&&h.history_generation()==generation,"true pre-OF cancellation preserves accepted history");
     auto retry=begin(h,f);check(retry.token!=p.token&&retry.action==Action::Continue,"unrecorded same source can retry with a fresh token");
     check(h.observe_flow(p.token,receipt(p,f))==FlowDecision::Invalid,"stale OF callback cannot touch newer transaction");
     check(h.finish(p.token,p.history_generation,ResidualStatus::SubmitFailed)==Completion::Invalid&&!h.quarantined(),"stale completion cannot poison another transaction");
     check(h.observe_flow(retry.token,receipt(retry,f))==FlowDecision::Accumulate,"current token remains valid after stale callbacks");}
    {HistoryContract h;auto f=frame();f.contract.backend=Backend::None;check(h.prepare(f).action==Action::Invalid,"ordinary or missing MV cannot masquerade as AMD optical flow");
     f=frame();f.source.sequence=0;check(h.prepare(f).action==Action::Invalid,"missing real source is rejected");}
    {HistoryContract h;adopt(h,frame());auto f=frame(2);auto p=begin(h,f);auto r=receipt(p,f);r.status=FlowStatus::Failed;
     check(h.observe_flow(p.token,r)==FlowDecision::ResetRequired&&!h.has_history(),"provider refusal cannot retain a stale residual reference");
     adopt(h,frame(3));check(h.has_history(),"non-submission provider refusal recovers through a fresh pair reset");}
    std::printf("final residual history contract: %u checks passed; CPU policy only, no AMDOF/GPU implementation\n",checks);
}
