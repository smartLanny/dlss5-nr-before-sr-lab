#pragma once
#include <stdint.h>
// The fixed reference module uses this 16-row/32-column permutation for
// E4M3 activation bytes. A pair always begins at an even column.
__host__ __device__ inline uint32_t nr_original_pair_byte(uint32_t row,uint32_t pair,uint32_t cols) {
    const uint32_t hi=((row*4)&28)|(pair&3)|(((row>>4)*cols+pair*2)&0xfffffe0u);
    const uint32_t lo=(pair&8)|((row>>1)&4)|((pair>>1)&2);
    return hi*16+lo;
}
__host__ __device__ inline void nr_original_pair_coordinate(uint32_t pair,uint32_t&row,uint32_t&col) {
    row=((pair>>9)&0x3ffff0u)|((pair>>5)&7)|((pair*4)&8);
    col=((pair*4)&16)|((pair>>3)&0x3e0)|((pair*8)&8)|((pair>>2)&6);
}
__host__ __device__ inline uint32_t nr_scale_byte(uint32_t row,uint32_t group,uint32_t cols) {
    return (row>>7)*((cols*8+504)&0xfffffe00u)+((row*16)&496)+((row>>3)&12)+(group&3)+((group*128)&0xfffffe00u);
}
