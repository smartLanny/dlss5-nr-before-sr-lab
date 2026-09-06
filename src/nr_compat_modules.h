#pragma once

#include <cstddef>
#include <cwchar>
#include <cwctype>

namespace nr_compat_modules {

// Competing Feature 18 owners are identified by add-on file name, not by a
// single hard-coded LoadLibrary stem. Community copies are often versioned
// (renodx-dlss_v20260904.addon64) and GetModuleHandleW(exact-name) misses them.
enum class Kind { None, ShortFuse, GenericDlss5 };

inline const wchar_t *FileName(const wchar_t *path) noexcept {
    if (path == nullptr || path[0] == L'\0') return L"";
    const wchar_t *name = path;
    for (const wchar_t *cursor = path; *cursor != L'\0'; ++cursor) {
        if (*cursor == L'\\' || *cursor == L'/') name = cursor + 1;
    }
    return name;
}

inline wchar_t AsciiLower(wchar_t value) noexcept {
    return (value >= L'A' && value <= L'Z')
        ? static_cast<wchar_t>(value - L'A' + L'a')
        : value;
}

inline bool EqualsI(const wchar_t *left, const wchar_t *right) noexcept {
    if (left == nullptr || right == nullptr) return false;
    while (*left != L'\0' && *right != L'\0') {
        if (AsciiLower(*left++) != AsciiLower(*right++)) return false;
    }
    return *left == L'\0' && *right == L'\0';
}

inline bool StartsWithI(const wchar_t *text, const wchar_t *prefix) noexcept {
    if (text == nullptr || prefix == nullptr) return false;
    while (*prefix != L'\0') {
        if (*text == L'\0' || AsciiLower(*text++) != AsciiLower(*prefix++))
            return false;
    }
    return true;
}

inline bool EndsWithI(const wchar_t *text, const wchar_t *suffix) noexcept {
    if (text == nullptr || suffix == nullptr) return false;
    const std::size_t text_n = wcslen(text);
    const std::size_t suffix_n = wcslen(suffix);
    if (suffix_n > text_n) return false;
    return EqualsI(text + (text_n - suffix_n), suffix);
}

inline Kind ClassifyAddonFileName(const wchar_t *path_or_name) noexcept {
    const wchar_t *name = FileName(path_or_name);
    if (!EndsWithI(name, L".addon64")) return Kind::None;
    // Generic RenoDX DLSS5 is a complete Feature 18 owner. Accept the stock
    // name and the common versioned/suffixed copies.
    if (EqualsI(name, L"renodx-dlss5.addon64") ||
        StartsWithI(name, L"renodx-dlss5.") ||
        StartsWithI(name, L"renodx-dlss5-") ||
        StartsWithI(name, L"renodx-dlss5_")) {
        return Kind::GenericDlss5;
    }
    // ShortFuse / RenoDX DLSS sidecar. Match the exact stem and versioned
    // copies (underscore or hyphen). Do not match longer stems such as
    // renodx-dlssfix.addon64 or renodx-dlssnr.addon64.
    if (EqualsI(name, L"renodx-dlss.addon64") ||
        StartsWithI(name, L"renodx-dlss_") ||
        StartsWithI(name, L"renodx-dlss-")) {
        return Kind::ShortFuse;
    }
    return Kind::None;
}

inline const char *KindName(Kind kind) noexcept {
    switch (kind) {
    case Kind::ShortFuse: return "renodx-dlss";
    case Kind::GenericDlss5: return "renodx-dlss5";
    case Kind::None:
    default: return "none";
    }
}

}  // namespace nr_compat_modules
