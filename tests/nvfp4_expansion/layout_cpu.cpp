#include <cuda_runtime.h>
#include "cute/atom/mma_traits_sm120.hpp"
#include "../../kernels/nvfp4_expansion/fragment_layout.h"
#include "../../kernels/nvfp4_io/original_layout.cuh"
#include <array>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <set>
#include <vector>

using Op=cute::SM120::BLOCKSCALED::SM120_16x8x64_TN_VS<cute::float_e2m1_t,cute::float_e2m1_t,float,cute::float_ue4m3_t,16>;
using Traits=cute::MMA_Traits<Op>;

int main() {
    size_t checks=0;
    std::array<unsigned,16*64> a_coverage{};
    std::array<unsigned,8*64> b_coverage{};
    std::array<unsigned,16*8> c_coverage{};
    for(uint32_t lane=0;lane<32;++lane) {
        for(uint32_t value=0;value<32;++value) {
            const auto got=nr_expansion::a_coordinate(lane,value);
            const uint32_t expected=Traits::ALayout{}(cute::make_coord(lane,value));
            assert(got.row==expected%16 && got.column==expected/16);
            assert(got.row<16 && got.column<64); ++a_coverage[got.row*64+got.column]; ++checks;
        }
        for(uint32_t value=0;value<16;++value) {
            const auto got=nr_expansion::b_coordinate(lane,value);
            const uint32_t expected=Traits::BLayout{}(cute::make_coord(lane,value));
            assert(got.row==expected%8 && got.column==expected/8);
            assert(got.row<8 && got.column<64); ++b_coverage[got.row*64+got.column]; ++checks;
        }
        for(uint32_t value=0;value<4;++value) {
            const auto got=nr_expansion::c_coordinate(lane,value);
            const uint32_t expected=Traits::CLayout{}(cute::make_coord(lane,value));
            assert(got.row==expected%16 && got.column==expected/16);
            assert(got.row<16 && got.column<8); ++c_coverage[got.row*8+got.column]; ++checks;
        }
        for(uint32_t k=0;k<64;++k) {
            const auto a=Traits::SFALayout{}(cute::make_coord(lane,k));
            const auto b=Traits::SFBLayout{}(cute::make_coord(lane,k));
            assert(nr_expansion::a_scale_row(lane)==uint32_t(a)%16 && uint32_t(a)/16==k);
            assert(nr_expansion::b_scale_row(lane)==uint32_t(b)%8 && uint32_t(b)/8==k);
            ++checks;
        }
        // The reduction XORs 4,8,16 hold token columns fixed and collect exactly
        // 16 feature values (the lower and upper output register rows).
        std::set<uint32_t> reduced_features;
        const auto token=nr_expansion::c_coordinate(lane,0).column;
        for(uint32_t neighbor=lane&3;neighbor<32;neighbor+=4) {
            for(uint32_t value:{0u,2u}) {
                auto c=nr_expansion::c_coordinate(neighbor,value);
                assert(c.column==token); reduced_features.insert(c.row);
            }
        }
        assert(reduced_features.size()==16);
        if(((lane>>2)&1)==0) {
            for(uint32_t value=0;value<4;++value) {
                const auto c=nr_expansion::c_coordinate(lane,value),adjacent=nr_expansion::c_coordinate(lane^4,value);
                assert(c.column==adjacent.column && adjacent.row==c.row+1); ++checks;
            }
        }
    }
    assert(std::all_of(a_coverage.begin(),a_coverage.end(),[](unsigned n){return n==1;}));
    assert(std::all_of(b_coverage.begin(),b_coverage.end(),[](unsigned n){return n==1;}));
    assert(std::all_of(c_coverage.begin(),c_coverage.end(),[](unsigned n){return n==1;}));

    for(uint32_t rows:{1u,7u,8u,15u,16u,31u,32u,63u,64u,127u,128u,129u,640u,641u,960u,961u,2160u,3840u}) {
        const uint32_t padded=nr_expansion::padded_rows(rows);
        for(uint32_t variant=0;variant<3;++variant) {
            const uint32_t tile=nr_expansion::token_tile(variant);
            std::vector<uint8_t> sf_seen(padded*256);
            std::vector<uint8_t> output_seen(padded*2048);
            uint64_t original_loads=0;
            // Verify one feature CTA for every token tile; the footprint is the
            // same for the other feature CTAs, which duplicate input packing.
            for(uint32_t base=0;base<padded;base+=tile) {
                for(uint32_t thread=0;thread<256;++thread) for(uint32_t i=thread;i<tile*512;i+=256) {
                    const uint32_t row=base+i/512,pair=i%512;
                    assert((i/512)==((i^1)/512) && (i/512)==((i^2)/512) && (i/512)==((i^4)/512));
                    if(row<rows) {
                        const uint32_t address=nr_original_pair_byte(row,pair,1024);
                        assert(address%2==0 && address+2<=((rows+15)&~15u)*1024); ++original_loads;
                    }
                }
            }
            assert(original_loads==uint64_t(rows)*512);
            for(uint32_t base=0;base<padded;base+=tile) for(uint32_t feature_cta=0;feature_cta<32;++feature_cta) {
                for(uint32_t warp=0;warp<8;++warp) for(uint32_t token=0;token<tile/8;++token) for(uint32_t lane=0;lane<32;++lane) {
                    const uint32_t feature=feature_cta*128+warp*16;
                    const uint32_t row0=base+token*8+2*(lane&3),row1=row0+1,col=feature+(lane>>2);
                    if(((lane>>2)&1)==0) for(uint32_t row:{row0,row1}) for(uint32_t output_col:{col,col+8}) {
                        const uint32_t offset=row*2048+output_col/2;
                        assert(offset<output_seen.size()); ++output_seen[offset];
                    }
                    if((lane>>2)==0) for(uint32_t row:{row0,row1}) {
                        const uint32_t offset=nr_scale_byte(row,feature/16,4096);
                        assert(offset<sf_seen.size()); ++sf_seen[offset];
                    }
                }
            }
            assert(std::all_of(output_seen.begin(),output_seen.end(),[](uint8_t n){return n==1;}));
            assert(std::all_of(sf_seen.begin(),sf_seen.end(),[](uint8_t n){return n==1;}));
            checks+=output_seen.size()+sf_seen.size()+original_loads;
        }
    }
    std::cout<<"CPU CUTLASS fragment, scale, pack footprint, output ownership and tail checks: "<<checks<<" passed; no GPU launched\n";
}
