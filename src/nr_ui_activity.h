#pragma once
#include <cstdint>
namespace nr_ui_activity {
using Count=std::uint64_t;
struct Counters { Count carriers=0,nr=0,submitted=0; };
struct Sample { bool valid=false; Count elapsed_ms=0; Counters delta{}; };
class Window {
    bool initialized=false;
    Count started=0;
    Counters before{};
    Sample sample{};
public:
    Sample Observe(Count now,Counters current) {
        if(!initialized || now<started || current.carriers<before.carriers ||
           current.nr<before.nr || current.submitted<before.submitted) {
            initialized=true;started=now;before=current;sample={};return sample;
        }
        if(now-started>=1000) {
            sample={true,now-started,{current.carriers-before.carriers,
                current.nr-before.nr,current.submitted-before.submitted}};
            started=now;before=current;
        }
        return sample;
    }
};
} // namespace nr_ui_activity
