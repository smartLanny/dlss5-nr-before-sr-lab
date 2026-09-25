#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace nr_processing {
// Deliberately distinct from the legacy Mode=2/1 wire representation.
enum class Start : unsigned { Before=0, After=1, Present=2 };
inline bool Valid(Start s) { return static_cast<unsigned>(s)<=2; }
inline const char* Name(Start s) {
    return s==Start::Before?"Before":s==Start::After?"After":"Present";
}
inline Start FromLegacy(int mode) { return mode==1?Start::After:Start::Before; }
struct Failure {
    std::uint64_t last_source=0, retry_source=0;
    unsigned count=0;
};
struct Snapshot {
    Start requested=Start::Before,current=Start::Before,retained=Start::Before;
    char reason[128]{};
};
// Guarded by the Core chain lock. A fallback is a lease, not a permanent
// replacement of the user's choice. Source claims and GPU retirement remain
// owned by their existing subsystems; this class never releases either.
class Session {
    Start requested_=Start::Before, current_=Start::Before, floor_=Start::Before;
    std::uint64_t selection_=0, source_=0;
    std::array<Failure,3> failures_{};
    char reason_[128]{};
    std::uint64_t last_native_source_=0, native_observations_=0;
    std::uint64_t retained_since_=0, last_adopted_=0;
    bool fallback_adopted_=false, native_probe_pending_=false;
    void Explain(const char* text) {
        std::strncpy(reason_,text,sizeof(reason_)-1);reason_[sizeof(reason_)-1]=0;
    }
public:
    // Counts distinct real native reconstruction opportunities, not Present,
    // FG frames, source-number gaps or wall-clock guesses.
    static constexpr std::uint64_t kStalledFallbackSources=32;
    // Bootstrap gets a longer bounded lease than a once-working fallback.
    // Repeated private recording is NOT evidence of a usable output.
    static constexpr std::uint64_t kBootstrapFallbackSources=256;
    bool Select(Start start,std::uint64_t selection) {
        if(!Valid(start))start=Start::Before;
        if(start==requested_&&selection==selection_)return false;
        requested_=current_=floor_=start;selection_=selection;source_=0;failures_={};reason_[0]=0;
        last_native_source_=native_observations_=retained_since_=last_adopted_=0;
        fallback_adopted_=native_probe_pending_=false;return true;
    }
    Start Requested() const {return requested_;}
    Start Current() const {return current_;}
    Start Retained() const {return floor_;}
    const char* Reason() const {return reason_;}
    // A skipped frame is not evidence that a later entry works. Retry from the
    // retained route on a NEW source, honoring the existing per-entry backoff.
    // Once private NR starts, Hold pins that route through creation/warmup so
    // switching entry cannot strand its in-flight work or repeat the source.
    bool BeginSource(std::uint64_t source) {
        if(!source||source<=source_)return false;
        source_=source;
        Start next=floor_;
        while(next!=Start::Present&&source<failures_[static_cast<unsigned>(next)].retry_source)
            next=static_cast<Start>(static_cast<unsigned>(next)+1);
        const bool changed=next!=current_;current_=next;return changed;
    }
    void Hold(Start route) {
        if(!Valid(route)||route!=current_)return;
        if(floor_!=route) {
            retained_since_=last_adopted_=native_observations_;
            fallback_adopted_=false;
        }
        floor_=route;
        // Do not cancel an armed probe: stopping new fallback submissions is
        // required for its real fence to drain rather than chase a moving tail.
    }
    bool NativeProbePending() const {return native_probe_pending_;}
    bool ObserveNativeSource(std::uint64_t source) {
        if(!source||source<=last_native_source_)return false;
        last_native_source_=source;
        if(native_observations_!=UINT64_MAX)++native_observations_;
        // This repair concerns automatic Present fallback only. Explicit Present
        // remains selected, and healthy output never expires periodically.
        if(floor_!=Start::Present||requested_==Start::Present)return false;
        const bool stalled=fallback_adopted_ ?
            native_observations_-last_adopted_>=kStalledFallbackSources :
            native_observations_-retained_since_>=kBootstrapFallbackSources;
        if(!native_probe_pending_&&stalled) {
            native_probe_pending_=true;
            Explain(fallback_adopted_?"fallback-stalled-awaiting-exact-drain":
                                      "fallback-bootstrap-expired-awaiting-exact-drain");
        }
        return native_probe_pending_;
    }
    bool ReprobeRequested(bool previous_work_drained) {
        if(!native_probe_pending_||!previous_work_drained)return false;
        // The caller checked the existing non-blocking retirement predicate.
        // This only permits normal native preflight; it does not assert success.
        native_probe_pending_=false;
        current_=floor_=requested_;
        retained_since_=last_adopted_=native_observations_;fallback_adopted_=false;
        Explain("preferred-native-route-reprobe");
        return true;
    }
    bool Ready(Start route,std::uint64_t source) const {
        return Valid(route)&&route==current_&&source&&
            !(native_probe_pending_&&route!=requested_)&&
            source>=failures_[static_cast<unsigned>(route)].retry_source;
    }
    bool Reject(Start route,std::uint64_t source,const char* reason,bool structural=false) {
        if(!source||!Valid(route)||route!=current_)return false;
        auto& f=failures_[static_cast<unsigned>(route)];
        if(f.last_source>=source)return false;
        source_=std::max(source_,source);
        f.last_source=source;if(f.count!=UINT32_MAX)++f.count;
        const unsigned delay=structural&&f.count<3?4u:f.count>=3?(8u<<std::min(f.count-3,4u)):0u;
        f.retry_source=source>UINT64_MAX-delay?UINT64_MAX:source+delay;
        if(reason){std::strncpy(reason_,reason,sizeof(reason_)-1);reason_[sizeof(reason_)-1]=0;}
        if(route==Start::Present)return false;
        current_=static_cast<Start>(static_cast<unsigned>(route)+1);
        return true;
    }
    void Adopted(Start route) {
        if(Valid(route)&&route==current_) {
            Hold(route);last_adopted_=native_observations_;fallback_adopted_=true;
            failures_[static_cast<unsigned>(route)]={};
        }
        // Keep the original rejection explanation even after successful fallback.
    }
};
}
