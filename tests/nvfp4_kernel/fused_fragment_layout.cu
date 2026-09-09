// CPU-only enumeration of the actual CUTLASS consumer coordinates. No CUDA
// context, device query, kernel launch, or private Params offset is used.
#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>
#include "fused_contract_kernel.cuh"

int main() {
    using namespace cute;
    using Contract = nr_nvfp4::FusedContract;
    using Epilogue = Contract::Epilogue;
    using TiledMma = Contract::Mainloop::TiledMma;
    constexpr int fragment = Epilogue::DispatchPolicy::FragmentSize;
    auto copy_atom = make_tiled_copy_C_atom(typename Epilogue::CopyAtomC{}, TiledMma{});
    auto copy = make_tiled_copy_S(
        Copy_Atom<typename Epilogue::CopyOpR2S,nr_nvfp4::OriginalFp8Store::ElementAux>{}, copy_atom);
    const int threads = size(copy);
    constexpr int tile_m = size<0>(Contract::Tile{}), tile_n = size<1>(Contract::Tile{});
    struct Owner { int thread=-1,em=-1,en=-1,vector=-1,index=-1; };
    std::array<Owner,tile_m*tile_n> owners{};
    std::uint64_t coordinates=0, same_thread_pairs=0, same_fragment_pairs=0, neighbor_pairs=0;
    std::uint64_t same_column_row_pairs=0;
    auto identity = make_identity_tensor(make_shape(1024,3840,1));
    for(int thread=0;thread<threads;++thread) {
        auto partition = cutlass::epilogue::fusion::detail::sm90_partition_for_epilogue<true>(
            identity, Contract::Tile{}, make_coord(0,0,0,0), typename Epilogue::EpilogueTile{}, copy, thread);
        for(int em=0;em<size<3>(partition);++em)for(int en=0;en<size<4>(partition);++en) {
            auto elements=coalesce(partition(_,_,_,em,en));
            for(int vector=0;vector<size(elements)/fragment;++vector)for(int index=0;index<fragment;++index) {
                auto point=elements(vector*fragment+index);
                const int col=get<0>(point),row=get<1>(point);
                if(col<0||col>=tile_m||row<0||row>=tile_n) return 2;
                auto& owner=owners[row*tile_m+col];
                if(owner.thread!=-1)return 3;
                owner={thread,em,en,vector,index};++coordinates;
                if(thread<8&&em==0&&en==0&&vector==0)
                    std::printf("thread=%d fragment_index=%d col=%d row=%d\n",thread,index,col,row);
                if(index%2==0) {
                    const auto next=elements(vector*fragment+index+1);
                    if(get<0>(next)==col&&get<1>(next)==row+1)++same_column_row_pairs;
                }
            }
        }
    }
    for(int row=0;row<tile_n;++row)for(int col=0;col<tile_m;col+=2) {
        const auto& a=owners[row*tile_m+col];const auto& b=owners[row*tile_m+col+1];
        if(a.thread<0||b.thread<0)return 4;
        same_thread_pairs+=a.thread==b.thread;
        same_fragment_pairs+=a.thread==b.thread&&a.em==b.em&&a.en==b.en&&a.vector==b.vector;
        neighbor_pairs+=(a.thread^b.thread)==nr_nvfp4::OriginalFp8Store::kNeighborLaneXor
            &&a.thread/32==b.thread/32&&a.em==b.em&&a.en==b.en&&a.vector==b.vector&&a.index==b.index;
    }
    std::printf("{\"cuda_initialized\":false,\"gpu_launches\":0,\"threads\":%d,\"fragment_size\":%d,"
                "\"tile_coordinates\":%llu,\"same_thread_column_pairs\":%llu,\"same_fragment_column_pairs\":%llu,"
                "\"neighbor_same_fragment_index_pairs\":%llu,\"same_column_adjacent_row_pairs\":%llu}\n",threads,fragment,
                (unsigned long long)coordinates,(unsigned long long)same_thread_pairs,
                (unsigned long long)same_fragment_pairs,(unsigned long long)neighbor_pairs,
                (unsigned long long)same_column_row_pairs);
    if(coordinates!=tile_m*tile_n || fragment!=nr_nvfp4::OriginalFp8Store::kFragmentSize
        || neighbor_pairs!=tile_m*tile_n/2 || same_column_row_pairs!=tile_m*tile_n/2)return 5;
    // Exercise the actual host implementations of scalar and half2 intrinsics.
    // This is CPU semantic evidence, not a substitute for the GPU byte oracle.
    std::uint64_t arithmetic_pairs=0;
    for(unsigned index=0;index<262144;++index) {
        const uint8_t low=uint8_t((index%127)|((index&1)<<7));
        const uint8_t high=uint8_t(((index*37)%127)|(((index>>1)&1)<<7));
        const __half cosine=__float2half_rn(index%31==0 ? 512.f : (float(int(index%4096)-2048)/1024.f));
        const float a=float(int((index*97)%32768)-16384)/32.f + 0.000244140625f;
        const float b=float(int((index*13)%32768)-16384)/32.f - 0.000244140625f;
        const __half2 gemm=__floats2half2_rn(a,b);
        const __half2 skip=__nv_cvt_fp8x2_to_halfraw2(uint16_t(low)|(uint16_t(high)<<8),__NV_E4M3);
        const uint16_t actual=__nv_cvt_halfraw2_to_fp8x2(
            __hadd2(gemm,__hmul2_rn(skip,__half2half2(cosine))),__NV_SATFINITE,__NV_E4M3);
        const uint16_t expected=uint16_t(__nv_cvt_halfraw_to_fp8(
            __hadd(__float2half_rn(a),__hmul_rn(__nv_cvt_fp8_to_halfraw(low,__NV_E4M3),cosine)),
            __NV_SATFINITE,__NV_E4M3)) | (uint16_t(__nv_cvt_halfraw_to_fp8(
            __hadd(__float2half_rn(b),__hmul_rn(__nv_cvt_fp8_to_halfraw(high,__NV_E4M3),cosine)),
            __NV_SATFINITE,__NV_E4M3))<<8);
        if(actual!=expected)return 6;
        ++arithmetic_pairs;
    }
    // Each vector store owns two neighboring channels. Enumerate the actual
    // owner table across CTA offsets; the packed prefix must be covered once.
    for(const unsigned rows:{1u,127u,128u,129u,640u,641u,960u,961u,2160u,2173u,3840u}) {
        const unsigned bytes=rows*1024,padded=(rows+15)/16*16;
        std::vector<uint8_t> coverage(bytes,0);
        std::vector<uint8_t> guarded(bytes+512,0xA5);
        unsigned stores=0,padded_values=0;
        for(unsigned row_tile=0;row_tile<(rows+127)/128;++row_tile)
        for(unsigned col_tile=0;col_tile<8;++col_tile)
        for(unsigned row=0;row<tile_n;++row)for(unsigned col=0;col<tile_m;col+=2) {
            const auto& a=owners[row*tile_m+col];const auto& b=owners[row*tile_m+col+1];
            if((a.thread^b.thread)!=nr_nvfp4::OriginalFp8Store::kNeighborLaneXor)return 7;
            const unsigned global_row=row_tile*tile_n+row,global_col=col_tile*tile_m+col;
            const unsigned byte=nr_original_pair_byte(global_row,global_col/2,1024);
            if(global_row<padded&&byte+1<bytes) {
                if(byte&1)return 8;
                ++coverage[byte];++coverage[byte+1];
                guarded[256+byte]=uint8_t(global_row*13+global_col);
                guarded[256+byte+1]=uint8_t(global_row*13+global_col+1);
                ++stores;padded_values+=global_row>=rows?2:0;
            }
        }
        for(unsigned byte=0;byte<bytes;++byte) {
            if(coverage[byte]!=1)return 9;
            // Independent inverse from physical pair index to logical point.
            const unsigned pair=byte/2;
            const unsigned row=((pair>>9)&0x3ffff0u)|((pair>>5)&7)|((pair*4)&8);
            const unsigned col=((pair*4)&16)|((pair>>3)&0x3e0)|((pair*8)&8)|((pair>>2)&6)|(byte&1);
            if(guarded[256+byte]!=uint8_t(row*13+col))return 10;
        }
        for(unsigned byte=0;byte<256;++byte)if(guarded[byte]!=0xA5||guarded[256+bytes+byte]!=0xA5)return 11;
        std::printf("{\"m\":%u,\"vector_stores\":%u,\"zero_padded_gemm_values\":%u,"
                    "\"prefix_coverage_once\":true,\"guards_intact\":true}\n",rows,stores,padded_values);
    }
    std::printf("{\"cpu_scalar_half2_pairs_equal\":%llu,\"cuda_initialized\":false,\"gpu_launches\":0,\"passed\":true}\n",
                (unsigned long long)arithmetic_pairs);
    return 0;
}
