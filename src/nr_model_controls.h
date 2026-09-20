#pragma once

// An isolated model-control comparison. No composition, route or raster fields.
namespace nr_model_controls {
struct Snapshot {
    float intensity = 1.5f, local_tone = 1.f, local_structure = 1.f;
    float skin_structure = .4f;
    bool auto_mask = true;
    unsigned style = 0;
    bool ui_correction = true;
};
enum class Action { None, Reset, Undo };
template<class Config> Snapshot Read(const Config &c) {
    return {c.intensity, c.local_tone, c.local_structure, c.skin_structure,
        c.auto_mask, c.style, c.ui_correction};
}
template<class Config> void Write(Config &c, const Snapshot &v) {
    c.intensity = v.intensity;
    c.local_tone = v.local_tone;
    c.local_structure = v.local_structure;
    c.skin_structure = v.skin_structure;
    c.auto_mask = v.auto_mask;
    c.style = v.style;
    c.ui_correction = v.ui_correction;
}
inline bool Same(const Snapshot &a, const Snapshot &b) {
    return a.intensity == b.intensity && a.local_tone == b.local_tone &&
        a.local_structure == b.local_structure && a.skin_structure == b.skin_structure &&
        a.auto_mask == b.auto_mask && a.style == b.style && a.ui_correction == b.ui_correction;
}
template<class Config> void Reset(Config &c) { Write(c, Snapshot{}); }
struct UndoState {
    Snapshot before{};
    unsigned generation = 0;
    bool valid = false;
    bool Available(unsigned current) const { return valid && current == generation; }
    void Observe(unsigned current) { if (!Available(current)) valid = false; }
    void Record(const Snapshot &value, unsigned committed) {
        before = value; generation = committed; valid = true;
    }
    void Clear() { valid = false; }
    template<class Config> bool Restore(Config &c, unsigned current) const {
        if (!Available(current)) return false;
        Write(c, before);
        return true;
    }
};
} // namespace nr_model_controls
