#pragma once
#include <cstdint>
namespace nr_ui_activity {
using Count=std::uint64_t;
struct Counters { Count carriers=0,nr=0,submitted=0,queue_waits=0; };
struct Scope { Count config=0,epoch=0; };
struct Sample {
    bool valid=false;
    Count elapsed_ms=0;
    Counters delta{};
    int bypass_reason=0;
    bool Processing() const { return valid && delta.nr>0 && delta.submitted>0; }
    bool Intermittent() const { return Processing() && delta.queue_waits>0; }
};
class Window {
    bool initialized=false;
    Count started=0,last_observed=0;
    Scope scope{};
    Counters before{};
    Sample sample{};
public:
    Sample Observe(Count now,Counters current,Scope current_scope={},int reason=0) {
        // Reopening a panel must not turn old work into a new activity claim.
        // Config/route changes also invalidate the held sample immediately.
        if(!initialized || now<last_observed || now-last_observed>2000 ||
           current_scope.config!=scope.config || current_scope.epoch!=scope.epoch ||
           current.carriers<before.carriers || current.nr<before.nr ||
           current.submitted<before.submitted || current.queue_waits<before.queue_waits) {
            initialized=true;started=last_observed=now;scope=current_scope;
            before=current;sample={};return sample;
        }
        last_observed=now;
        if(now-started>=1000) {
            sample={true,now-started,{current.carriers-before.carriers,
                current.nr-before.nr,current.submitted-before.submitted,
                current.queue_waits-before.queue_waits},reason};
            started=now;before=current;
        }
        return sample;
    }
};
} // namespace nr_ui_activity
