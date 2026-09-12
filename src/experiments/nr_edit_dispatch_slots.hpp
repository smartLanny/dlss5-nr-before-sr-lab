#pragma once
#include <array>
#include <cstdint>
#include <limits>

namespace nr_edit::dispatch {
using Id=std::uint64_t;
struct Completion { Id queue=0,fence=0,value=0; };
struct Ticket { unsigned slot=0; Id serial=0; explicit operator bool()const{return serial!=0;} };
enum class State { Free, Recording, Submitted, Poisoned };
// This is used by the D3D12 implementation. All calls must be serialized by its
// owner; a token is NOT a GPU fence and an unsubmitted recording is never free.
template<unsigned Count=12> class Slots {
    struct Entry { State state=State::Free; Id serial=0; Completion target{}; };
    std::array<Entry,Count> entries_{};
    Id next_=1;
    bool poisoned_=false;
    Entry* Find(Ticket t) {
        return t.slot<Count&&t.serial&&entries_[t.slot].serial==t.serial ? &entries_[t.slot]:nullptr;
    }
public:
    Ticket Begin() {
        if(poisoned_||next_==std::numeric_limits<Id>::max())return {};
        for(unsigned i=0;i<Count;++i)if(entries_[i].state==State::Free){
            entries_[i]={State::Recording,next_++,{}};return {i,entries_[i].serial};
        }
        return {};
    }
    bool CancelBeforeRecord(Ticket t) {
        auto* e=Find(t);if(!e||e->state!=State::Recording)return false;
        *e={};return true;
    }
    bool Submit(Ticket t,Completion c) {
        auto* e=Find(t);
        if(!e||e->state!=State::Recording||!c.queue||!c.fence||!c.value||
           c.value==std::numeric_limits<Id>::max())return false;
        e->state=State::Submitted;e->target=c;return true;
    }
    bool Complete(Ticket t,Completion c) {
        auto* e=Find(t);
        if(!e||e->state!=State::Submitted||c.queue!=e->target.queue||c.fence!=e->target.fence)return false;
        if(c.value==std::numeric_limits<Id>::max()){Poison();return false;}
        if(c.value<e->target.value)return false;
        *e={};return true;
    }
    void Poison(){poisoned_=true;for(auto& e:entries_)if(e.state!=State::Free)e.state=State::Poisoned;}
    bool Poisoned()const{return poisoned_;}
    bool Idle()const{if(poisoned_)return false;for(const auto& e:entries_)if(e.state!=State::Free)return false;return true;}
    State Get(Ticket t)const{return t.slot<Count&&t.serial&&entries_[t.slot].serial==t.serial?entries_[t.slot].state:State::Free;}
};
}
