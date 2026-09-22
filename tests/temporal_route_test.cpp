#include "../src/nr_temporal_route.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
using namespace nr_temporal_route;
unsigned checks=0;
void Check(bool ok,const char* name){++checks;if(!ok){std::printf("FAIL %s\n",name);std::exit(1);}}
int main(){
    Evidence pre{1,2,2,0,true,false,true,false,true};
    Check(Admit(Entry::PreSr,pre)==Decision::Supported,"actual pre SDR whole-chain AMD contract");
    auto e=pre;e.scope=1;Check(Admit(Entry::PreSr,e)==Decision::Scope,"P1 must not be inferred from post descriptor layout");
    e=pre;e.backend=1;Check(Admit(Entry::PreSr,e)==Decision::Motion,"copied MV is not pre-SR jitter phase proof");
    e=pre;e.packed_original=false;Check(Admit(Entry::PreSr,e)==Decision::Route,"atlas/subrect requires a real packed B producer");
    Evidence rr{13,1,2,0,true,false,true,true,true};
    Check(Admit(Entry::PostRr,rr)==Decision::Supported,"successful native RR output AMD route");
    e=rr;e.scope=1;Check(Admit(Entry::PostRr,e)==Decision::Supported,"RR post shares actual composed P1 seam");
    e=rr;e.backend=1;Check(Admit(Entry::PostRr,e)==Decision::Motion,"RR fallback MV cannot become native temporal proof");
    Check(Admit(Entry::PostSr,rr)==Decision::Route,"changing Feature ID cannot widen original consumer");
    Check(Admit(Entry::PostRr,pre)==Decision::Route,"pre source cannot pretend native RR completion");
    for(auto entry:{Entry::PreSr,Entry::PostRr}){
        auto good=entry==Entry::PreSr?pre:rr;
        e=good;e.hdr=true;Check(Admit(entry,e)==Decision::Domain,"HDR refused before allocation");
        e=good;e.sdr_codec=false;Check(Admit(entry,e)==Decision::Domain,"PQ/linear/model domains not relabeled SDR");
        e=good;e.flags_known=false;Check(Admit(entry,e)==Decision::Domain,"unknown flags cannot prove SDR");
        e=good;e.feature=18;Check(Admit(entry,e)==Decision::Route,"private NR is not native source");
        e=good;e.mode=0;Check(Admit(entry,e)==Decision::Route,"Present cannot enter native seam");
    }
    Evidence sr{1,1,1,1,false,false,true,true,true};
    Check(Admit(Entry::PostSr,sr)==Decision::Supported,"existing post SDR route preserved");
    std::printf("PASS temporal route contracts checks=%u; CPU only\n",checks);
}
