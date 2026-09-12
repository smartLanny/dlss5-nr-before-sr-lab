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
// Guarded by the Core chain lock. Resize/configuration changes do not reset
// this session preference. Only an explicit route selection does so.
class Session {
    Start requested_=Start::Before, current_=Start::Before;
    std::uint64_t selection_=0;
    std::array<Failure,3> failures_{};
    char reason_[128]{};
public:
    bool Select(Start start,std::uint64_t selection) {
        if(!Valid(start))start=Start::Before;
        if(start==requested_&&selection==selection_)return false;
        requested_=current_=start;selection_=selection;failures_={};reason_[0]=0;return true;
    }
    Start Requested() const {return requested_;}
    Start Current() const {return current_;}
    const char* Reason() const {return reason_;}
    bool Ready(Start route,std::uint64_t source) const {
        return route==current_&&source&&source>=failures_[static_cast<unsigned>(route)].retry_source;
    }
    bool Reject(Start route,std::uint64_t source,const char* reason,bool structural=false) {
        if(!source||!Valid(route)||route!=current_)return false;
        auto& f=failures_[static_cast<unsigned>(route)];
        if(f.last_source==source)return false;
        f.last_source=source;++f.count;
        const unsigned delay=structural&&f.count<3?4u:f.count>=3?(8u<<std::min(f.count-3,4u)):0u;
        f.retry_source=source>UINT64_MAX-delay?UINT64_MAX:source+delay;
        if(reason){std::strncpy(reason_,reason,sizeof(reason_)-1);reason_[sizeof(reason_)-1]=0;}
        if(route==Start::Present)return false;
        current_=static_cast<Start>(static_cast<unsigned>(route)+1);return true;
    }
    void Adopted(Start route) {
        if(route==current_)failures_[static_cast<unsigned>(route)]={};
        // Keep the original rejection explanation even after successful fallback.
    }
};
}
