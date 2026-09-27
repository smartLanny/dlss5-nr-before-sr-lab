#include "nr_acceptance_policy.h"
#include <cstdio>
#include <cstdlib>
using namespace nr_acceptance_policy;
static unsigned checks=0;
void check(bool c,const char* why){++checks;if(!c){std::printf("FAIL %s\n",why);std::exit(41);}}
int main(){
    check(Count(Program::Colour)==6&&Count(Program::Temporal)==5&&Count(Program::PrivateSr)==3&&Count(Program::LowFrequency)==2,"bounded programs");
    check(Get(Program::Colour,2,true).percent==50&&Get(Program::Colour,2,false).percent==25,"route-specific minimum");
    check(Get(Program::Temporal,3,false).scope==1&&Get(Program::Temporal,4,false).backend==2,"later scope does not enable more layers");
    check(Get(Program::Colour,4,false).colour==5,"current colour midpoint");
    check(Get(Program::PrivateSr,0,true).percent==50&&Get(Program::PrivateSr,1,false).percent==25&&Get(Program::PrivateSr,1,false).private_sr==1&&Get(Program::PrivateSr,2,false).percent==100,"private SR fair comparisons");
    check(Get(Program::LowFrequency,0,false).private_sr==0&&Get(Program::LowFrequency,0,false).temporal_mode==0&&Get(Program::LowFrequency,1,false).temporal_mode==1,"mutually exclusive temporal comparisons");
    Window w;w.Start(1000);Observation o{true,true,false,true,100};
    check(!w.Tick(4000,o)&&w.phase==Phase::Warmup,"Evaluate without adoption does not begin measurement");
    o.adopted=true;o.temporal=false;check(!w.Tick(5000,o),"unavailable temporal does not look active");
    o.temporal=true;o.since_reset=63;check(!w.Tick(6000,o),"post-reset warmup remains real");
    o.since_reset=64;check(w.Tick(6001,o)&&w.phase==Phase::Measure,"eligible actual state begins timing");
    check(!w.Tick(26000,o),"full timing window");
    check(w.Tick(26001,o)&&w.phase==Phase::Report,"report starts after measurement");
    w.Start(0);o.matches=false;check(w.Tick(20000,o)&&w.refusal,"missing route is refused");
    w.Start(0);o={true,false,false,true,0};check(w.Tick(3000,o),"OFF baseline needs no fake NR success");
    o.matches=false;check(w.Tick(3001,o)&&w.refusal,"mid-window configuration change terminates segment");
    check(!CountersAdvanced(10,9)&&!CountersAdvanced(10,10)&&CountersAdvanced(10,11),"monotonic adoption only");
    std::printf("acceptance policy: %u checks\n",checks);
}
