#pragma once

// Composition-only policy. Never grants resource access or an NGX feature.
// Values are persistent INI IDs; do not reorder when adding later experiments.
namespace nr_colour_lab {
enum class Mode : int { Legacy = 0, ColourPriority = 1, Conservative = 2 };
constexpr int kDefaultMode = static_cast<int>(Mode::Conservative);
constexpr Mode Normalize(int value) noexcept {
    return value == 0 ? Mode::Legacy : value == 1 ? Mode::ColourPriority
                                                : Mode::Conservative;
}
constexpr bool AllowUnverified(int value, bool legacy_request) noexcept {
    switch (Normalize(value)) {
    case Mode::Legacy: return legacy_request;
    case Mode::ColourPriority: return true;
    case Mode::Conservative: return false;
    }
    return false;
}
// The preparation log reports admission, not the final consumer gain budget.
constexpr bool AllowHdrColour(int value, bool legacy_request,
                              bool verified_exposure) noexcept {
    return verified_exposure || AllowUnverified(value, legacy_request);
}
constexpr bool LegacyPermissionEditable(int value) noexcept {
    return Normalize(value) == Mode::Legacy;
}
constexpr const char *Name(int value) noexcept {
    switch (Normalize(value)) {
    case Mode::Legacy: return "legacy-configured";
    case Mode::ColourPriority: return "colour-priority-experimental";
    default: return "conservative-hdr";
    }
}
} // namespace nr_colour_lab
