#include "nr_processing_route.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
using nr_processing::Session;
using nr_processing::Start;
static void fallback(Session& s,Start requested=Start::Before,bool adopt=true) {
    s.Select(requested,1);s.ObserveNativeSource(1);s.BeginSource(1);
    if(requested==Start::Before)assert(s.Reject(Start::Before,1,"before rejected"));
    assert(s.Reject(Start::After,1,"after rejected"));
    s.Hold(Start::Present);if(adopt)s.Adopted(Start::Present);
}
int main() {
    unsigned cases=0;
    {
        Session s;fallback(s);
        for(std::uint64_t i=2;i<33;++i){assert(!s.ObserveNativeSource(i));s.BeginSource(i);}
        assert(s.ObserveNativeSource(33));assert(s.NativeProbePending());
        assert(!s.Ready(Start::Present,33));assert(!s.ReprobeRequested(false));
        assert(s.Current()==Start::Present);assert(s.ReprobeRequested(true));
        assert(s.Current()==Start::Before&&s.Retained()==Start::Before);
        assert(s.Requested()==Start::Before);++cases;
    }
    {
        Session s;fallback(s);
        // Recording repeatedly without adopting a result must not hide starvation.
        for(std::uint64_t i=2;i<=33;++i){s.BeginSource(i);s.Hold(Start::Present);
            const bool due=s.ObserveNativeSource(i);assert(due==(i==33));}
        assert(s.NativeProbePending());++cases;
    }
    {
        Session s;fallback(s,Start::Before,false);
        for(std::uint64_t i=2;i<=256;++i){s.BeginSource(i);s.Hold(Start::Present);assert(!s.ObserveNativeSource(i));}
        assert(s.ObserveNativeSource(257));assert(s.ReprobeRequested(true));++cases;
    }
    {
        Session s;fallback(s);
        for(std::uint64_t i=2;i<10002;++i){s.BeginSource(i);assert(!s.ObserveNativeSource(i));s.Adopted(Start::Present);}
        assert(!s.NativeProbePending()&&s.Current()==Start::Present);++cases;
    }
    {
        Session s;fallback(s);
        for(unsigned i=0;i<8000;++i){assert(!s.ObserveNativeSource(1));s.BeginSource(1);}
        assert(!s.NativeProbePending());
        // A huge token gap is one opportunity, not thousands of missing frames.
        assert(!s.ObserveNativeSource(1000000));assert(!s.ObserveNativeSource(12));++cases;
    }
    {
        Session s;s.Select(Start::Present,1);s.BeginSource(1);s.Hold(Start::Present);s.Adopted(Start::Present);
        for(std::uint64_t i=1;i<8000;++i){s.BeginSource(i);assert(!s.ObserveNativeSource(i));}
        assert(!s.ReprobeRequested(true));assert(s.Current()==Start::Present);++cases;
    }
    {
        Session s;fallback(s,Start::After);
        for(std::uint64_t i=2;i<=33;++i)s.ObserveNativeSource(i);
        assert(s.ReprobeRequested(true));assert(s.Current()==Start::After);++cases;
    }
    {
        Session s;fallback(s);
        // Present-only traffic is not an opportunity to probe an absent native SR.
        for(std::uint64_t i=2;i<=8000;++i)s.BeginSource(i);
        assert(!s.NativeProbePending()&&s.Current()==Start::Present);++cases;
    }
    {
        Session s;fallback(s);
        for(std::uint64_t i=2;i<=33;++i)s.ObserveNativeSource(i);
        s.Hold(Start::Present);s.Adopted(Start::Present);
        assert(s.NativeProbePending()&&!s.Ready(Start::Present,33));
        assert(!s.ObserveNativeSource(33)); // duplicate cannot cause new transition
        assert(s.Select(Start::Present,2));assert(!s.NativeProbePending());
        assert(!s.Select(Start::Present,2));assert(s.Select(Start::Before,3));++cases;
    }
    {
        Session s;s.Select(Start::Before,1);s.ObserveNativeSource(1);s.BeginSource(1);
        assert(s.Reject(Start::Before,1,"structural",true));
        s.BeginSource(2);assert(s.Current()==Start::After);
        assert(!s.Reject(Start::Before,2,"wrong route"));
        assert(!s.Ready(static_cast<Start>(99),2));
        s.BeginSource(5);assert(s.Current()==Start::Before);++cases;
    }
    {
        Session s;fallback(s);
        for(std::uint64_t i=2;i<=33;++i)s.ObserveNativeSource(i);
        // Existing guide-capture reconciles the source before CarrierScope.
        s.BeginSource(33);assert(s.ReprobeRequested(true));assert(!s.BeginSource(33));
        assert(s.Current()==Start::Before);++cases;
    }
    std::cout<<"route recovery: "<<cases<<" scenarios passed\n";
}
