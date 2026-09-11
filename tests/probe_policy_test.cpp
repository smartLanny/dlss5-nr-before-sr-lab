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
    Check(control.Change(Mode::OutputTransitionsOnly),"state-only comparison accepted");
    Check(control.Read().phase==3 && control.Read().OutputTransitionsOnly() &&
          control.Read().ScheduleCheckpoint() && !control.Read().AutomaticSampling(),
          "state-only comparison schedules barriers without enabling automatic reads");
    Check(!control.Change(Mode::OutputTransitionsOnly) && control.Read().phase==3,"state-only reread preserves phase");
    Check(!control.Change(static_cast<Mode>(3)),"unused packed mode is rejected");
    Control concurrent;
    std::atomic<bool> done{false};
    std::thread writer([&]{for(unsigned i=0;i<20000;++i)concurrent.Change(static_cast<Mode>((i+1)%3));done.store(true);});
    while(!done.load()){
        const auto snapshot=concurrent.Read();
        Check((snapshot.phase-1)%3==static_cast<unsigned>(snapshot.mode),
              "concurrent feedback cannot pair mode with a different phase");
    }
    writer.join();
    Control restarted;
    Check(restarted.Read().phase==1 && restarted.Read().AutomaticSampling(),"session experiment is not persistent");
    std::puts("PASS process-local probe phase identity and concurrent snapshots");
}
