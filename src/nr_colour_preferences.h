#pragma once
#include <algorithm>
#include <cmath>
#include "nr_colour_lab_policy.h"

namespace nr_colour_preferences {
constexpr float PriorityDefault=0.7f, ConservativeDefault=1.f;
inline float Bound(float value,float fallback) {
    return std::isfinite(value)?std::clamp(value,0.f,2.f):fallback;
}
template<class C> unsigned Policy(const C& c) {
    return nr_colour_lab::AllowUnverified(c.colour_lab_mode,c.allow_unverified_hdr_color)?0u:1u;
}
// Keep the existing effective request as the sole compositor input. The banks
// remember UI requests and never grant HDR permission or change model tuning.
template<class C> void Remember(C& c) {
    c.colour_priority_strength=Bound(c.colour_priority_strength,PriorityDefault);
    c.colour_conservative_strength=Bound(c.colour_conservative_strength,ConservativeDefault);
    c.color_strength=Bound(c.color_strength,Policy(c)?ConservativeDefault:PriorityDefault);
    (Policy(c)?c.colour_conservative_strength:c.colour_priority_strength)=c.color_strength;
}
template<class C> void Load(C& c,float old_value,float priority,float conservative) {
    c.colour_priority_strength=Bound(priority,Policy(c)==0?Bound(old_value,PriorityDefault):PriorityDefault);
    c.colour_conservative_strength=Bound(conservative,Policy(c)==1?Bound(old_value,ConservativeDefault):ConservativeDefault);
    c.color_strength=Policy(c)?c.colour_conservative_strength:c.colour_priority_strength;
}
template<class C> void Select(C& c,unsigned policy) {
    Remember(c);
    c.colour_lab_mode=policy?2:1;
    c.color_strength=policy?c.colour_conservative_strength:c.colour_priority_strength;
}
template<class C> void Reset(C& c) {
    c.colour_lab_mode=nr_colour_lab::kDefaultMode;c.colour_priority_strength=PriorityDefault;
    c.colour_conservative_strength=ConservativeDefault;c.color_strength=ConservativeDefault;
}
}
