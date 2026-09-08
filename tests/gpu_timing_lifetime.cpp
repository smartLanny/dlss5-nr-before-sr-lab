#include "../src/nr_gpu_timing.h"
#include <cstdio>
#include <cstdlib>
static void Check(bool ok,const char *message){if(!ok){std::fprintf(stderr,"FAIL %s\n",message);std::exit(1);}}
int main(){
    using nr_gpu_timing::Lifetime;
    Lifetime a;a.Record(11);Check(a.Submit(1,2,5),"first real submission");
    Check(!a.Retirable(5),"completed live list can still replay");
    a.Reset(false);Check(!a.Retirable(4),"reset does not fabricate completion");
    Check(!a.Retirable(UINT64_MAX),"device removal is not completion");
    Check(a.Retirable(5),"exact fence and dead recording retire");
    Lifetime b;b.Record(12);b.Submit(1,2,6);b.Submit(1,2,9);b.Reset(false);
    Check(b.replayed&&!b.Retirable(8)&&b.Retirable(9),"replay waits newest exact fence and invalidates measurement");
    Lifetime c;c.Record(13);c.Submit(1,2,1);Check(!c.Submit(3,4,2),"cross queue cannot replace proof");c.Reset(false);Check(!c.Retirable(100),"unknown submission stays pinned");
    Lifetime d;d.Record(14);d.Reset(false);Check(!d.Retirable(0),"unknown unsubmitted reset stays pinned");
    Lifetime e;e.Record(15);e.Reset(true);Check(e.Retirable(0),"proven discarded query recording can retire");
    Lifetime f;f.Record(16);Check(!f.Submit(0,2,1),"missing queue rejected");f.Reset(true);Check(!f.Retirable(100),"failed proof cannot be rescued by reset");
    Lifetime old,fresh;old.Record(17);fresh.Record(18);old.Submit(1,2,10);fresh.Submit(1,2,11);old.Reset(false);
    Check(old.Retirable(10)&&!fresh.Retirable(11),"new recording identity independent of reused list address");
    std::puts("PASS GPU timing recording lifetime, replay, reset, fence and device-loss scenarios");
}
