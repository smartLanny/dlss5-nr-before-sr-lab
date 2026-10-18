#pragma once

#include <array>
#include <cerrno>
#include <cwchar>
#include <cwctype>
#include <limits>
#include <iterator>
#include "nr_network_policy.h"

// A separate namespace/key family: historical NRPreset and Style never select
// the new network. Every key is independently optional; no migration marker
// may erase an explicit override, including one on a disabled layer.
namespace nr_network_settings {
inline constexpr int InvalidText = std::numeric_limits<int>::min();
inline constexpr const wchar_t* LayerKeys[] = {
    L"Layer1NetworkPreset", L"Layer2NetworkPreset", L"Layer3NetworkPreset",
    L"Layer4NetworkPreset", L"Layer5NetworkPreset"
};

inline int Parse(const wchar_t* text, int fallback) noexcept {
    if (!text) return fallback;
    while (*text && std::iswspace(static_cast<std::wint_t>(*text))) ++text;
    if (!*text) return fallback;
    wchar_t* end = nullptr;
    errno = 0;
    const auto value = std::wcstoll(text, &end, 10);
    if (end == text || errno == ERANGE) return InvalidText;
    while (*end && std::iswspace(static_cast<std::wint_t>(*end))) ++end;
    if (*end || value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max()) return InvalidText;
    // Preserve unknown numeric IDs. The runtime rejects unsupported requests;
    // normalization and saving must not silently turn them into another model.
    return static_cast<int>(value);
}

template<class Config, class Reader>
void Load(Config& config, Reader&& read) {
    static_assert(std::tuple_size_v<decltype(config.nr_layers)> + 1 == std::size(LayerKeys));
    config.nr_network_preset = read(L"NRNetworkPreset", nr_network_policy::RuntimeDefault);
    config.nr_primary_network_preset = read(LayerKeys[0], nr_network_policy::Inherit);
    for (unsigned i = 0; i < config.nr_layers.size(); ++i)
        config.nr_layers[i].network_preset = read(LayerKeys[i + 1], nr_network_policy::Inherit);
}

template<class Config, class Writer>
void Save(const Config& config, Writer&& write) {
    static_assert(std::tuple_size_v<decltype(config.nr_layers)> + 1 == std::size(LayerKeys));
    // The caller observes every write and records its first failure. A failed
    // disk write never modifies the live request or certifies a schema marker.
    write(L"NRNetworkPreset", config.nr_network_preset);
    write(LayerKeys[0], config.nr_primary_network_preset);
    for (unsigned i = 0; i < config.nr_layers.size(); ++i)
        write(LayerKeys[i + 1], config.nr_layers[i].network_preset);
}
}
