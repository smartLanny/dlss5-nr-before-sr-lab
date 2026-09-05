#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Metadata-only classification. Never follows a pointer or changes code. In
// particular an existing EB F9 / E9 hotpatch is NOT permission to overwrite its
// prefix or its previous owner's anonymous context relay.
namespace nr_hook_entry {
enum class Kind { Unreadable, Ordinary, ShortHotpatch, RelativeJump, IndirectJump, ContextRelay };
inline Kind Classify(const unsigned char *entry, std::size_t count,
                     const unsigned char *prefix, std::size_t prefix_count) {
    if (!entry || count < 2) return Kind::Unreadable;
    if (entry[0] == 0xEB && entry[1] == 0xF9 && prefix &&
        prefix_count == 5 && prefix[0] == 0xE9) return Kind::ShortHotpatch;
    if (entry[0] == 0xE9 && count >= 5) return Kind::RelativeJump;
    if (entry[0] == 0xFF && entry[1] == 0x25 && count >= 6) return Kind::IndirectJump;
    if (entry[0] == 0x49 && entry[1] == 0xBA && count >= 10) return Kind::ContextRelay;
    return Kind::Ordinary;
}
inline const char *Name(Kind kind) {
    switch (kind) {
    case Kind::Unreadable: return "unreadable";
    case Kind::ShortHotpatch: return "existing-short-hotpatch";
    case Kind::RelativeJump: return "relative-jump";
    case Kind::IndirectJump: return "indirect-jump";
    case Kind::ContextRelay: return "register-context-relay";
    default: return "ordinary-or-unsupported";
    }
}
} // namespace nr_hook_entry
