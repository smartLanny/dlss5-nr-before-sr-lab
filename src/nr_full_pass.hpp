#pragma once
#include "nr_dual_pass.hpp"
#include <array>

// Full-size reference is a separate request; the old reduced NR2 contract
// remains unchanged. This ledger never infers GPU completion or frame identity.
namespace nr_full {
constexpr unsigned kMinPasses=2, kMaxPasses=4, kAuxLayers=3;
struct Settings { bool enabled=false; unsigned passes=2; };
constexpr bool Valid(Settings s) noexcept { return s.passes>=kMinPasses&&s.passes<=kMaxPasses; }
struct Plan {
    unsigned passes=0;
    std::array<nr_dual::Plan,kAuxLayers> layers{};
    explicit operator bool() const noexcept {return passes>=kMinPasses&&passes<=kMaxPasses;}
};
inline Plan MakePlan(Settings s,nr_dual::Extent extent,const nr_dual::RasterRules& rules={}) noexcept {
    if(!s.enabled||!Valid(s))return {};
    const auto direct=nr_dual::MakePlan({2,{1,1}},extent,rules);
    if(!direct||direct.path!=nr_dual::Path::DirectSecond)return {};
    Plan out{};out.passes=s.passes;
    for(unsigned i=0;i<s.passes-1;++i)out.layers[i]=direct;
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
