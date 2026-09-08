#pragma once
#include <cstdint>
namespace nr_nvfp4_core {
// A failed temporal generation cannot escape its quarantine by changing a
// setting, work extent or precision mode. Different carriers remain independent.
// The Core has sixteen context entries; failed contexts remain pinned there.
class FailureLatch {
    std::uint64_t generations_[16]{};
    bool exhausted_=false;
public:
    bool Contains(std::uint64_t generation) const {
        if(exhausted_)return true;
        for(const auto value:generations_)if(value && value==generation)return true;
        return false;
    }
    void Poison(std::uint64_t generation) {
        if(!generation){exhausted_=true;return;}
        if(Contains(generation))return;
        for(auto &value:generations_)if(!value){value=generation;return;}
        exhausted_=true;
    }
};
}
