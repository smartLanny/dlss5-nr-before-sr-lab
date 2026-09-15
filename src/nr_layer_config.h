#pragma once
#include <array>
#include <algorithm>
#include <cmath>

// Layer zero keeps the existing serialized model keys. Auxiliary layers own
// independent controls; there is deliberately no per-layer raster scale.
namespace nr_layers {
constexpr unsigned Count = 5;
struct Model {
    float intensity=1.2f, local_tone=1.f, local_structure=1.f;
    float skin_structure=-1.f;
    bool auto_mask=false;
    unsigned style=0;
    bool ui_correction=true;
    bool operator==(const Model&) const = default;
};
struct Layer : Model {
    bool enabled=false, initialized=false;
    bool operator==(const Layer&) const = default;
};
using Auxiliary = std::array<Layer,Count-1>;
template<class C> Model Primary(const C& c) {
    return {c.intensity,c.local_tone,c.local_structure,c.skin_structure,
        c.auto_mask,c.style,c.ui_correction};
}
template<class C> void WritePrimary(C& c,const Model& m) {
    c.intensity=m.intensity;c.local_tone=m.local_tone;c.local_structure=m.local_structure;
    c.skin_structure=m.skin_structure;c.auto_mask=m.auto_mask;c.style=m.style;c.ui_correction=m.ui_correction;
}
inline void Normalize(Model& m) {
    const auto bound=[](float v,float fallback,float low,float high){return std::clamp(std::isfinite(v)?v:fallback,low,high);};
    m.intensity=bound(m.intensity,1.2f,0.f,2.f);
    m.local_tone=bound(m.local_tone,1.f,0.f,2.f);
    m.local_structure=bound(m.local_structure,1.f,0.f,2.f);
    const float low=m.auto_mask?0.f:-1.f;
    m.skin_structure=bound(m.skin_structure,low,low,2.f);m.style=std::min(m.style,2u);
}
inline unsigned Active(const Auxiliary& layers) {
    unsigned n=1;for(const auto& layer:layers){if(!layer.enabled)break;++n;}return n;
}
inline void Migrate(Auxiliary& layers,unsigned active,const Model& primary) {
    active=std::clamp(active,1u,Count);
    for(unsigned i=0;i<layers.size();++i){
        layers[i]={};layers[i].enabled=i+1<active;
        if(layers[i].enabled){static_cast<Model&>(layers[i])=primary;layers[i].initialized=true;}
    }
}
template<class C> Model Read(const C& c,unsigned layer) {
    return layer?static_cast<const Model&>(c.nr_layers.at(layer-1)):Primary(c);
}
// Configuration epochs, rather than texture addresses, propagate upstream
// input changes. A layer's own epoch changes on re-enable or model edits.
template<class C> unsigned FirstChanged(const C& a,const C& b) {
    if(Primary(a)!=Primary(b))return 0;
    for(unsigned i=0;i<a.nr_layers.size();++i)
        if(static_cast<const Model&>(a.nr_layers[i])!=static_cast<const Model&>(b.nr_layers[i]) ||
           a.nr_layers[i].enabled!=b.nr_layers[i].enabled)return i+1;
    return Count;
}
}
