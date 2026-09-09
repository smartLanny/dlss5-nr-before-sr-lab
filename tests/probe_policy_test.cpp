#include "nr_probe_policy.h"
#include <cstdio>
#include <cstdlib>
#include <thread>

static void Check(bool ok,const char *reason) {
    if(!ok){std::fprintf(stderr,"FAIL %s\n",reason);std::exit(1);}
}
int main() {
    using namespace nr_probe_policy;
    Control control;
    Check(control.Read().phase==1 && control.Read().AutomaticSampling(),"new session is normal");
    Check(!control.Change(Mode::Normal) && control.Read().phase==1,"UI reread is not a new phase");
    const auto recording=control.Read();
    Check(control.Change(Mode::Paused),"pause accepted");
    Check(control.Read().phase==2 && !control.Read().AutomaticSampling(),"pause has distinct phase");
    Check(recording.phase==1 && recording.AutomaticSampling(),"pending recording retains its old identity");
    Check(!control.Change(static_cast<Mode>(9)) && control.Read().phase==2,"unknown policy is rejected");
    std::atomic<bool> done{false};
    std::thread writer([&]{for(unsigned i=0;i<20000;++i)control.Change(i%2?Mode::Paused:Mode::Normal);done.store(true);});
    while(!done.load()){
        const auto snapshot=control.Read();
        Check((snapshot.phase&1)==static_cast<unsigned>(snapshot.AutomaticSampling()),
              "concurrent feedback cannot pair mode with a different phase");
    }
    writer.join();
    Control restarted;
    Check(restarted.Read().phase==1 && restarted.Read().AutomaticSampling(),"session experiment is not persistent");
    std::puts("PASS process-local probe phase identity and concurrent snapshots");
}
