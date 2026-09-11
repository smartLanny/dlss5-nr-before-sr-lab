#include "nr_ui_activity.h"
#include <cassert>
#include <cstdio>
#include <limits>
int main() {
    using namespace nr_ui_activity;
    Window w;
    assert(!w.Observe(100,{10,5,4}).valid);
    assert(!w.Observe(1099,{12,6,4}).valid);
    auto s=w.Observe(1100,{20,10,8});
    assert(s.valid && s.elapsed_ms==1000 && s.delta.carriers==10 && s.delta.nr==5 && s.delta.submitted==4);
    auto held=w.Observe(1110,{21,11,9});
    assert(held.delta.carriers==s.delta.carriers && held.elapsed_ms==s.elapsed_ms);
    s=w.Observe(2100,{20,10,8});assert(s.valid && !s.delta.carriers && !s.delta.nr && !s.delta.submitted);
    s=w.Observe(12100,{30,20,12});assert(!s.valid && !s.Processing());
    assert(!w.Observe(12200,{0,0,0}).valid);
    assert(!w.Observe(100,{1,1,1}).valid);
    s=w.Observe(1100,{2,1,1});assert(s.valid && s.delta.carriers==1 && !s.delta.nr);
    assert(!w.Observe(1200,{2,0,1}).valid);
    assert(!w.Observe(1300,{2,0,0}).valid);
    auto hi=std::numeric_limits<Count>::max();Window huge;
    assert(!huge.Observe(0,{hi-1,hi-1,hi-1}).valid);
    s=huge.Observe(1000,{hi,hi,hi});assert(s.delta.nr==1 && s.delta.submitted==1);
    // Reproduce a carrier alternating active and queue-wait every 10 ms.
    // Either reason can be current when the interval closes: the complete
    // visible status must still describe the same intermittent work.
    for (int phase=0;phase!=2;++phase) {
        Window alternating;
        Counters counts{};
        const Scope scope{5,2};
        alternating.Observe(0,counts,scope,0);
        for (Count tick=10;tick<=3000;tick+=10) {
            ++counts.carriers;
            const bool skipped=((tick/10+phase)%2)==0;
            if (skipped) ++counts.queue_waits;
            else { ++counts.nr; ++counts.submitted; }
            const auto observed=alternating.Observe(tick,counts,scope,skipped?14:0);
            if(tick>=1000) {
                assert(observed.Processing() && observed.Intermittent());
                assert(observed.delta.nr==50 && observed.delta.queue_waits==50);
            }
        }
        // A persistent failure stops the activity claim at the next empty
        // interval; an active-looking last reason cannot invent a submission.
        s=alternating.Observe(4000,counts,scope,19);
        assert(s.valid && !s.Processing() && s.bypass_reason==19);
        s=alternating.Observe(4100,counts,scope,0);
        assert(s.bypass_reason==19 && !s.Processing());
        s=alternating.Observe(5000,counts,scope,0);
        assert(s.valid && !s.Processing() && s.bypass_reason==0);
        // F6/mode/config and feature lifetime changes invalidate even when
        // monotonic process counters have not reset.
        assert(!alternating.Observe(5010,counts,{6,2},0).valid);
        ++counts.nr; ++counts.submitted;
        s=alternating.Observe(6010,counts,{6,2},0);
        assert(s.Processing() && !s.Intermittent());
        assert(!alternating.Observe(6020,counts,{6,3},0).valid);
        ++counts.nr; ++counts.submitted;
        s=alternating.Observe(7020,counts,{6,3},0);assert(s.Processing());
        // A closed/paused UI has no valid recent interval when reopened.
        ++counts.nr; ++counts.submitted;
        assert(!alternating.Observe(12000,counts,{6,3},0).valid);
        s=alternating.Observe(13000,counts,{6,3},0);
        assert(s.valid && !s.Processing());
    }
    Window unsubmitted;
    unsubmitted.Observe(0,{0,0,0,0});
    s=unsubmitted.Observe(1000,{100,100,0,0});
    assert(s.valid && !s.Processing());
    Window submit_only;
    submit_only.Observe(0,{0,0,0,0});
    s=submit_only.Observe(1000,{0,0,2,0});assert(!s.Processing());
    Window queue_reset;
    queue_reset.Observe(0,{10,5,5,5});
    assert(!queue_reset.Observe(1000,{20,10,10,0}).valid);
    std::puts("UI activity: alternating-carrier/stall/scope/reopen/no-submit/reset/overflow checks PASS");
}
