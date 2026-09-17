#pragma once

// Separate persistent output and optional input selectors. Only the input
// curve changes proxy preparation and NR history; neither selects a different
// model nor grants a resource/queue/lifetime contract.
namespace nr_colour_restore {
enum class Mode : int { Legacy = 0, SourceReferencedRgb = 1 };
enum class Encoding : unsigned { Unknown = 0, Post = 1, V2 = 2 };
struct EncodingReceipt {
    Encoding encoding = Encoding::Unknown;
    unsigned long long serial = 0;
    int input_curve = 0;
    constexpr Encoding For(unsigned long long current) const noexcept {
        return current != 0 && current == serial ? encoding : Encoding::Unknown;
    }
};
constexpr int kDefaultMode = 1;
constexpr int kDefaultInputCurve = 0;
constexpr unsigned kInputCurveFlag = 268435456u;
constexpr unsigned kRgbFlag = 1073741824u;
constexpr unsigned kPostEncodingFlag = 536870912u;
// uniform3's source/diagnostic/consumer flags occupy the low 24 bits.
static_assert(((kRgbFlag | kPostEncodingFlag | kInputCurveFlag) & 0x00ffffffu) == 0u);
constexpr int Normalize(int value) noexcept { return value == 1 ? 1 : 0; }
constexpr unsigned Flags(int mode, bool combine, Encoding encoding) noexcept {
    return Normalize(mode) == 1 && combine &&
        (encoding == Encoding::Post || encoding == Encoding::V2)
        ? kRgbFlag | (encoding == Encoding::Post ? kPostEncodingFlag : 0u) : 0u;
}
constexpr int EffectiveInputCurve(int request, unsigned source_flags) noexcept {
    return request == 1 && (source_flags & (1u | 16u | 32u)) == 0u ? 1 : 0;
}
constexpr const char *CurveName(int curve) noexcept {
    return curve == 1 ? "paired-n2-hdr" : "d21-input-unchanged";
}
constexpr const char *Name(int mode) noexcept {
    return Normalize(mode) == 1 ? "matched-rgb-source-reference" : "legacy-ratio";
}
} // namespace nr_colour_restore
