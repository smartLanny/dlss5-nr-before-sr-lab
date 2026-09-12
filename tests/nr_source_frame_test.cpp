#include "../src/nr_source_frame.hpp"
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <cstring>
#include <type_traits>
using namespace nr_source_frame;
static_assert(std::is_trivially_copyable_v<PresentObservation>,"Present snapshot remains a lock-copyable value");
// The validation runner inserts the unchanged production producer registry.
// No graphics interfaces are called by this registry; retire types are opaque.
struct NgxHandle {};
namespace reshade::api {struct swapchain {};}
namespace source_producers {
using namespace nr_source_frame;
std::atomic<Id> source_objects{1};Claims claims;Lineage lineage;
struct Lock {~Lock(){}};
// @SOURCE_PRODUCERS@
}
unsigned checks=0;
void Check(bool ok,const char* what){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}
Resource R(Id n,Id lifetime=1,Id device=1){return {n,lifetime,device,0};}
Recording C(Id n,Id reset=1,Id device=1){return {n,n,reset,device};}
Label App(Id frame=1,Id stream=1){return {{stream,1,frame},Role::Application};}
int main(){
    {Claims c;auto t=c.Begin(App(),Point::Before);Check(bool(t),"begin real source");
     Check(!c.Begin(App(),Point::After),"no duplicate before recorded");
     Check(c.RejectBeforeWork(t),"prework refusal releases claim");
     t=c.Begin(App(),Point::After);Check(bool(t)&&c.Record(t),"later point records once");
     Check(c.Record(t)&&c.recorded==1,"record acknowledgement idempotent");
     Check(!c.RejectBeforeWork(t),"recorded cannot be downgraded");
     Check(!c.Begin(App(2),Point::Present),"contradiction poisons only this producer");
     Check(bool(c.Begin(App(1,2),Point::Present)),"unrelated producer still works");}
    {Claims c;auto t=c.Begin(App(),Point::Before);c.Record(t);
     Check(!c.Complete(t,true,false),"API success is not fence completion");
     Check(c.Complete(t,true,true)&&c.completed==1,"real fence completes");
     Check(!c.Begin(App(),Point::Present),"completion does not release source");
     Check(!c.Complete(t,true,true),"late duplicate completion ignored");
     auto newer=c.Begin(App(2),Point::After);Check(bool(newer),"new source admitted");
     Check(c.RejectBeforeWork(newer),"new source rollback preserves old highwater");
     Check(!c.Begin(App(),Point::Present),"old frame stays consumed");}
    {auto exact=App();exact.role=Role::Unknown;exact.content=7;exact.exact_write=true;exact.submitted=true;exact.queue=10;
     Check(PresentPromotionEligible(exact,10),"exact unknown Present write is a promotion candidate");
     Check(PresentRejectMask(exact,10,false)==0,"unowned exact write can promote when no native producer exists");
     const auto blocked=PresentRejectMask(exact,10,true);
     Check((blocked&PresentRejectNativeProducer)!=0&&
           (blocked&PresentRejectNoApplication)!=0,"native producer blocks heuristic Present promotion");
     auto app=App();app.content=8;app.submitted=true;app.queue=10;
     Check(!PresentPromotionEligible(app,10)&&PresentRejectMask(app,10,false)==0,
           "known submitted application source bypasses heuristic promotion");
     app.submitted=false;Check((PresentRejectMask(app,10,false)&PresentRejectUnsubmitted)!=0,
           "unsubmitted application source is rejected");
     app.submitted=true;app.queue=11;Check((PresentRejectMask(app,10,false)&PresentRejectWrongQueue)!=0,
           "application source on another queue is rejected");
     app.queue=10;app.fg_consumed=true;Check((PresentRejectMask(app,10,false)&PresentRejectFgConsumed)!=0,
           "FG-consumed source is rejected at Present");}
    {PresentObservation snapshot{};snapshot.ordinal=1;snapshot.decision=PresentDecision::Accepted;snapshot.accepted=true;
     const auto copy=snapshot;Check(copy.ordinal==1&&copy.decision==PresentDecision::Accepted&&copy.accepted,
           "Present snapshot copies as a bounded value");
     for(unsigned i=0;i<=static_cast<unsigned>(PresentDecision::Accepted);++i)
         Check(std::strcmp(PresentDecisionName(static_cast<PresentDecision>(i)),"invalid")!=0,
               "Present decision enum has a name");
     Check(std::strcmp(PresentDecisionName(static_cast<PresentDecision>(static_cast<unsigned>(PresentDecision::Accepted)+1)),"invalid")==0,
           "Present decision enum rejects out-of-range values");}
    {Claims c;Check(!c.Begin({},Point::Present),"unknown source excluded");
     auto g=App();g.role=Role::Generated;Check(!c.Begin(g,Point::Present),"generated frames excluded");
     auto t=c.Begin(App(),Point::After);Check(!c.Complete(t,true,true),"completion without recorded poisons");}
    {Lineage l;Check(l.Set(C(1),R(1),App()),"publish native result");
     Check(l.Copy(C(2),R(2),R(1),true),"record copy before producer submitted");
     Check(l.Submit(C(1),10,true)&&l.Submit(C(2),10,true),"actual producer then consumer submission");
     auto v=l.Lookup(R(2));Check(v.KnownApplication()&&v.source.sequence==1&&v.submitted&&v.queue==10,"submit order propagates source");
     Check(l.Copy(C(3),R(3),R(2),true)&&l.Submit(C(3),11,true),"cross queue submitted");
     Check(!l.Lookup(R(3)).KnownApplication(),"cross queue without wait never inherits");
     Check(!l.Lookup(R(2,2)).KnownApplication(),"pointer reuse requires new lifetime");}
    {Lineage l;l.Set(C(1),R(1),App());l.Copy(C(2),R(2),R(1),true);
     l.Submit(C(2),10,true);l.Submit(C(1),10,true);
     Check(!l.Lookup(R(2)).KnownApplication(),"CPU order cannot repair reversed GPU dependency");}
    {Lineage l;auto generated=App();generated.role=Role::Generated;
     l.Set(C(1),R(1),generated);l.Copy(C(1),R(2),R(1),true);
     Check(l.Lookup(R(2),C(1)).role==Role::Generated,"same recording role before submission");
     l.Submit(C(1),10,true);Check(l.Lookup(R(2)).role==Role::Generated,"generated role survives copy");
     Check(!l.Submit(C(1),10,true)&&l.Lookup(R(2)).role==Role::Unknown,"list replay invalidates provenance");}
    {Lineage l;l.Invalidate(C(1),R(1));l.Submit(C(1),10,true);auto first=l.Lookup(R(1));
     Check(first.exact_write&&first.content&&!first.KnownApplication(),"exact output write is content not camera identity");
     Check(l.Lookup(R(1)).content==first.content,"repeat display has no new content generation");
     l.Copy(C(2),R(2),R(1),true);l.Submit(C(2),10,true);
     Check(l.Lookup(R(2)).content==first.content,"copy does not manufacture another image");
     l.InvalidateDevice(C(3));l.Invalidate(C(3),R(1));l.Submit(C(3),10,true);
     Check(!l.Lookup(R(2)).exact_write&&l.Lookup(R(1)).content>first.content,"unknown writes clear history then exact target creates content");}
    {Lineage l;l.Set(C(1),R(1),App());l.Submit(C(1),10,true);l.Reset(C(1,2));
     Check(l.Set(C(1,2),R(1),App(2))&&l.Submit(C(1,2),10,true),"successful reset permits new recording");
     l.RetireSource(1,1);Check(l.Lookup(R(1)).role==Role::Retired,"producer retirement prevents stale fallback");
     l.Forget(R(1));Check(!l.Lookup(R(1)).source,"resource retirement removes label");}
    {Lineage l;l.Set(C(1),R(1),App());l.Submit(C(1),10,true);
     for(Id i=0;i<60;++i)l.Copy(C(2),R(i+2),R(1),true);
     Check(!l.Lookup(R(1),C(2)).KnownApplication(),"overflow cannot use historical source during recording");
     Check(!l.Submit(C(2),10,true),"overflow submit is not proof");}
    {Lineage l;auto g=App();g.role=Role::Generated;l.DescribeSurface(R(1),3);
     l.Set(C(1),R(1),g);l.Submit(C(1),10,true);
     l.InvalidateDevice(C(2));l.Invalidate(C(2),R(1));
     auto pending=l.Lookup(R(1),C(2));Check(pending.tainted&&pending.encoding==3,"pending generated write retains taint and explicit encoding");
     l.Submit(C(2),10,true);auto v=l.Lookup(R(1));
     Check(v.role==Role::Unknown&&v.tainted&&v.exact_write&&v.encoding==3,"shader overwrite cannot wash generated into genuine source");
     l.Copy(C(3),R(2),R(1),true);l.Submit(C(3),10,true);
     Check(l.Lookup(R(2)).tainted&&l.Lookup(R(2)).encoding==3,"unknown generated lineage and encoding survive full copy");
     l.Clear(C(4),R(1));l.Submit(C(4),10,true);
     Check(!l.Lookup(R(1)).tainted&&l.Lookup(R(1)).encoding==3,"verified complete clear replaces all old pixels");}
    {Lineage l;auto a=App();a.processed=true;l.Set(C(1),R(1),a);l.Submit(C(1),10,true);
     l.InvalidateDevice(C(2));l.Invalidate(C(2),R(1));l.Submit(C(2),10,true);
     Check(l.Lookup(R(1)).tainted,"HUD added to already processed source cannot repeat NR");
     l.Set(C(3),R(1),App(2));l.Submit(C(3),10,true);
     Check(l.Lookup(R(1)).KnownApplication()&&!l.Lookup(R(1)).tainted,"authoritative new producer restores genuine source");}
    {Lineage l;auto g=App();g.role=Role::Generated;l.Set(C(1),R(1),g);l.Submit(C(1),10,true);
     l.Copy(C(2),R(2),R(1),true);l.Submit(C(2),11,true);
     Check(l.Lookup(R(2)).tainted&&!l.Lookup(R(2)).KnownApplication(),"unknown cross queue copy still retains generated taint");
     l.Submit(C(1),10,true);Check(l.Lookup(R(1)).tainted,"replayed generated recording cannot erase taint");}
    {Claims c;for(Id i=1;i<150;++i){auto t=c.Begin(App(1,i),Point::After);Check(bool(t)&&c.Record(t),"actual producer retirement recovers bounded capacity");
         c.RetainRecorded(t);Check(!c.Available(t.source)&&c.WasRecorded(t.source),"recorded highwater persists before producer retirement");c.RetireStream(i,1);}}
    {Claims c;c.ConsumeFg(App().source);
     Check(!c.Begin(App(),Point::Present)&&!c.Available(App().source),"successful FG input consumption prevents late real-only NR");
     Check(!c.WasRecorded(App().source)&&bool(c.Begin(App(2),Point::Present)),"FG consumption is not a fabricated NR success or permanent disable");
     Lineage l;auto a=App();a.fg_consumed=true;l.Set(C(1),R(1),a);l.Copy(C(1),R(2),R(1),true);l.Submit(C(1),10,true);
     Check(l.Lookup(R(2)).fg_consumed,"real FG output copy preserves consumption boundary");
     l.InvalidateDevice(C(2));l.Invalidate(C(2),R(2));l.Submit(C(2),10,true);
     Check(l.Lookup(R(2)).fg_consumed&&l.Lookup(R(2)).tainted,"post-FG HUD write cannot re-enable late NR");}
    {Claims c;auto ticket=c.Begin(App(),Point::Present);c.ConsumeFg(App().source);
     Check(c.RejectBeforeWork(ticket)&&!c.Available(App().source),"unrecorded pre-FG attempt cannot roll back successful original FG consumption");
     Check(!c.WasRecorded(App().source)&&bool(c.Begin(App(2),Point::Present)),"no private GPU work still leaves next genuine FG source eligible");}
    {Lineage l;l.DescribeSurface(R(1),1);for(unsigned i=0;i<4000;++i){l.InvalidateDevice(C(1));l.Invalidate(C(1),R(1));}
     Check(l.TracksList(1,1)&&!l.TracksList(9,9),"unobserved queue lists do not need COM inspection");
     Check(l.Submit(C(1),10,true)&&l.Lookup(R(1)).exact_write,"thousands of ordinary draws fit bounded receipt storage");
     l.ForgetAddress(1);Check(!l.Lookup(R(1)).content,"destroy resource retires source metadata without COM calls");
     l.ForgetList(1);Check(!l.TracksList(1,1),"actual list destruction recovers receipt capacity");}
    {using namespace source_producers;
     ObserveNativeProducer(11,2,7);auto first=ProducerSource(11,2,1);auto token=RetryToken(first);
     Check(bool(first)&&token&&RetryToken(first)==token,"production duplicate source retains retry identity");
     auto view=ProducerSource(12,2,1);auto view_token=RetryToken(view);
     Check(view_token&&view_token!=token,"multiple view/producer frame one does not collide");
     Check(NativeProducerOnDevice(7)&&!NativeProducerOnDevice(8),"native source ambiguity guard is per device");
     for(Id sequence=2;sequence<1024;++sequence){auto current=ProducerSource(11,2,sequence);auto retry=RetryToken(current);
         if(!retry||RetryToken(current)!=retry)Check(false,"production retry cache remains stable under bounded turnover");}
     Check(RetryToken(first)==0,"evicted old source cannot manufacture a fresh attempt");
     Check(RetryToken(view)==view_token,"other view retry identity survives stream churn");
     RetireProducer(11,2);Check(!NativeProducerOnDevice(7)&&!ProducerActive(first),"actual feature retirement releases native ambiguity guard");
     auto replacement=ProducerSource(11,3,1);Check(replacement.stream!=first.stream&&RetryToken(replacement)!=token,"feature pointer reuse gets a new source and retry identity");}
    {QueueLease q{1,2,2,3,4,0,true,true,true};
     Check(q.Admitted()&&!q.CanReuse(),"same queue order is distinct from CPU reuse");q.completed=4;
     Check(q.CanReuse(),"exact completed fence permits reuse");q.completed=UINT64_MAX;
     Check(!q.Admitted()&&!q.CanReuse(),"device removal fence sentinel rejected");q.completed=5;q.consumer_queue=9;
     Check(!q.Admitted(),"unproven cross queue rejected");q.consumer_queue=2;q.value=0;
     Check(!q.CanReuse(),"zero fence cannot retire");}
    std::printf("source-frame: %u checks passed\n",checks);
}
