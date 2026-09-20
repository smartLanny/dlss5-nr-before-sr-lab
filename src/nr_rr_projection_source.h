#pragma once
#include <array>
#include "nr_rr_depth_projection.h"
#include "nr_source_frame.hpp"

// CPU copies only. The current SL RR evaluation and its exact native call own
// the lease; no global "latest camera" is usable by a different frame/view.
namespace nr_rr_projection {
using nr_source_frame::Source;
using Input=nr_rr_depth::CurrentInput;
struct Entry { Source source{}; Input input{}; unsigned long long tick=0,history=0; };
struct Cache {
    std::array<Entry,96> entries{};
    unsigned cursor=0;
    void Put(Source source,Input input,unsigned long long tick,unsigned long long history=1) {
        if(!source)return;
        for(auto& e:entries)if(e.source==source){e={source,input,tick,history};return;}
        entries[cursor]={source,input,tick,history};cursor=(cursor+1)%entries.size();
    }
    Input Get(Source source,unsigned long long now,unsigned long long history=1) const {
        if(source)for(const auto& e:entries)if(e.source==source && e.history==history && now>=e.tick && now-e.tick<=1000)return e.input;
        return {};
    }
};
struct Lease {Source source{};Input input{};const void* commands=nullptr;};
inline thread_local Lease evaluation{};
struct Evaluation {
    Lease previous=evaluation;
    Evaluation(){evaluation={};}
    ~Evaluation(){evaluation=previous;}
    void Set(Source source,Input input,const void* commands){if(source&&commands)evaluation={source,input,commands};}
};
struct NativeLease {Lease evaluation{};const void* params=nullptr;unsigned long long generation=0;};
inline thread_local NativeLease native{};
inline std::atomic<unsigned long long> scope_matches{0},scope_mismatches{0},used{0};
inline std::atomic<unsigned> last_origin{0}; // 0 NGX/none, 1 matched Streamline.
struct NativeScope {
    NativeLease previous=native;
    NativeScope(const void* commands,const void* params,unsigned long long generation) {
        native={};
        if(evaluation.source && commands && params && generation) {
            if(commands==evaluation.commands){native={evaluation,params,generation};scope_matches.fetch_add(1,std::memory_order_relaxed);}
            else scope_mismatches.fetch_add(1,std::memory_order_relaxed);
        }
    }
    ~NativeScope(){native=previous;}
};
inline Input Choose(Input current,const void* params,unsigned long long generation) {
    last_origin.store(0,std::memory_order_relaxed);
    if(current.state!=nr_rr_depth::InputState::Absent || !params || !generation ||
       native.params!=params || native.generation!=generation || !native.evaluation.source)return current;
    const auto alternate=native.evaluation.input;
    if(alternate.state==nr_rr_depth::InputState::Absent)return current;
    last_origin.store(1,std::memory_order_relaxed);
    if(alternate.state==nr_rr_depth::InputState::Supported)used.fetch_add(1,std::memory_order_relaxed);
    return alternate;
}
}
