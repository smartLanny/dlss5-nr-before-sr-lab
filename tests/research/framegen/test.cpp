#include "research/framegen_control.h"
#include <cstdio>
using namespace nr::research::fg;
static int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(false)
static Caps Good() {
    Caps c; c.backend=Backend::NativeDlssG; c.generation=1;
    c.maxGenerated=3; c.verifiedMaxMultiplier=4;
    c.available=c.ownerKnown=c.runtimeValid=c.pacingValid=c.inputsValid=true;
    c.liveControl=c.releaseSupported=c.disableSupported=true; return c;
}
int main() {
    auto c=Good(); Conditions s; s.offlineAllowed=s.atSafeBoundary=true;
    Request r{Mode::Fixed,4,0}; auto d=Decide(r,c,s);
    CHECK(d.action==Action::Apply && d.generated==3 && d.multiplier==4);
    c.maxGenerated=1; d=Decide(r,c,s);
    CHECK(d.multiplier==2 && d.reason==Reason::Limited);
    c=Good(); c.verifiedMaxMultiplier=3; CHECK(Decide(r,c,s).multiplier==3);
    c=Good(); c.maxGenerated=0xffffffffu; CHECK(Decide(r,c,s).reason==Reason::InvalidCaps);
    c=Good(); c.backend=Backend::ExternalAda;
    CHECK(Decide(r,c,s).reason==Reason::ExternalUnverified);
    c.externalAbiVerified=c.externalTimingVerified=true;
    CHECK(Decide(r,c,s).action==Action::Apply);
    r.mode=Mode::Dynamic; CHECK(Decide(r,c,s).reason==Reason::DynamicUnsupported);
    c.dynamicSupported=true; d=Decide(r,c,s);
    CHECK(d.mode==Mode::Dynamic && d.generated==0 && d.targetFps==0);
    c.backend=Backend::Fsr; CHECK(Decide(r,c,s).reason==Reason::DynamicUnsupported);
    c=Good(); r={Mode::Fixed,4,0}; s.captureLocked=true;
    CHECK(Decide(r,c,s).reason==Reason::CaptureLocked);
    s.controlOwned=true; s.transition=true;
    CHECK(Decide(r,c,s).action==Action::Suspend);
    s.captureLocked=s.transition=false; c.liveControl=false;
    CHECK(Decide(r,c,s).reason==Reason::RestartRequired);
    c=Good(); s.atSafeBoundary=false; CHECK(Decide(r,c,s).reason==Reason::UnsafeBoundary);
    s.atSafeBoundary=true; r.mode=Mode::FollowGame;
    CHECK(Decide(r,c,s).action==Action::Release);
    s.controlOwned=false; CHECK(Decide(r,c,s).action==Action::None);
    r.mode=Mode::Off; c.disableSupported=false;
    CHECK(Decide(r,c,s).reason==Reason::DisableUnsupported);
    c=Good(); r={Mode::Fixed,4,0}; s.controlOwned=true; s.conflict=true;
    CHECK(Decide(r,c,s).action==Action::Release);
    c.ownerKnown=false; CHECK(Decide(r,c,s).reason==Reason::UnknownOwner);
    s.conflict=false; c=Good(); d=Decide(r,c,s); ChangeGate gate;
    auto first=gate.Begin(d,1,1); CHECK(first!=0);
    CHECK(gate.Begin(d,1,1)==0);
    CHECK(!gate.Acknowledge(first+1,true));
    CHECK(gate.Acknowledge(first,false) && !gate.Accepted());
    CHECK(gate.Begin(d,1,1)==0);
    gate.Retry(); auto second=gate.Begin(d,1,1); CHECK(second>first);
    CHECK(!gate.Acknowledge(first,true)); CHECK(gate.Acknowledge(second,true));
    CHECK(!gate.Observed()); CHECK(!gate.Observe(1,2,d));
    CHECK(gate.Observe(1,1,d)); gate.Invalidate(); CHECK(!gate.Observed());
    for (std::uint32_t maximum=1;maximum<=31;++maximum) {
        c=Good(); c.maxGenerated=maximum; c.verifiedMaxMultiplier=maximum+1;
        for (std::uint32_t desired=2;desired<=32;++desired) {
            d=Decide({Mode::Fixed,desired,0},c,s);
            CHECK(d.action==Action::Apply && d.generated<=maximum && d.multiplier==d.generated+1);
        }
    }
    c=Good(); c.backend=Backend::ExternalAda; s.controlOwned=true;
    CHECK(Decide({Mode::Off,2,0},c,s).reason==Reason::ExternalUnverified);
    CHECK(Decide({Mode::FollowGame,2,0},c,s).reason==Reason::ExternalUnverified);
    s.transition=true; CHECK(Decide(r,c,s).reason==Reason::ExternalUnverified);
    c.externalAbiVerified=true;
    CHECK(Decide(r,c,s).action==Action::Suspend);
    s.transition=false;
    CHECK(Decide({Mode::Off,2,0},c,s).action==Action::Apply);
    CHECK(Decide({Mode::FollowGame,2,0},c,s).action==Action::Release);
    CHECK(Decide(r,c,s).reason==Reason::ExternalUnverified);
    c=Good(); c.generation=0;
    CHECK(Decide({Mode::FollowGame,2,0},c,s).reason==Reason::Unavailable);
    c=Good(); c.backend=static_cast<Backend>(999);
    CHECK(Decide(r,c,s).reason==Reason::Unavailable);
    CHECK(Decide({Mode::FollowGame,2,0},c,s).reason==Reason::Unavailable);
    std::printf("framegen: %d checks passed\n",checks);
}
