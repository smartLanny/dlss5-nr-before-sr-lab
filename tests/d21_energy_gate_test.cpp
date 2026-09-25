#include "../src/nr_energy_gate.h"
#include <cstdlib>
#include <iostream>
using namespace nr_energy_gate;
static void Check(bool value){if(!value)std::abort();}
int main(){
    Options safe{true,false};
    State s{};
    auto r=Observe(s,safe,true,true,10,100);
    Check(!r.state.Active()&&r.state.explicit_samples==1);
    r=Observe(r.state,safe,true,true,10,108);
    Check(!r.state.Active());
    r=Observe(r.state,safe,true,true,10,116);
    Check(r.state.Active()&&r.state.ActiveCause()==Cause::Explicit&&r.transition==Transition::PausedExplicit);
    r=Observe(r.state,safe,true,false,11,120);
    Check(!r.state.Active()&&r.transition==Transition::Resumed);

    // Conservative default does nothing when the public pause key is missing.
    r=Observe({},safe,false,false,20,200);
    r=Observe(r.state,safe,false,false,20,800);
    Check(!r.state.Active());

    Options guess{true,true};
    // Guess requires a stable public FG frame ID for both time and sample count.
    r=Observe({},guess,false,false,30,1000);
    r=Observe(r.state,guess,false,false,30,1100);
    r=Observe(r.state,guess,false,false,30,1200);
    Check(!r.state.Active());
    r=Observe(r.state,guess,false,false,30,1300);
    Check(r.state.Active()&&r.state.ActiveCause()==Cause::Heuristic&&r.transition==Transition::PausedHeuristic);
    // A moving source frame immediately cancels the guess.
    r=Observe(r.state,guess,false,false,31,1310);
    Check(!r.state.Active()&&r.transition==Transition::Resumed);

    // An authoritative false always overrides a heuristic pause.
    r=Observe({},guess,false,false,40,2000);
    r=Observe(r.state,guess,false,false,40,2100);
    r=Observe(r.state,guess,false,false,40,2200);
    r=Observe(r.state,guess,false,false,40,2300);
    Check(r.state.heuristic_active);
    r=Observe(r.state,guess,true,false,40,2301);
    Check(!r.state.Active()&&r.transition==Transition::Resumed);

    // A known true never falls through to guess mode when conservative is off.
    Options guess_only{false,true};
    r=Observe({},guess_only,true,true,50,3000);
    r=Observe(r.state,guess_only,true,true,50,3500);
    Check(!r.state.Active()&&!r.state.heuristic_active);

    // Active evidence eventually fails open and asks runtime for a history reset.
    r=Observe({},safe,true,true,60,4000);
    r=Observe(r.state,safe,true,true,60,4020);
    Check(r.state.explicit_active);
    r=Poll(r.state,safe,4020+kEvidenceMaxAgeMs+1);
    Check(!r.state.Active()&&r.transition==Transition::Stale);

    // Disabling the currently active mechanism also reopens immediately.
    r=Observe({},safe,true,true,70,5000);
    r=Observe(r.state,safe,true,true,70,5020);
    Check(r.state.explicit_active);
    r=Poll(r.state,{false,false},5021);
    Check(!r.state.Active()&&r.transition==Transition::Disabled);

    // Clock rollback invalidates an active lease instead of stretching it.
    r=Observe({},safe,true,true,80,6000);
    r=Observe(r.state,safe,true,true,80,6020);
    r=Poll(r.state,safe,5900);
    Check(!r.state.Active()&&r.transition==Transition::Stale);
    // Missing evidence breaks a pending explicit confirmation sequence.
    r=Observe({},safe,true,true,90,7000);
    r=Observe(r.state,safe,false,false,90,7010);
    r=Observe(r.state,safe,true,true,90,7040);
    Check(!r.state.Active()&&r.state.explicit_samples==1);

    Streams streams{};
    const StreamKey a{1,10,2},b{1,11,2},other_device{2,10,2},new_epoch{1,10,3};
    unsigned resets=0;
    auto transition=[&](Transition t){
        if(t==Transition::Resumed||t==Transition::Stale||t==Transition::Disabled) {
            // The history callback must run before the active entry is cleared.
            Check(streams.AnyActive());++resets;
        }
    };
    streams.ObserveStream(a,100,3,safe,true,true,1,8000,transition);
    streams.ObserveStream(a,100,3,safe,true,true,1,8020,transition);
    Check(streams.Blocked(a,safe,8020,transition));
    Check(!streams.Blocked(b,safe,8020,transition));
    Check(!streams.Blocked(other_device,safe,8020,transition));
    Check(!streams.Blocked(new_epoch,safe,8020,transition));
    // A false from a different viewport cannot reopen the paused stream.
    streams.ObserveStream(b,200,3,safe,true,false,1,8021,transition);
    Check(streams.Blocked(a,safe,8021,transition)&&resets==0);
    // Recycled observer pointer with a new generation starts unpaused.
    streams.ObserveStream(a,100,4,safe,true,true,1,8040,transition);
    Check(!streams.AnyActive()&&resets==1);
    streams.ObserveStream(a,100,4,safe,true,true,1,8060,transition);
    streams.Retire(100,3,transition);Check(streams.AnyActive());
    streams.Retire(100,4,transition);Check(!streams.AnyActive()&&resets==2);

    // Same-tick repeated Evaluate is not four independent guess observations.
    streams.ObserveStream(a,100,4,guess,false,false,1,9000,transition);
    for(unsigned i=0;i<8;++i)streams.ObserveStream(a,100,4,guess,false,false,1,9300,transition);
    Check(!streams.AnyActive());
    streams.ObserveStream(a,100,4,guess,false,false,1,9310,transition);
    streams.ObserveStream(a,100,4,guess,false,false,1,9320,transition);
    Check(streams.AnyActive());
    streams.ObserveStream(a,100,4,guess,true,false,1,9320,transition);
    Check(!streams.AnyActive()&&resets==3);

    // Bounded capacity must never evict another actively paused stream.
    for(unsigned i=0;i<16;++i) {
        const StreamKey key{1,100+i,1};
        streams.ObserveStream(key,200+i,1,safe,true,true,1,10000,transition);
        streams.ObserveStream(key,200+i,1,safe,true,true,1,10020,transition);
    }
    streams.ObserveStream(a,100,4,safe,true,true,1,10000,transition);
    streams.ObserveStream(a,100,4,safe,true,true,1,10020,transition);
    Check(!streams.Blocked(a,safe,10020,transition));
    for(unsigned i=0;i<16;++i)Check(streams.Blocked({1,100+i,1},safe,10020,transition));
    streams.PollAll(safe,13000,transition);
    Check(!streams.AnyActive()&&resets==19);
    std::cout<<"PASS: original two-mode policy, scoped streams, reset-before-reopen, generation retirement, duplicate observations and bounded capacity\n";
    return 0;
}
