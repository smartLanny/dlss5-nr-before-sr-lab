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
    s=w.Observe(12100,{30,20,12});assert(s.elapsed_ms==10000 && s.delta.carriers==10);
    assert(!w.Observe(12200,{0,0,0}).valid);
    assert(!w.Observe(100,{1,1,1}).valid);
    s=w.Observe(1100,{2,1,1});assert(s.valid && s.delta.carriers==1 && !s.delta.nr);
    assert(!w.Observe(1200,{2,0,1}).valid);
    assert(!w.Observe(1300,{2,0,0}).valid);
    auto hi=std::numeric_limits<Count>::max();Window huge;
    assert(!huge.Observe(0,{hi-1,hi-1,hi-1}).valid);
    s=huge.Observe(1000,{hi,hi,hi});assert(s.delta.nr==1 && s.delta.submitted==1);
    std::puts("UI activity: interval/idle/no-submit/reset/stale/overflow checks PASS");
}
