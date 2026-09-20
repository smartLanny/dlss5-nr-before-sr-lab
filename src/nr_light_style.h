#pragma once
#include <algorithm>
#include <cmath>

// Composition-only preferences. Never write model, route, raster or HDR permission.
namespace nr_light_style {
constexpr unsigned kVersion = 1;
enum Preset : unsigned { Original = 0, Natural = 1, LessGlow = 2, Custom = 3 };
enum ControlMode : unsigned { DetailLayers = 0, WholeLightDark = 1 };
struct Snapshot {
    float broad = 1.f, dark = 1.f, reflection = 1.f, structure = 1.f, glow = 0.f;
    float color = 1.f;
    unsigned preset = Original;
    float preset_color = 1.f;
    unsigned version = kVersion;
    unsigned control_mode = WholeLightDark;
};
inline float Bounded(float value, float fallback, float high) {
    return std::isfinite(value) ? std::clamp(value, 0.f, high) : fallback;
}
inline bool SameValues(const Snapshot &a, const Snapshot &b) {
    return a.broad == b.broad && a.dark == b.dark && a.reflection == b.reflection &&
        a.structure == b.structure && a.glow == b.glow &&
        a.control_mode == b.control_mode;
}
inline bool SameSnapshot(const Snapshot &a, const Snapshot &b) {
    return SameValues(a,b) && a.preset == b.preset &&
        a.preset_color == b.preset_color && a.version == b.version;
}
inline Snapshot Select(unsigned preset, float current_color, unsigned control_mode) {
    Snapshot value;
    value.color = current_color;
    value.preset = preset;
    value.control_mode = control_mode;
    if (preset == Natural) {
        value.dark = .85f; value.reflection = 1.10f; value.structure = 1.05f;
        value.glow = .30f;
    } else if (preset == LessGlow) value.glow = .70f;
    else if (preset != Original) value.preset = Custom;
    value.preset_color = value.color;
    return value;
}
template<class Config> Snapshot Read(const Config &c) {
    return {c.light_broad,c.light_dark,c.light_reflection,c.light_structure,
        c.light_glow,c.color_strength,c.light_preset,c.light_preset_color,c.light_style_version,c.light_control_mode};
}
template<class Config> void Write(Config &c, const Snapshot &v) {
    c.light_broad=v.broad; c.light_dark=v.dark; c.light_reflection=v.reflection;
    c.light_structure=v.structure; c.light_glow=v.glow; // Colour is outside this group, including Undo.
    c.light_preset=v.preset; c.light_preset_color=v.preset_color; c.light_style_version=v.version;
    c.light_control_mode=v.control_mode;
}
template<class Config> void MarkCustom(Config &c) {
    c.light_preset=Custom; c.light_preset_color=c.color_strength;
    c.light_style_version=kVersion;
}
template<class Config> void Reset(Config &c) {
    Write(c,Select(Original,c.color_strength,c.light_control_mode));
    c.light_preview=0;
}
template<class Config> void Normalize(Config &c) {
    if (c.light_control_mode > WholeLightDark) c.light_control_mode=WholeLightDark;
    c.light_broad=Bounded(c.light_broad,1.f,2.f);
    c.light_dark=Bounded(c.light_dark,1.f,2.f);
    c.light_reflection=Bounded(c.light_reflection,1.f,2.f);
    c.light_structure=Bounded(c.light_structure,1.f,2.f);
    c.light_glow=Bounded(c.light_glow,0.f,1.f);
    c.light_preview=c.light_preview<=2u ? c.light_preview : 0u;
    c.light_preset_color=Bounded(c.light_preset_color,c.color_strength,2.f);
    // Older INIs add a neutral group while retaining their existing colour.
    if (c.light_style_version == 0 &&
        SameValues(Read(c),Select(Original,c.color_strength,c.light_control_mode))) {
        c.light_preset=Original;
        c.light_preset_color=c.color_strength;
    }
    // Historical colour metadata is retained; colour no longer defines a lighting preset.
    else if (c.light_style_version != kVersion || c.light_preset > LessGlow ||
        !SameValues(Read(c),Select(c.light_preset,c.light_preset_color,c.light_control_mode)))
        c.light_preset=Custom;
    c.light_style_version=kVersion;
}
template<class Config> bool Active(const Config &c) {
    return c.light_broad!=1.f || c.light_dark!=1.f || c.light_reflection!=1.f ||
        c.light_structure!=1.f || c.light_glow!=0.f || c.light_preview!=0;
}
// Store no model, route, raster or unrelated settings. A subsequent generation
// invalidates this receipt even when the newer change happened outside the group.
struct UndoState {
    Snapshot before{};
    unsigned generation=0;
    bool valid=false;
    bool Available(unsigned current) const { return valid && current==generation; }
    void Observe(unsigned current) { if (!Available(current)) valid=false; }
    void Record(const Snapshot &value,unsigned committed) {
        before=value; generation=committed; valid=true;
    }
    void Clear() { valid=false; }
    template<class Config> bool Restore(Config &c,unsigned current) const {
        if (!Available(current)) return false;
        Write(c,before);
        return true;
    }
};
} // namespace nr_light_style
