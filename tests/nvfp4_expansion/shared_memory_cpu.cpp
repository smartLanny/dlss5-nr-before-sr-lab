// Pure CPU proof of CTA-local bank addresses and producer/consumer footprints.
// Equal 32-bit addresses are broadcasts, not separate bank conflicts.
#include "../../kernels/nvfp4_expansion/fragment_layout.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <set>
#include <vector>

static unsigned conflicts(const std::array<uint32_t,32>& addresses) {
    std::array<std::set<uint32_t>,32> banks;
    for(const auto address:addresses) {
        assert(address%4==0);
        banks[(address/4)%32].insert(address/4);
    }
    unsigned maximum=0;
    for(const auto& bank:banks)maximum=std::max(maximum,unsigned(bank.size()));
    return maximum;
}

static uint8_t pattern(uint32_t row,uint32_t column,uint32_t salt) {
    uint32_t value=(row+1)*0x45d9f3bu^(column+1)*0x119de1f3u^salt;
    return uint8_t((value^(value>>11))>>19);
}

int main() {
    constexpr uint32_t Packed=nr_expansion::packed_shared_stride;
    constexpr uint32_t Scale=nr_expansion::scale_shared_stride;
    static_assert(Packed>=512&&Packed%4==0&&Scale>=64&&Scale%4==0);
    uint64_t warp_loads=0,checked_bytes=0;
    for(uint32_t variant=0;variant<3;++variant) {
        const uint32_t tile=nr_expansion::token_tile(variant),scale_base=tile*Packed;
        const uint32_t capacity=nr_expansion::shared_bytes(tile);
        assert(capacity==tile*(Packed+Scale)&&capacity<=101376);
        std::vector<uint8_t> guarded(capacity+512,0xA5);
        auto* shared=guarded.data()+256;
        std::vector<unsigned> writes(capacity,0);
        // Same logical packing work as the CTA: padding bytes remain unowned.
        for(uint32_t thread=0;thread<256;++thread)for(uint32_t i=thread;i<tile*512;i+=256) {
            const uint32_t row=i/512,pair=i%512,packed_address=row*Packed+pair;
            assert(packed_address<scale_base);
            shared[packed_address]=pattern(row,pair,0x12345678u);++writes[packed_address];
            if((i&7)==0) {
                const uint32_t sf=scale_base+row*Scale+pair/8;
                assert(sf<capacity);shared[sf]=pattern(row,pair/8,0x87654321u);++writes[sf];
            }
        }
        unsigned max_old_packed=0,max_new_packed=0,max_old_scale=0,max_new_scale=0;
        for(uint32_t token=0;token<tile/8;++token)for(uint32_t k=0;k<1024;k+=64) {
            std::array<uint32_t,32> old_b{},new_b{},old_sf{},new_sf{};
            for(uint32_t lane=0;lane<32;++lane) {
                const uint32_t row=token*8+nr_expansion::b_scale_row(lane),pair=(k+8*(lane&3))/2;
                old_sf[lane]=tile*512+row*64+k/16;
                new_sf[lane]=scale_base+row*Scale+k/16;
                assert(new_sf[lane]+4<=scale_base+row*Scale+64&&new_sf[lane]+4<=capacity);
                for(uint32_t byte=0;byte<4;++byte) {
                    assert(writes[new_sf[lane]+byte]==1);
                    assert(shared[new_sf[lane]+byte]==pattern(row,k/16+byte,0x87654321u));++checked_bytes;
                }
                for(uint32_t offset:{0u,16u}) {
                    const uint32_t address=row*Packed+pair+offset;
                    assert(address%4==0&&address+4<=row*Packed+512&&address+4<=scale_base);
                    for(uint32_t byte=0;byte<4;++byte) {
                        assert(writes[address+byte]==1);
                        assert(shared[address+byte]==pattern(row,pair+offset+byte,0x12345678u));++checked_bytes;
                    }
                }
            }
            // Scale loads contain four identical lane addresses per row.
            assert(conflicts(old_sf)==4&&conflicts(new_sf)==1);
            max_old_scale=std::max(max_old_scale,conflicts(old_sf));
            max_new_scale=std::max(max_new_scale,conflicts(new_sf));++warp_loads;
            for(uint32_t offset:{0u,16u}) {
                for(uint32_t lane=0;lane<32;++lane) {
                    const uint32_t row=token*8+nr_expansion::b_scale_row(lane),pair=(k+8*(lane&3))/2;
                    old_b[lane]=row*512+pair+offset;
                    new_b[lane]=row*Packed+pair+offset;
                }
                assert(conflicts(old_b)==8&&conflicts(new_b)==1);
                max_old_packed=std::max(max_old_packed,conflicts(old_b));
                max_new_packed=std::max(max_new_packed,conflicts(new_b));++warp_loads;
            }
        }
        for(uint32_t row=0;row<tile;++row) {
            for(uint32_t byte=512;byte<Packed;++byte)assert(writes[row*Packed+byte]==0&&shared[row*Packed+byte]==0xA5);
            for(uint32_t byte=64;byte<Scale;++byte)assert(writes[scale_base+row*Scale+byte]==0&&shared[scale_base+row*Scale+byte]==0xA5);
        }
        for(uint32_t byte=0;byte<256;++byte)assert(guarded[byte]==0xA5&&guarded[256+capacity+byte]==0xA5);
        std::printf("{\"variant\":%u,\"token_rows\":%u,\"packed_stride\":%u,\"scale_stride\":%u,"
                    "\"shared_bytes\":%u,\"packed_conflict_before\":%u,\"packed_conflict_after\":%u,"
                    "\"scale_conflict_before\":%u,\"scale_conflict_after\":%u,\"padding_and_guards_intact\":true}\n",
                    variant,tile,Packed,Scale,capacity,max_old_packed,max_new_packed,max_old_scale,max_new_scale);
    }
    std::printf("{\"warp_load_mappings_checked\":%llu,\"consumer_bytes_checked\":%llu,"
                "\"passed\":true,\"cuda_initialized\":false,\"gpu_launches\":0}\n",
                (unsigned long long)warp_loads,(unsigned long long)checked_bytes);
}
