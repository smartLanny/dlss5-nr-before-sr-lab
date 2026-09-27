#pragma once
#include <cstdint>

// Session-only comparison policy. A successful recording is not a quality verdict.
namespace nr_acceptance_policy {
enum class Program : unsigned { Current, Colour, Temporal, PrivateSr, LowFrequency };
enum class Phase : unsigned { Warmup, Measure, Report, Capture, Complete };
struct Step { const char* name; unsigned percent; int colour; int backend; unsigned scope; bool capture; int private_sr=-1,temporal_mode=-1; };
inline unsigned Count(Program p) { return p==Program::Current?1u:p==Program::Colour?6u:p==Program::Temporal?5u:p==Program::PrivateSr?3u:2u; }
inline Step Get(Program p,unsigned i,bool before) {
    if(p==Program::Current)return {"current",0,-1,-1,0,false};
    if(p==Program::Colour){
        static constexpr Step values[]={{"scale-100",100,-1,0,0,true},{"scale-99",99,-1,0,0,true},
            {"scale-low",0,-1,0,0,true},{"colour-0",0,0,0,0,false},
            {"colour-0.5",0,5,0,0,false},{"colour-1",0,10,0,0,false}};
        auto s=values[i<6?i:0];if(i==2)s.percent=before?50:25;return s;
    }
    if(p==Program::PrivateSr){
        const unsigned low=before?50u:25u;
        if(i==0)return {"low-spatial-recovery",low,-1,0,0,false,0};
        if(i==1)return {"low-private-residual-sr",low,-1,0,0,false,1};
        return {"high-spatial-recovery",100,-1,0,0,false,0};
    }
    if(p==Program::LowFrequency)return i==0?Step{"ordinary-accumulation",0,-1,2,0,false,0,0}:Step{"low-frequency-reconstruction",0,-1,2,0,false,0,1};
    static constexpr Step values[]={{"temporal-off",0,-1,0,0,false},
        {"native-whole",0,-1,1,0,false},{"amd-whole",0,-1,2,0,false},
        {"native-later",0,-1,1,1,false},{"amd-later",0,-1,2,1,false}};
    return values[i<5?i:0];
}
struct Observation { bool matches=false,enabled=false,adopted=false,temporal=false; unsigned since_reset=0; };
struct Window {
    Phase phase=Phase::Warmup;
    std::uint64_t started=0,measuring=0;
    const char* refusal=nullptr;
    void Start(std::uint64_t now) { *this={};started=now; }
    bool Tick(std::uint64_t now,const Observation& o) {
        if(phase==Phase::Warmup){
            if(now-started>=20000){refusal="warmup-or-route-unavailable";phase=Phase::Report;return true;}
            if(now-started>=3000 && o.matches && (!o.enabled || (o.adopted&&o.since_reset>=64)) && o.temporal){
                phase=Phase::Measure;measuring=now;return true;
            }
        } else if(phase==Phase::Measure){
            if(!o.matches){refusal="configuration-or-route-changed";phase=Phase::Report;return true;}
            if(now-measuring>=20000){phase=Phase::Report;return true;}
        }
        return false;
    }
};
inline bool CountersAdvanced(std::uint64_t first,std::uint64_t last){return last>first;}
}
