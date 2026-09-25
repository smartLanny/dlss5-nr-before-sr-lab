#pragma once

// One output-only bit; colour4 owns bit25 and colour3 owns bits26..30.
namespace nr_colour_edge {
constexpr unsigned kFlag = 1u << 24;
constexpr int Normalize(int mode) noexcept { return mode == 1 ? 1 : 0; }
constexpr unsigned Flags(int request, bool combine, bool rgb,
                         bool encoder_known, bool v2, bool scaled) noexcept {
    return Normalize(request) && combine && rgb && encoder_known && v2 && scaled ? kFlag : 0u;
}
constexpr const char *Name(bool active) noexcept {
    return active ? "matched-positive-edge-weights" : "d21-bilinear";
}
} // namespace nr_colour_edge
