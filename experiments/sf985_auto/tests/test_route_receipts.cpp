#include "../src/nr_auto_dispatch.hpp"
#include <cstdlib>
#include <iostream>
using namespace nr_auto_injection;
static unsigned n=0;
#define CHECK(x) do {++n;if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::exit(1);}}while(0)
Frame F(unsigned long long sequence=1) {
    Frame f;f.stream={1,1,1,7,1,1};f.source=sequence;f.source_known=f.application_frame=true;return f;
}
std::array<Candidate,4> C() {
    std::array<Candidate,4> c{};
    for(unsigned i=1;i<=3;++i){auto& x=c[i];x.registered=x.input_current=x.resource_valid=
        x.same_device=x.queue_ordered=x.unique_owner=x.inline_state_restorable=
        x.native_guides=x.app_frame_proven=x.fg_contract_proven=true;x.independent_recording=i==3;}
    return c;
}
int main(){
    auto c=C();auto f=F();
    // Releasing a claim after recording must never allow another NR at Present.
    {Policy p;auto r=p.Begin(f,p.Choose(f,c));CHECK(bool(r));p.Recorded(r);
      p.RejectBeforeWork(r,Fault::CommandState);CHECK(p.Read().poisoned);
      CHECK(!p.Choose(f,c));auto newer=F(2);newer.stream.config=2;CHECK(!p.Choose(newer,c));}
    // A success without a record receipt is not evidence of a submitted result.
    {Policy p;auto r=p.Begin(f,p.Choose(f,c));p.Complete(r,true,true);
      CHECK(p.Read().poisoned);CHECK(p.Read().completed==0);}
    // Legitimate pre-work refusal may transfer ownership to a later boundary.
    {Policy p;auto r=p.Begin(f,p.Choose(f,c));p.RejectBeforeWork(r,Fault::CommandState);
      auto bad=c;bad[1].blocked=Fault::CommandState;auto d=p.Choose(f,bad);
      CHECK(d.point==Point::After);auto r2=p.Begin(f,d);CHECK(bool(r2));p.Recorded(r2);
      p.Complete(r2,true,true);CHECK(p.Read().confirmed_source==1);}
    // Latest selected source and last completed source are distinct fields.
    {Policy p;auto a=p.Begin(F(1),p.Choose(F(1),c));p.Recorded(a);
      auto b=p.Begin(F(2),p.Choose(F(2),c));p.Recorded(b);p.Complete(a,true,true);
      CHECK(p.Read().source==2);CHECK(p.Read().confirmed_source==1);
      p.Complete(b,true,true);CHECK(p.Read().confirmed_source==2);}
    // Late completion delivery cannot roll an already confirmed source backward.
    {Policy p;auto a=p.Begin(F(1),p.Choose(F(1),c));p.Recorded(a);
      auto b=p.Begin(F(2),p.Choose(F(2),c));p.Recorded(b);p.Complete(b,true,true);
      const auto before=p.Read().completed;p.Complete(a,true,true);
      CHECK(p.Read().confirmed_source==2);CHECK(p.Read().completed==before);}
    // Before and Present must share a nonzero, stable presentation identity.
    {Policy p;auto unknown=f;unknown.stream.swapchain=0;CHECK(!p.Choose(unknown,c));
      auto r=p.Begin(f,p.Choose(f,c));CHECK(bool(r));p.Recorded(r);
      auto edit=f;edit.stream.config=9;CHECK(!p.Choose(edit,c));}
    {Policy p;auto both=c;both[1].synthetic_guides=both[1].allow_synthetic=true;
      CHECK(!p.Choose(f,both).synthetic);}
    {Dispatcher d;for(unsigned raw=4;raw<100;++raw){const auto invalid=static_cast<Point>(raw);
      d.Set(invalid,{});auto result=d.At(invalid,f);CHECK(result.kind==DispatchResult::Kind::Native);}
      CHECK(d.At(Point::None,f).kind==DispatchResult::Kind::Native);}
    {Policy p;for(unsigned raw=5;raw<100;++raw){f.request=static_cast<Request>(raw);CHECK(!p.Choose(f,c));}
      p.ProvenConsumerOverwrite(static_cast<Point>(99));CHECK(!p.Read().poisoned);}
    // An opaque failure may occur before Recorded() can be published: poison still wins.
    {Policy p;auto ff=F();auto r=p.Begin(ff,p.Choose(ff,c));p.Complete(r,false,false,Fault::OpaqueCall);
      CHECK(p.Read().poisoned);CHECK(p.Read().reason==Fault::OpaqueCall);}
    std::cout<<"route_receipt_checks="<<n<<"\n";
}
