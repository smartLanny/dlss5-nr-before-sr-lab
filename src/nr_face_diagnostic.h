#pragma once
#include "nr_model_controls.h"

// Pure D7 experiment inputs. No resource, history, route or composition owner.
namespace nr_face_diagnostic {
inline constexpr unsigned kGroups = 9, kRoiSide = 512;
inline constexpr const char *kLabels[kGroups] = {
    "current-face-off", "current-face-on0",
    "baseline-face-off", "baseline-face-on0",
    "baseline-tone2-face-off", "baseline-tone2-face-on0",
    "baseline-structure2-face-off", "baseline-structure2-face-on0",
    "current-face-on0-repeat"
};
inline bool ValidCenter(float x, float y) {
    // Ordered bounds also reject NaN and infinities, without replacing them.
    return x >= 0.f && x <= 1.f && y >= 0.f && y <= 1.f;
}
inline unsigned long long ScreenshotUpperBound(unsigned width, unsigned height) {
    // RGBA8 scanlines, zlib compressBound plus PNG chunks; reject huge inputs first.
    if (!width || !height || width > 16384 || height > 16384) return 0;
    const auto raw = (4ull * width + 1u) * height;
    return raw + (raw >> 12) + (raw >> 14) + (raw >> 25) + 13u + 65536u;
}
inline bool AdmitDisplay(unsigned width, unsigned height) {
    const auto png = ScreenshotUpperBound(width,height);
    const auto roi = kGroups * (kRoiSide * static_cast<unsigned long long>(kRoiSide) * 80u + 16384u);
    return png && png <= 64ull * 1024 * 1024 &&
        roi + kGroups * png + 16ull * 1024 * 1024 <= 512ull * 1024 * 1024;
}
struct Roi { bool valid = false; unsigned x = 0, y = 0, width = 0, height = 0; };
inline Roi ResolveRoi(unsigned width, unsigned height, float center_x, float center_y) {
    if (!width || !height || !ValidCenter(center_x, center_y)) return {};
    Roi result; result.valid = true;
    result.width = width < kRoiSide ? width : kRoiSide;
    result.height = height < kRoiSide ? height : kRoiSide;
    const double x = static_cast<double>(center_x) * width - result.width * .5;
    const double y = static_cast<double>(center_y) * height - result.height * .5;
    const unsigned max_x = width - result.width, max_y = height - result.height;
    result.x = x <= 0 ? 0 : x >= max_x ? max_x : static_cast<unsigned>(x + .5);
    result.y = y <= 0 ? 0 : y >= max_y ? max_y : static_cast<unsigned>(y + .5);
    return result;
}
template<class Config> Config GroupConfig(const Config &original, unsigned index) {
    Config result = original;
    if (index >= kGroups) return result;
    if (index >= 2 && index <= 7) nr_model_controls::Reset(result);
    if (index == 4 || index == 5) result.local_tone = 2.f;
    if (index == 6 || index == 7) result.local_structure = 2.f;
    result.auto_mask = (index & 1u) != 0u || index == 8;
    result.skin_structure = 0.f;
    return result;
}
template<class Config, class Tuning> bool MatchesApplied(const Config &expected, const Tuning &actual) {
    return expected.intensity == actual.intensity && expected.local_tone == actual.local_tone &&
        expected.local_structure == actual.local_structure && expected.style == actual.style &&
        expected.auto_mask == actual.auto_mask && expected.ui_correction == actual.ui_correction &&
        (expected.auto_mask ? expected.skin_structure : -1.f) == actual.skin_structure;
}
} // namespace nr_face_diagnostic
