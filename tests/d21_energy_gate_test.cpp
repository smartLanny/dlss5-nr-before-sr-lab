#include "../src/nr_energy_gate.h"
#include <cstdlib>
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
    return 0;
}
