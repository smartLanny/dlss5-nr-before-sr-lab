#pragma once

// Separate persistent output/input selectors. Output recovery never grants a
// resource/queue/lifetime contract. Input preparation participates in NR
// history because it changes what the temporal model sees.
namespace nr_colour_restore {
enum class Mode : int {
    Legacy = 0,
    SourceReferencedRgbProtected = 1,
    SourceReferencedRgbRaw = 2,
};
enum class Encoding : unsigned { Unknown = 0, Post = 1, V2 = 2 };
struct EncodingReceipt {
    Encoding encoding = Encoding::Unknown;
    unsigned long long serial = 0;
    int input_curve = 0;
    int scaled_proxy_mode = 0;
    constexpr Encoding For(unsigned long long current) const noexcept {
        return current != 0 && current == serial ? encoding : Encoding::Unknown;
    }
};
// Owner colour4 feedback: RGB restoration adds contour halos. Only the
// missing-key default changes; existing explicit 0/1/2 settings are preserved.
constexpr int kDefaultMode = 0;
constexpr int kDefaultInputCurve = 0;
constexpr int kDefaultScaledProxyMode = 0;
constexpr unsigned kInputCurveFlag = 268435456u;
constexpr unsigned kRgbFlag = 1073741824u;
constexpr unsigned kPostEncodingFlag = 536870912u;
constexpr unsigned kRawEditFlag = 134217728u;
constexpr unsigned kProxyFirstScaleFlag = 67108864u;
static_assert(((kRgbFlag | kPostEncodingFlag | kInputCurveFlag |
                kRawEditFlag | kProxyFirstScaleFlag) & 0x00ffffffu) == 0u);
constexpr int Normalize(int value) noexcept {
    return value == 1 || value == 2 ? value : 0;
}
constexpr int NormalizeScaledProxy(int value) noexcept {
    return value == 1 ? 1 : 0;
}
constexpr unsigned Flags(int mode, bool combine, Encoding encoding) noexcept {
    const int normalized = Normalize(mode);
    return normalized != 0 && combine &&
        (encoding == Encoding::Post || encoding == Encoding::V2)
        ? kRgbFlag |
          (encoding == Encoding::Post ? kPostEncodingFlag : 0u) |
          (normalized == 2 ? kRawEditFlag : 0u)
        : 0u;
}
constexpr int EffectiveInputCurve(int request, unsigned source_flags) noexcept {
    return request == 1 && (source_flags & (1u | 16u | 32u)) == 0u ? 1 : 0;
}
constexpr int EffectiveScaledProxyMode(int request, bool exact_area_encoder,
                                       bool scaled) noexcept {
    return NormalizeScaledProxy(request) == 1 && exact_area_encoder && scaled ? 1 : 0;
}
constexpr const char *CurveName(int curve) noexcept {
    return curve == 1 ? "paired-n2-hdr" : "d21-input-unchanged";
}
constexpr const char *ScaledProxyName(int mode) noexcept {
    return NormalizeScaledProxy(mode) == 1
        ? "proxy-first-exact-area" : "d21-source-first";
}
constexpr const char *Name(int mode) noexcept {
    return Normalize(mode) == 2 ? "matched-rgb-raw-edit-reference" :
        Normalize(mode) == 1 ? "matched-rgb-protected" : "legacy-ratio";
}
} // namespace nr_colour_restore
