#pragma once

// A recipe is derived from existing settings, never a second persisted state.
// It changes only the six colour-lab selectors. Model, route, scale, layers,
// guards, exposure and all other saved user preferences are untouched.
namespace nr_quality_recipe {
enum class Recipe : int { ColourBaseline = 0, N2Shadow = 1, Custom = 2 };
template<class Config>
constexpr Recipe Describe(const Config &c) noexcept {
    if (c.colour_lab_mode != 1 || c.colour_restore_mode != 0 ||
        c.colour_scaled_proxy_mode != 0 || c.colour_edge_recovery != 0 ||
        c.colour_neutral_probe) return Recipe::Custom;
    return c.colour_input_curve == 0 ? Recipe::ColourBaseline :
        c.colour_input_curve == 1 ? Recipe::N2Shadow : Recipe::Custom;
}
template<class Config>
constexpr bool Select(Config &c, Recipe recipe) noexcept {
    if (recipe != Recipe::ColourBaseline && recipe != Recipe::N2Shadow) return false;
    const bool changed = Describe(c) != recipe;
    c.colour_lab_mode = 1;
    c.colour_restore_mode = 0;
    c.colour_input_curve = recipe == Recipe::N2Shadow ? 1 : 0;
    c.colour_scaled_proxy_mode = 0;
    c.colour_edge_recovery = 0;
    c.colour_neutral_probe = false;
    return changed;
}
} // namespace nr_quality_recipe
