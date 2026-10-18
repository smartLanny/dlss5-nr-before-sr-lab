#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Feature 18's create-time network selector. These are native parameter values,
// not Style, SR/RR presets, or the zero-based labels used by some other UIs.
namespace nr_network_policy {
inline constexpr int Inherit = -1;
inline constexpr int RuntimeDefault = 0;
inline constexpr int Original = 1;
inline constexpr int Optimized = 2;
inline constexpr int Unknown = -2;
inline constexpr const char* CreateParameter = "DLSSNR.Hint.Render.Preset";

constexpr int Resolve(int global, int layer_override) noexcept {
    return layer_override == Inherit ? global : layer_override;
}
constexpr bool IsValidRequest(int value) noexcept {
    return value >= RuntimeDefault && value <= Optimized;
}
constexpr bool IsValidOverride(int value) noexcept {
    return value == Inherit || IsValidRequest(value);
}
constexpr const char* Name(int value) noexcept {
    switch (value) {
    case RuntimeDefault: return "runtime-default";
    case Original: return "original";
    case Optimized: return "optimized";
    case Inherit: return "inherit-global";
    default: return "unknown";
    }
}

enum class Family : unsigned { Unknown, OriginalOnly, OriginalAndOptimized };
enum class Evidence : unsigned { Unknown, LoadedVersionHint, LoadedPathFileHash };
constexpr const char* EvidenceName(Evidence value) noexcept {
    switch (value) {
    case Evidence::LoadedVersionHint: return "loaded-version-hint";
    case Evidence::LoadedPathFileHash: return "loaded-path-file-hash";
    default: return "unknown";
    }
}
constexpr const char* FamilyName(Family value) noexcept {
    switch (value) {
    case Family::OriginalOnly: return "original-only";
    case Family::OriginalAndOptimized: return "original-and-optimized";
    default: return "unknown";
    }
}
struct Version {
    bool known = false;
    std::array<unsigned, 4> value{};
};

// Metadata alone does not establish the contents of a modified DLL. In
// particular, do not turn a future version or an arbitrary 310.9 build into a
// capability allow-list. A version hint never rejects a network request.
constexpr Family VersionHint(const Version& version) noexcept {
    if (!version.known) return Family::Unknown;
    if (version.value == std::array<unsigned, 4>{310, 8, 0, 0})
        return Family::OriginalOnly;
    if (version.value == std::array<unsigned, 4>{310, 9, 16, 0})
        return Family::OriginalAndOptimized;
    return Family::Unknown;
}
constexpr unsigned char LowerHex(unsigned char value) noexcept {
    return value >= 'A' && value <= 'F' ? static_cast<unsigned char>(value + ('a' - 'A')) : value;
}
inline bool SameDigest(const char* lhs, const char* rhs) noexcept {
    if (!lhs || !rhs) return false;
    for (unsigned i = 0; i < 64; ++i) {
        if (!lhs[i] || !rhs[i] || LowerHex(static_cast<unsigned char>(lhs[i])) !=
            LowerHex(static_cast<unsigned char>(rhs[i]))) return false;
    }
    return lhs[64] == 0 && rhs[64] == 0;
}
inline Family KnownFileFamily(const char* digest) noexcept {
    // Source receipts, not a local GPU or Authenticode validation:
    // alandtse/open-shaders#956, head 9bd81177956428fff5beaef8ecc170bb794fae29.
    // New-model identity is independently recorded by wilsjo2/...#128,
    // head 45437a2f8336794ecd4f1817b26fcf0d3e0a2634.
    if (SameDigest(digest, "2bc19bc09a7b89dd44f73e2ac0108fcc586e2154bdd0830f242f4f040e28789f"))
        return Family::OriginalAndOptimized;
    if (SameDigest(digest, "8270b350cd82de5ce89806872cdd6b6a9249b80836b91bbeb3573470744cc206") ||
        SameDigest(digest, "e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e"))
        return Family::OriginalOnly;
    return Family::Unknown;
}
constexpr bool FileVersionConsistent(Family known_file, const Version& loaded_version) noexcept {
    if (!loaded_version.known || known_file == Family::Unknown) return true;
    return VersionHint(loaded_version) == known_file;
}

struct Assessment {
    int requested = RuntimeDefault;
    int expected = Unknown;
    bool valid = true;
    bool unavailable = false;
    bool possible_fallback = false;
    Evidence evidence = Evidence::Unknown;
};
constexpr Assessment Assess(int requested, Family family, Evidence evidence) noexcept {
    Assessment result{requested, Unknown, IsValidRequest(requested), false, false, evidence};
    if (!result.valid || family == Family::Unknown) return result;
    if (family == Family::OriginalOnly) {
        result.expected = Original;
        result.possible_fallback = requested == Optimized;
        result.unavailable = result.possible_fallback && evidence == Evidence::LoadedPathFileHash;
    } else {
        result.expected = requested == RuntimeDefault ? Optimized : requested;
    }
    // Expected is always an inference from a file/metadata receipt. Neither
    // Create success nor reading the parameter back proves the executed model.
    return result;
}

template<class UIntWriter>
bool WriteCreateParameter(int request, UIntWriter&& write_uint) {
    if (!IsValidRequest(request)) return false;
    write_uint(CreateParameter, static_cast<unsigned>(request));
    return true;
}

// Decode only the bounded VS_VERSIONINFO root value from a loaded resource.
// No file read, version.dll dependency, module scan, or model execution.
inline Version ReadVersionResource(const unsigned char* bytes, std::size_t size) noexcept {
    Version result;
    if (!bytes || size < 6) return result;
    const auto u16 = [bytes](std::size_t offset) -> unsigned {
        return unsigned(bytes[offset]) | (unsigned(bytes[offset + 1]) << 8);
    };
    const auto u32 = [bytes](std::size_t offset) -> std::uint32_t {
        return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
            (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
    };
    const auto length = u16(0), value_length = u16(2), type = u16(4);
    if (length > size || length < 6 || value_length < 52 || type != 0) return result;
    constexpr char key[] = "VS_VERSION_INFO";
    std::size_t offset = 6;
    for (const char ch : key) {
        if (offset + 2 > length || u16(offset) != static_cast<unsigned char>(ch)) return result;
        offset += 2;
    }
    offset = (offset + 3u) & ~std::size_t(3u);
    if (offset > length || value_length > length - offset ||
        u32(offset) != 0xfeef04bdu || u32(offset + 4) != 0x00010000u) return result;
    const auto ms = u32(offset + 8), ls = u32(offset + 12);
    result.known = true;
    result.value = {ms >> 16, ms & 0xffffu, ls >> 16, ls & 0xffffu};
    return result;
}
}
