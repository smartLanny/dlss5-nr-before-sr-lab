#pragma once
#include <stdint.h>
#if defined(__CUDACC__)
#define NR_EXPANSION_HD __host__ __device__
#else
#define NR_EXPANSION_HD
#endif

namespace nr_expansion {
// These register-coordinate functions are exhaustively compared on CPU with
// NVIDIA CUTLASS 4.7.1's SM120_16x8x64_TN_VS MMA traits. Matrix A contains weight
// features; matrix B contains token activations. Both MMA inputs are registers.
struct Coordinate { uint32_t row, column; };
NR_EXPANSION_HD inline Coordinate a_coordinate(uint32_t lane,uint32_t nibble) {
    return {(lane>>2)+8*((nibble>>3)&1),8*(lane&3)+(nibble&7)+32*(nibble>>4)};
}
NR_EXPANSION_HD inline Coordinate b_coordinate(uint32_t lane,uint32_t nibble) {
    return {lane>>2,8*(lane&3)+(nibble&7)+32*(nibble>>3)};
}
NR_EXPANSION_HD inline Coordinate c_coordinate(uint32_t lane,uint32_t value) {
    return {(lane>>2)+8*(value>>1),2*(lane&3)+(value&1)};
}
NR_EXPANSION_HD inline uint32_t a_scale_row(uint32_t lane) { return (lane>>2)+8*(lane&1); }
NR_EXPANSION_HD inline uint32_t b_scale_row(uint32_t lane) { return lane>>2; }
NR_EXPANSION_HD inline uint32_t token_tile(uint32_t variant) { return variant==0 ? 64 : (variant==1 ? 32 : 128); }
NR_EXPANSION_HD inline uint32_t padded_rows(uint32_t rows) { return (rows+127u)&~127u; }
// CTA-local padding only. MMA B loads span eight token rows per warp: 132
// packed words advance by four banks/row; 17 scale words give distinct banks
// to the eight row broadcasts. Global FP4/SF layouts retain 512/64-byte rows.
inline constexpr uint32_t packed_shared_stride=528;
inline constexpr uint32_t scale_shared_stride=68;
NR_EXPANSION_HD inline uint32_t shared_bytes(uint32_t tile) { return tile*(packed_shared_stride+scale_shared_stride); }
} // namespace nr_expansion
