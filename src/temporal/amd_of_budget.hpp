#pragma once
#include <cstdint>
#include <limits>
namespace nr::final_residual {
// Pinned FFX 1.1.4 opticalflow internalSurfaceDesc: two seven-level R8
// luma pyramids, two seven-level RG16 flow pyramids, three SCD textures.
// Plus borrowed AmdOfProducer input/raw/SCD. No GPU resources are created here.
// The callback queries actual D3D12 allocation sizes, not pixel-size guesses.
// Extra 16 MiB covers non-texture creation overhead conservatively; actual SDK
// memory usage and later snapshot increments remain independently budgeted.
template<class Allocation> std::uint64_t AmdBorrowedCreationAllowance(unsigned w,unsigned h,Allocation allocation) {
    if(w<128||h<128||w>4096||h>4096)return 0;
    std::uint64_t total=16ull<<20;
    const auto add=[&](unsigned x,unsigned y,unsigned format){
        const auto bytes=allocation(x,y,format);
        if(!bytes||bytes==UINT64_MAX||bytes>UINT64_MAX-total)return false;
        total+=bytes;return true;
    };
    for(unsigned copy=0;copy<2;++copy){
        unsigned fw=(w+7)/8,fh=(h+7)/8;
        for(unsigned level=0;level<7;++level){
            if(!add(w>>level,h>>level,62)||!add(fw,fh,38))return 0; // R8_UINT / R16G16_SINT
            fw=(fw+1)/2;fh=(fh+1)/2;
        }
    }
    if(!add(256*3*3,1,42)||!add(256*3*3,1,41)||!add(3,1,42)||
       !add(w,h,28)||!add((w+7)/8,(h+7)/8,38)||!add(3,1,42))return 0;
    return total;
}
}
