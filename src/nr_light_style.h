#pragma once
#include <algorithm>
#include <cmath>

// Composition-only preferences. Never write model, route, raster or HDR permission.
namespace nr_light_style {
constexpr unsigned kVersion = 1;
enum Preset : unsigned { Original = 0, Natural = 1, LessGlow = 2, Custom = 3 };
struct Snapshot {
    float broad = 1.f, dark = 1.f, reflection = 1.f, structure = 1.f, glow = 0.f;
    float color = 1.f;
    unsigned preset = Original;
};
inline float Bounded(float value, float fallback, float high) {
    return std::isfinite(value) ? std::clamp(value, 0.f, high) : fallback;
}
inline bool SameValues(const Snapshot &a, const Snapshot &b) {
    return a.broad == b.broad && a.dark == b.dark && a.reflection == b.reflection &&
        a.structure == b.structure && a.glow == b.glow && a.color == b.color;
}
inline Snapshot Select(unsigned preset, float current_color) {
    Snapshot value;
    value.color = current_color;
    value.preset = preset;
    if (preset == Natural) {
        value.dark = .85f; value.reflection = 1.10f; value.structure = 1.05f;
        value.glow = .30f; value.color = .80f;
    } else if (preset == LessGlow) value.glow = .70f;
    else if (preset != Original) value.preset = Custom;
    return value;
}
template<class Config> Snapshot Read(const Config &c) {
    return {c.light_broad,c.light_dark,c.light_reflection,c.light_structure,
        c.light_glow,c.color_strength,c.light_preset};
}
template<class Config> void Write(Config &c, const Snapshot &v) {
    c.light_broad=v.broad; c.light_dark=v.dark; c.light_reflection=v.reflection;
    c.light_structure=v.structure; c.light_glow=v.glow; c.color_strength=v.color;
    c.light_preset=v.preset; c.light_preset_color=v.color; c.light_style_version=kVersion;
}
template<class Config> void Normalize(Config &c) {
    c.light_broad=Bounded(c.light_broad,1.f,2.f);
    c.light_dark=Bounded(c.light_dark,1.f,2.f);
    c.light_reflection=Bounded(c.light_reflection,1.f,2.f);
    c.light_structure=Bounded(c.light_structure,1.f,2.f);
    c.light_glow=Bounded(c.light_glow,0.f,1.f);
    c.light_preview=std::min(c.light_preview,2u);
    // Saved selection has a resolved colour value, including "keep current".
    if (c.light_style_version != kVersion || c.light_preset > LessGlow ||
        !SameValues(Read(c),Select(c.light_preset,c.light_preset_color)))
        c.light_preset=Custom;
    c.light_style_version=kVersion;
}
template<class Config> bool Active(const Config &c) {
    return c.light_broad!=1.f || c.light_dark!=1.f || c.light_reflection!=1.f ||
        c.light_structure!=1.f || c.light_glow!=0.f || c.light_preview!=0;
}
} // namespace nr_light_style
