#include "nr_network_policy.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <vector>

namespace {
unsigned checks = 0;
void Check(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
std::vector<unsigned char> VersionResource(unsigned major, unsigned minor, unsigned build, unsigned revision) {
    std::vector<unsigned char> bytes(92);
    const auto u16 = [&](unsigned at, unsigned value) {
        bytes.at(at) = static_cast<unsigned char>(value);
        bytes.at(at + 1) = static_cast<unsigned char>(value >> 8);
    };
    const auto u32 = [&](unsigned at, unsigned value) { u16(at, value); u16(at + 2, value >> 16); };
    u16(0, static_cast<unsigned>(bytes.size())); u16(2, 52); u16(4, 0);
    constexpr char key[] = "VS_VERSION_INFO";
    for (unsigned i = 0; i < sizeof(key); ++i) u16(6 + i * 2, key[i]);
    u32(40, 0xfeef04bd); u32(44, 0x00010000);
    u32(48, (major << 16) | minor); u32(52, (build << 16) | revision);
    return bytes;
}
}

int main() {
    using namespace nr_network_policy;
    Check(Resolve(Original, Inherit) == Original, "inherit resolves against global");
    Check(Resolve(Original, RuntimeDefault) == RuntimeDefault, "explicit automatic does not inherit original");
    Check(Resolve(Optimized, Original) == Original, "explicit original remains independent");
    Check(Resolve(31, Inherit) == 31 && Resolve(1, 31) == 31, "unknown saved IDs remain visible");
    Check(!IsValidRequest(Inherit) && IsValidOverride(Inherit), "inherit is config syntax, never a native network");
    unsigned writes = 0, observed = 99;
    const auto writer = [&](const char* key, auto value) {
        static_assert(std::is_same_v<decltype(value), unsigned>, "NGX network uses UInt setter");
        Check(std::strcmp(key, "DLSSNR.Hint.Render.Preset") == 0, "exact Feature 18 create key");
        ++writes; observed = value;
    };
    for (int request = 0; request <= 2; ++request) {
        Check(WriteCreateParameter(request, writer), "known native request bound");
        Check(observed == static_cast<unsigned>(request), "UI value is not offset or substituted");
    }
    Check(!WriteCreateParameter(-1, writer) && !WriteCreateParameter(3, writer), "invalid request writes nothing");
    Check(writes == 3, "invalid request cannot masquerade as automatic");

    const auto old = VersionResource(310, 8, 0, 0), modern = VersionResource(310, 9, 16, 0);
    const auto old_version = ReadVersionResource(old.data(), old.size());
    const auto new_version = ReadVersionResource(modern.data(), modern.size());
    Check(old_version.known && VersionHint(old_version) == Family::OriginalOnly, "bounded old version hint");
    Check(new_version.known && new_version.value[2] == 16 && VersionHint(new_version) == Family::OriginalAndOptimized, "four-part loaded version hint");
    auto future = VersionResource(310, 10, 1, 0);
    Check(VersionHint(ReadVersionResource(future.data(), future.size())) == Family::Unknown, "future versions are not assumed compatible");
    for (std::size_t size = 0; size < modern.size(); ++size)
        Check(!ReadVersionResource(modern.data(), size).known, "truncated resource never overreads or produces identity");
    auto malformed = modern; malformed[6] = 'x';
    Check(!ReadVersionResource(malformed.data(), malformed.size()).known, "wrong resource key refused");
    malformed = modern; malformed[2] = 255;
    Check(!ReadVersionResource(malformed.data(), malformed.size()).known, "out-of-bounds fixed value refused");
    malformed = modern; malformed[4] = 1;
    Check(!ReadVersionResource(malformed.data(), malformed.size()).known, "text value cannot impersonate fixed metadata");
    malformed = modern; malformed[40] = 0;
    Check(!ReadVersionResource(malformed.data(), malformed.size()).known, "fixed-info signature required");

    constexpr char modern_hash[] = "2BC19BC09A7B89DD44F73E2AC0108FCC586E2154BDD0830F242F4F040E28789F";
    Check(KnownFileFamily(modern_hash) == Family::OriginalAndOptimized, "known source receipt accepts case-insensitive SHA");
    Check(FileVersionConsistent(KnownFileFamily(modern_hash), new_version), "known hash and loaded version agree");
    Check(!FileVersionConsistent(KnownFileFamily(modern_hash), old_version), "changed file at a loaded path cannot override contradictory loaded metadata");
    Check(KnownFileFamily("unknown") == Family::Unknown && KnownFileFamily(nullptr) == Family::Unknown, "missing hash has no model evidence");
    const auto old_hint = Assess(Optimized, Family::OriginalOnly, Evidence::LoadedVersionHint);
    Check(old_hint.valid && old_hint.possible_fallback && !old_hint.unavailable, "version metadata warns without rejecting a modified runtime");
    const auto old_hash = Assess(Optimized, Family::OriginalOnly, Evidence::LoadedPathFileHash);
    Check(old_hash.unavailable && old_hash.expected == Original, "known missing network cannot be reported as adopted optimized");
    const auto automatic = Assess(RuntimeDefault, Family::OriginalAndOptimized, Evidence::LoadedPathFileHash);
    Check(automatic.expected == Optimized && !automatic.unavailable, "new automatic is inferred small");
    Check(Assess(Original, Family::OriginalAndOptimized, Evidence::LoadedPathFileHash).expected == Original, "new DLL retains explicit original");
    Check(Assess(Optimized, Family::Unknown, Evidence::Unknown).expected == Unknown, "Create or request alone cannot prove executed network");
    Check(!Assess(15, Family::OriginalAndOptimized, Evidence::LoadedPathFileHash).valid, "unknown request is refused instead of clamped");
    std::printf("PASS %u network policy/typed binding/version bounds/evidence checks; CPU only, no NGX or game claim\n", checks);
}
