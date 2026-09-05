#include "nr_command_evidence.h"
#include <cstdlib>
#include <limits>
#include <string>
using namespace nr_command_evidence;
unsigned checks=0;
void Check(bool value,const char *why){++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
Event Make(const char *reason,bool admitted,std::uint64_t tick){Event e{};Identifier(e.reason,sizeof(e.reason),reason);e.admitted=admitted;e.tick=tick;return e;}
int main(){
    auto *s=new State{};auto *text=new char[kTextCapacity];
    Check(Format(*s,text,kTextCapacity),"no observations is representable");
    Check(std::strstr(text,"commandEvidence.total=0")!=nullptr,"no observations does not invent successful NR");
    Event blocked=Make("null-pso-policy-disabled",false,100);blocked.token=112;blocked.baseline=1;blocked.reset=83728;blocked.registered=blocked.hooks=blocked.known=true;blocked.meta_known=blocked.meta_covered=true;blocked.null_baseline=true;
    Observe(*s,blocked);
    for(unsigned i=0;i<50000;++i){blocked.tick++;blocked.reset++;Observe(*s,blocked);}
    Check(s->first_count==1 && s->first[0].count==50001,"same bypass is counted without unbounded entries");
    Check(s->first[0].first.reset==83728 && s->first[0].latest.reset==133728,"first and last reset evidence retained");
    Check(s->recent_count==1 && s->transitions==1,"repeated bypass is not a fake reason transition");
    Observe(*s,Make("admitted",true,60000));
    Check(s->recoveries==1 && s->admitted==1 && s->blocked==50001,"gate recovery differs from GPU completion");
    for(unsigned i=0;i<1000;++i)Observe(*s,Make(i%2?"native-hooks-unavailable":"admitted",i%2==0,70000+i));
    Check(s->recent_count==kRecentCount && s->first_count==3,"transition ring remains bounded");
    Check(Format(*s,text,kTextCapacity),"ordinary evidence formatted");
    Check(std::strstr(text,"reason:null-pso-policy-disabled")!=nullptr,"first null PSO survives later noise");
    Check(std::strstr(text,"extra graphics setters are NOT observed")!=nullptr,"unobserved dynamic setters not mistaken for unused");
    ObserveHookFailure(*s,"create",3,7,23,10);ObserveHookFailure(*s,"apply",10,23,23,20);
    Check(s->first_hook.status==3 && s->latest_hook.status==10 && s->hook_failures==2,"first and latest hook failure preserved");
    Check(Format(*s,text,kTextCapacity) && std::strstr(text,"stage:apply status:10"),"hook failure has phase, numeric status and request index");
    for(unsigned i=0;i<100;++i){auto reason="reason-"+std::to_string(i);Observe(*s,Make(reason.c_str(),false,i));}
    Check(s->first_count==kFirstCount && s->first_overflow>0,"distinct reason overflow disclosed");
    Check(Format(*s,text,kTextCapacity),"bounded distinct reasons formatted");
    char tiny[8]={'x','x','x','x','x','x','x','x'};
    Check(!Format(*s,tiny,sizeof(tiny)) && tiny[0]==0,"truncation cannot return stale report");
    Check(!Format(*s,nullptr,100) && !Format(*s,tiny,0),"invalid destination rejected");
    Event unsafe=Make("C:\\Users\\Private Name\n",false,1);
    Check(!std::strchr(unsafe.reason,'\\') && !std::strchr(unsafe.reason,'\n') && !std::strchr(unsafe.reason,':'),"unexpected reason cannot inject log syntax or a path");
    *s={};
    for(unsigned i=0;i<32;++i){std::string reason(79,'a');reason[0]=char('A'+i%26);reason[1]=char('A'+i/26);auto e=Make(reason.c_str(),false,UINT64_MAX);e.token=e.reset=e.pipeline_writes=e.dynamic=UINT64_MAX;e.dirty=e.meta_init=e.meta_execute=e.bindings=UINT32_MAX;Observe(*s,e);}
    Check(Format(*s,text,kTextCapacity),"maximum bounded identifiers and counters fit report allocation");
    Check(std::strlen(text)<kTextCapacity,"physical report bound");
    delete[] text;delete s;
    std::printf("command evidence policies: %u checks passed; no GPU or admission mutations\n",checks);
}
