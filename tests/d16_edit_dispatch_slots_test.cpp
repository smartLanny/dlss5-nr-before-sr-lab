#include "nr_edit_dispatch_slots.hpp"
#include <cassert>
#include <iostream>
using namespace nr_edit::dispatch;
int main(){
    Slots<2> s;auto a=s.Begin(),b=s.Begin();assert(a&&b&&!s.Begin());
    assert(!s.Complete(a,{1,2,3}));assert(s.Submit(a,{1,2,3}));
    assert(!s.Submit(a,{1,2,3}));assert(!s.Complete(a,{2,2,100}));
    assert(!s.Complete(a,{1,3,100}));assert(!s.Complete(a,{1,2,2}));
    assert(s.Complete(a,{1,2,3}));auto c=s.Begin();assert(c&&c.serial!=a.serial);
    assert(!s.Complete(a,{1,2,100}));assert(s.CancelBeforeRecord(b));
    assert(s.Submit(c,{1,2,4}));assert(!s.Complete(c,{1,2,UINT64_MAX}));assert(s.Poisoned()&&!s.Idle()&&!s.Begin());
    Slots<1> reusable;for(unsigned i=1;i<=10000;++i){auto t=reusable.Begin();assert(t);
        assert(reusable.Submit(t,{4,5,i}));assert(reusable.Complete(t,{4,5,i}));assert(reusable.Idle());}
    std::cout<<"dispatch slots: immutable leases, same fence/queue, duplicate/stale tokens, poison passed\n";
}
