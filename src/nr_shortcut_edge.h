#pragma once
namespace nr_shortcut {
// Low-bit short taps are best effort; a held key must not repeat actions.
constexpr bool Pressed(short state, bool was_down) {
    return !was_down && (static_cast<unsigned short>(state) & 0x8001u) != 0;
}
}
