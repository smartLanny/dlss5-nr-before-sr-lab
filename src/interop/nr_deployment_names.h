#pragma once
// Deliberately bounded ASCII filename recognition, not a DLL trust check.
// Luma and per-game RenoDX HDR are NOT classified as generic NR owners.
namespace nr_deployment_names {
inline wchar_t Lower(wchar_t c) noexcept { return c >= L'A' && c <= L'Z' ? c + 32 : c; }
inline unsigned Length(const wchar_t* name) noexcept {
    if (!name) return 0;
    unsigned n = 0;
    while (n < 160 && name[n]) ++n;
    return n == 160 ? 0 : n;
}
inline bool Equal(const wchar_t* a, const wchar_t* b) noexcept {
    const unsigned n = Length(a), m = Length(b);
    if (!n || n != m) return false;
    for (unsigned i = 0; i < n; ++i) if (Lower(a[i]) != Lower(b[i])) return false;
    return true;
}
inline bool Prefix(const wchar_t* name, const wchar_t* prefix) noexcept {
    const unsigned n = Length(name), m = Length(prefix);
    if (!m || n < m) return false;
    for (unsigned i = 0; i < m; ++i) if (Lower(name[i]) != Lower(prefix[i])) return false;
    return true;
}
inline bool GenericNr(const wchar_t* name) noexcept {
    constexpr wchar_t prefix[] = L"renodx-dlss5";
    constexpr wchar_t suffix[] = L".addon64";
    const unsigned n = Length(name), a = Length(prefix), b = Length(suffix);
    if (n < a + b || !Prefix(name, prefix) || !Equal(name + n - b, suffix)) return false;
    if (n == a + b) return true;
    unsigned i = a;
    if ((name[i] != L'-' && name[i] != L'_') || Lower(name[++i]) != L'v') return false;
    ++i;
    if (name[i] == L'_') ++i;
    if (i >= n - b || name[i] < L'0' || name[i] > L'9') return false;
    for (; i < n - b; ++i) {
        const auto c = name[i];
        if (!(c >= L'0' && c <= L'9') && c != L'.' && c != L'_' && c != L'-') return false;
    }
    return true;
}
inline bool Carrier(const wchar_t* name) noexcept {
    const unsigned n = Length(name);
    return Equal(name, L"dlss5-bridge.addon64") ||
        (n > 8 && Prefix(name, L"dlss5-native-carrier-") && Equal(name + n - 8, L".addon64"));
}
inline bool MatchingCarrier(const wchar_t* name) noexcept {
    return Equal(name, L"dlss5-native-carrier-045-dx11-compat.addon64");
}
} // namespace nr_deployment_names
