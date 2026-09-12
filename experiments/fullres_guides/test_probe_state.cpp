#include "probe_state.hpp"
#include <cstdlib>
#include <iostream>
using namespace nr::fullres;
static unsigned count=0;
static void check(bool ok,const char* what){++count;
        if(!ok){std::cerr<<"FAIL: "<<what<<'\n';std::exit(1);}}
int main(){
    Frame source{1,2,10,{1921,1081},false}, view{1,2,14,{1921,1081},false};
    check(preview_matches(source,view),"completed older same-generation preview allowed");
    for(int i=0;i<7;++i){auto bad=source;
        if(i==0)++bad.stream;
        if(i==1)++bad.epoch;
        if(i==2)++bad.extent.width;
        if(i==3)++bad.extent.height;
        if(i==4)bad.sequence=view.sequence;
        if(i==5)bad.sequence=view.sequence+1;
        if(i==6)bad={};
        check(!preview_matches(bad,view),"foreign/reset/future preview rejected");
    }
    check(!preview_matches(source,{}),"invalid consumer rejected");
    check(!preview_matches(source,Frame{1,3,15,source.extent,false}),"toggle/reload generation rejects old result");
    check(fence_progress(1,0,100,5099,5000)==Progress::pending,"before deadline stays pending");
    check(fence_progress(1,0,100,5100,5000)==Progress::timed_out,"deadline latches failure");
    check(fence_progress(1,1,100,9000,5000)==Progress::ready,"completed work is not timed out");
    check(fence_progress(1,UINT64_MAX,100,101,5000)==Progress::removed,"device loss not an endless wait");
    check(fence_progress(UINT64_MAX,0,100,101,5000)==Progress::removed,"reserved target refused");
    check(fence_progress(0,0,0,99999,5000)==Progress::ready,"no submission never times out");
    check(fence_progress(1,0,100,99,5000)==Progress::pending,"clock anomaly no underflow timeout");
    for(int e=0;e<2;++e)for(int c=0;c<2;++c)for(int f=0;f<2;++f)
        check(capture_enabled(e!=0,c!=0,f!=0)==(e&&c&&!f),"explicit consent gate");
    std::cout<<"PASS "<<count<<" preview/timeout/consent assertions (no GPU)\n";
}
