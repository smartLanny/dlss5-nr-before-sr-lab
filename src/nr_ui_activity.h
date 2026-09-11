#pragma once
#include <array>
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
enum Reset : unsigned {
    FirstWindow=1u, ClockBackwards=2u, WindowGap=4u, ConfigChanged=8u,
    EpochChanged=16u, CounterRegressed=32u, ConfigReadChanged=64u, EpochReadChanged=128u
};
enum class Kind : unsigned { Unavailable, Reset, Held, Updated, Incoherent };
enum class Site : unsigned { Top, Feedback, Count };
enum class Display : unsigned {
    Unavailable, WrongApi, Native, Checking, Intermittent, Observed, QueueTimeout,
    RuntimeUnavailable, CodecUnavailable, RayReconstruction, OtherOwner, Conflict,
    WaitingForSr, ObserverIncomplete, CommandStateUnknown, Bypassed, NoSubmission
};
struct WindowBefore {
    bool initialized=false;
    Count started=0,last_observed=0;
    Scope scope{};
    Counters counters{};
    Sample sample{};
};
struct Observation {
    WindowBefore before{};
    Scope input_scope{};
    Counters current{};
    Sample sample{};
    Count now=0,gap_ms=0;
    unsigned reset_mask=0,regression_mask=0;
    int live_reason=0;
    bool gap_known=false,observe_called=false;
    Kind kind=Kind::Unavailable;
};
class Window {
    bool initialized=false;
    Count started=0,last_observed=0;
    Scope scope{};
    Counters before{};
    Sample sample{};
public:
    WindowBefore Inspect() const { return {initialized,started,last_observed,scope,before,sample}; }
    Sample Observe(Count now,Counters current,Scope current_scope={},int reason=0,
                   Observation* receipt=nullptr) {
        if(receipt) {
            *receipt={};receipt->before=Inspect();receipt->input_scope=current_scope;
            receipt->current=current;receipt->now=now;receipt->live_reason=reason;
            receipt->observe_called=true;
            receipt->gap_known=initialized && now>=last_observed;
            if(receipt->gap_known)receipt->gap_ms=now-last_observed;
        }
        // Reopening a panel must not turn old work into a new activity claim.
        // Config/route changes also invalidate the held sample immediately.
        if(!initialized || now<last_observed || now-last_observed>2000 ||
           current_scope.config!=scope.config || current_scope.epoch!=scope.epoch ||
           current.carriers<before.carriers || current.nr<before.nr ||
           current.submitted<before.submitted || current.queue_waits<before.queue_waits) {
            if(receipt) {
                if(!initialized)receipt->reset_mask|=FirstWindow;
                else {
                    if(now<last_observed)receipt->reset_mask|=ClockBackwards;
                    else if(now-last_observed>2000)receipt->reset_mask|=WindowGap;
                    if(current_scope.config!=scope.config)receipt->reset_mask|=ConfigChanged;
                    if(current_scope.epoch!=scope.epoch)receipt->reset_mask|=EpochChanged;
                    receipt->regression_mask=(current.carriers<before.carriers?1u:0u) |
                        (current.nr<before.nr?2u:0u) | (current.submitted<before.submitted?4u:0u) |
                        (current.queue_waits<before.queue_waits?8u:0u);
                    if(receipt->regression_mask)receipt->reset_mask|=CounterRegressed;
                }
                receipt->kind=Kind::Reset;
            }
            initialized=true;started=last_observed=now;scope=current_scope;
            before=current;sample={};return sample;
        }
        last_observed=now;
        if(now-started>=1000) {
            sample={true,now-started,{current.carriers-before.carriers,
                current.nr-before.nr,current.submitted-before.submitted,
                current.queue_waits-before.queue_waits},reason};
            started=now;before=current;
            if(receipt)receipt->kind=Kind::Updated;
        }
        else if(receipt)receipt->kind=Kind::Held;
        if(receipt)receipt->sample=sample;
        return sample;
    }
};
struct ReadReceipt {
    Observation observation{};
    Count thread_cookie=0,thread_call=0,read_gap_ms=0;
    Count config_argument=0,epoch_entered=0,config_checked=0,epoch_checked=0;
    bool read_gap_known=false,epoch_checked_known=false;
};
struct Publication {
    ReadReceipt read{};
    Count sequence=0;
    Site site=Site::Top;
    Display previous=Display::Unavailable,display=Display::Unavailable;
    bool available=false,previous_known=false,previous_valid=false,transition=false;
};
constexpr unsigned kRecentReceipts=4;
struct TraceSnapshot {
    std::array<Publication,static_cast<unsigned>(Site::Count)> latest{};
    std::array<Publication,kRecentReceipts> recent{};
    Count published=0,overwritten=0;
    unsigned recent_count=0;
};
} // namespace nr_ui_activity
