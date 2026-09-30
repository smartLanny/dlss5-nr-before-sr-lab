#pragma once
#include "nr_dual_pass.hpp"
#include <array>

// The primary raster already includes WorkingScale. Every auxiliary consumes
// the same raster; applying another scale here would compound the request.
namespace nr_full {
constexpr unsigned kMinPasses=2, kMaxPasses=5, kAuxLayers=kMaxPasses-1;
struct Settings { bool enabled=false; unsigned passes=2; };
inline bool Valid(Settings s) noexcept { return s.passes>=kMinPasses&&s.passes<=kMaxPasses; }
struct Plan {
    unsigned passes=0;
    nr_dual::Extent auxiliary{};
    bool reduced=false;
    std::array<nr_dual::Plan,kAuxLayers> layers{};
    explicit operator bool() const noexcept {return passes>=kMinPasses&&passes<=kMaxPasses;}
};
inline Plan MakePlan(Settings s,nr_dual::Extent extent,const nr_dual::RasterRules& rules={}) noexcept {
    if(!s.enabled||!Valid(s))return {};
    const auto first=nr_dual::MakePlan({2,{1,1}},extent,rules);
    if(!first)return {};
    const auto direct=nr_dual::MakePlan({2,{1,1}},first.secondary,rules);
    if(!direct||direct.path!=nr_dual::Path::DirectSecond)return {};
    Plan out{};out.passes=s.passes;out.auxiliary=first.secondary;out.reduced=first.path==nr_dual::Path::MatchedResidual;
    out.layers[0]=first;
    for(unsigned i=1;i<s.passes-1;++i)out.layers[i]=direct;
    return out;
}
struct Step {std::uint32_t fatal=0;bool evaluated=false,adoptable=false;};
struct Result {std::uint32_t fatal=0;unsigned evaluated=0;bool adopted=false;};
// The caller seals every available layer first. Each step owns independent
// Create/parameters/history/fence state. A partial chain is never advertised
// as the requested full reference; the original consumer composes once.
template<class Backend>
Result Run(unsigned requested,unsigned prepared,Backend& backend) {
    if(requested<kMinPasses||requested>kMaxPasses||prepared>requested-1)return {};
    Result out{};
    for(unsigned i=0;i<prepared;++i) {
        const auto step=backend.Layer(i);
        if(step.fatal){out.fatal=step.fatal;return out;}
        if(!step.evaluated)return out;
        ++out.evaluated;
        if(!step.adoptable)return out;
    }
    if(prepared==requested-1){backend.Adopt(prepared-1);out.adopted=true;}
    return out;
}
}
