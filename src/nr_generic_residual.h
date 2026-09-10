#pragma once
#include <algorithm>
#include <cmath>
// Original implementation of the published generic directional/HSL method.
// The Core applies it to its normalized model-storage pair before its existing
// compositor. It does not import Magpie's SDR output clamp into game HDR.
namespace nr_generic {
template<class C> void Normalize(C& c) {
    for(unsigned i=0;i<5;++i)
        c.generic_gains[i]=std::clamp(std::isfinite(c.generic_gains[i])?c.generic_gains[i]:1.f,i==0?1.f:0.f,2.f);
}
template<class C> bool Active(const C& c) { return c.generic_residual; }
}
