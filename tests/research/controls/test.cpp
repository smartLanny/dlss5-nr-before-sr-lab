#include "research/unified_controls.h"
#include <cstdio>
using namespace nr::research::controls;
static int checks=0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(false)
int main() {
    Observation o{{3840,2160},{2560,1440},100,10,true}; ScaleCaps c;
    auto p=Plan({0,750},c,o,false);
    CHECK(p.reason==ScaleReason::None && Same(p.nr,{1920,1080}) && !p.changesRender);
    CHECK(Plan({500,750},c,o,false).reason==ScaleReason::RenderUnavailable);
    c.controllerVerified=true;
    CHECK(Plan({500,750},c,o,false).reason==ScaleReason::RestartRequired);
    c.liveResize=true; p=Plan({500,750},c,o,false);
    CHECK(Same(p.render,{1920,1080}) && Same(p.nr,{1440,810}));
    CHECK(Plan({500,750},c,o,true).reason==ScaleReason::CaptureLocked);
    c.gameDrsActive=true; CHECK(Plan({500,750},c,o,false).reason==ScaleReason::GameDrsOwnsScale);
    CHECK(Plan({0,750},c,o,false).reason==ScaleReason::None); c.gameDrsActive=false;
    CHECK(Plan({249,750},c,o,false).reason==ScaleReason::InvalidScale);
    CHECK(Plan({500,499},c,o,false).reason==ScaleReason::InvalidScale);
    c.alignX=0; CHECK(Plan({500,750},c,o,false).reason==ScaleReason::InvalidAlignment); c.alignX=1;
    Extent scaled; CHECK(Scaled({0xffffffffu,0xffffffffu},1000,1,1,scaled));
    CHECK(scaled.width==0xffffffffu); CHECK(!Scaled({1,1},500,1,1,scaled));
    ScaleTransaction tx; CHECK(tx.Begin(p,o,1)); CHECK(!tx.Begin(p,o,1));
    CHECK(tx.Observe(o)==ScaleReason::StaleFrame); CHECK(!tx.CommitNr(true));
    auto now=o; ++now.frame; now.render=p.render;
    CHECK(tx.Observe(now)==ScaleReason::ResourceChangeUnproven);
    ++now.resources; CHECK(tx.Observe(now)==ScaleReason::None);
    CHECK(tx.NeedsHistoryReset()); CHECK(!tx.CommitNr(false));
    CHECK(tx.Pending() && tx.NeedsHistoryReset() && tx.AppliedNr().width==0);
    CHECK(tx.CommitNr(true) && Same(tx.AppliedNr(),p.nr)); CHECK(!tx.CommitNr(true));
    auto unchanged=Plan({0,750},c,now,false); CHECK(tx.Begin(unchanged,now,2));
    ++now.frame; CHECK(tx.Observe(now)==ScaleReason::None && !tx.NeedsHistoryReset());
    CHECK(tx.CommitNr(true)); CHECK(tx.Begin(unchanged,now,3));
    now.output={1920,1080}; CHECK(tx.Observe(now)==ScaleReason::OutputChanged && !tx.Pending());
    now=o; CHECK(tx.Begin(p,now,4)); now.frame+=121;
    CHECK(tx.Observe(now)==ScaleReason::TimedOut && !tx.Pending());
    nr::research::fg::Caps fc; nr::research::fg::Conditions conditions;
    conditions.offlineAllowed=conditions.atSafeBoundary=true;
    Request request; request.scale={0,750}; auto menu=Resolve(request,ScaleCaps{},o,fc,conditions);
    CHECK(!menu.trueRenderControlAvailable && menu.scale.reason==ScaleReason::None);
    CHECK(menu.framegen.action==nr::research::fg::Action::None);
    for (std::uint32_t ratio=500;ratio<=1000;++ratio) {
        auto q=Plan({0,ratio},ScaleCaps{},o,false);
        CHECK(q.reason==ScaleReason::None && Same(q.render,o.render));
        CHECK(q.nr.width<=o.render.width && q.nr.height<=o.render.height);
    }
    auto invalid=o; invalid.sameFrameGuidesVerified=false;
    CHECK(!Resolve(request,c,invalid,fc,conditions).trueRenderControlAvailable);
    CHECK(tx.Begin(p,o,5)); invalid.frame=o.frame+121;
    CHECK(tx.Observe(invalid)==ScaleReason::TimedOut && !tx.Pending());
    std::printf("controls: %d checks passed\n",checks);
}
