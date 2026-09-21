#include "../src/nr_reset_observation.h"
#include <cstdlib>
#include <cstdio>
static void check(bool condition,const char* message){if(!condition){std::fprintf(stderr,"FAIL %s\n",message);std::exit(1);}}
int main(){
    nr_reset_observation::Counters c;
    nr_reset_observation::Sample s{1,3,1,0,960,544,8|64,1,1.f,false,7,2,90,2,4};
    c.Observe(s);c.Observe(s);
    check(c.layers[0].evaluations==1,"duplicate evaluation is not a second sample");
    check(c.layers[0].reasons[3]==1&&c.layers[0].reasons[6]==1,"combined diagnostic reasons are counted by bit");
    check(c.layers[0].delayed_exposure==0,"RR placeholder bit64 is not exposure readback");
    ++s.evaluation;s.reasons=8;s.delayed_exposure=true;c.Observe(s);
    check(c.layers[0].delayed_exposure==1&&c.layers[0].resets==2,"explicit delayed exposure attributed separately");
    ++s.evaluation;s.reasons=0;s.delayed_exposure=false;s.since_reset=2;c.Observe(s);
    check(c.layers[0].evaluations==3&&c.reset_events==2,"non-reset success counts without filling event ring");
    s.layer=4;s.instance=2;s.reasons=4;
    for(unsigned i=0;i<40;++i){s.evaluation=1+i;s.source_frame=100+i;c.Observe(s);}
    check(c.layers[4].evaluations==40&&c.layers[0].evaluations==3,"independent fifth layer");
    check(c.reset_events==42&&c.recent_resets[41%32].source_frame==139,"bounded ring retains exact last source identity");
    s.layer=5;c.Observe(s);check(c.reset_events==42,"invalid layer cannot corrupt counters");
    std::puts("reset observation: bitmask, schema separation, layers, source identity and bounded history PASS");
}
