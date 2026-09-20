#pragma once
#include "nr_layer_config.h"
#include "nr_light_style.h"

// UI actions operate on the existing pending configuration, never runtime state.
namespace nr_unified_ui {
template<class C> void SetLayers(C& c,unsigned count) {
    count=std::clamp(count,1u,nr_layers::Count);
    for(unsigned i=0;i<c.nr_layers.size();++i) {
        auto& layer=c.nr_layers[i];
        layer.enabled=i+1<count;
        // The constructor/INI loader already supplied defaults or explicit
        // saved fields. First enable must not erase either kind of value.
        if(layer.enabled)layer.initialized=true;
    }
    c.nr_passes=count>1?2u:1u;
}
template<class C> unsigned LightMethod(const C& c) {
    return c.generic_residual?2u:c.region_enabled?1u:0u;
}
template<class C> void SetLightMethod(C& c,unsigned method) {
    c.generic_residual=method==2;c.region_enabled=method==1;
    c.light_preview=0;c.region_preview=0;
}
template<class C> void ResetLightMethod(C& c) {
    const auto method=LightMethod(c);
    if(method==2)std::fill(c.generic_gains,c.generic_gains+5,1.f);
    else if(method==1)std::fill(c.region_gains,c.region_gains+11,1.f);
    else { nr_light_style::Reset(c); c.light_control_mode=nr_light_style::WholeLightDark; }
    c.light_preview=0;c.region_preview=0;
}
template<class C> bool LegacyLightActive(const C& c) {
    return c.light_broad!=1.f||c.light_dark!=1.f||c.light_control_mode!=1u;
}
}
