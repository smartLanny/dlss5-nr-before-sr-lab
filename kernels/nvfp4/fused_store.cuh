#pragma once
#include <cuda_fp16.h>
#include <cuda_fp8.h>
#include "../nvfp4_io/original_layout.cuh"
#include "cutlass/epilogue/fusion/sm90_visitor_tma_warpspecialized.hpp"

namespace nr_nvfp4 {
// A direct CUTLASS consumer-store callback. ElementD=void disables the regular
// TMA store, so the rounded FP16 intermediate never goes to global memory.
// The original layout and half multiply/add order were separately verified.
struct OriginalFp8Store {
    using ElementAux = cutlass::half_t;
    // The actual SM120 consumer map is enumerated by fused_fragment_layout.cu:
    // a fragment holds two rows of one column, then the same rows of col+8.
    // The adjacent column is held by lane XOR 4 at the same fragment index.
    static constexpr int kFragmentSize = 4;
    static constexpr int kNeighborLaneXor = 4;
    struct SharedStorage {};
    struct Arguments {
        const uint8_t* skip{};
        const __half* cosine{};
        uint8_t* output{};
        uint32_t* done{};
        uint32_t* counter{};
        uint32_t* expand_done{};
        uint32_t rows{};
        uint32_t reserved{};
    };
    using Params = Arguments;
    template<class Shape>
    static Params to_underlying_arguments(const Shape&, const Arguments& args, void*) { return args; }
    template<class Shape>
    static bool can_implement(const Shape&, const Arguments& args) {
        return args.skip && args.cosine && args.output && args.rows && !args.reserved;
    }
    template<class Shape>
    static size_t get_workspace_size(const Shape&, const Arguments&) { return 0; }
    template<class Shape>
    static cutlass::Status initialize_workspace(const Shape&, const Arguments&, void*, cudaStream_t,
                                                cutlass::CudaHostAdapter* = nullptr) { return cutlass::Status::kSuccess; }
    CUTLASS_HOST_DEVICE OriginalFp8Store() = default;
    CUTLASS_HOST_DEVICE OriginalFp8Store(const Params& params, const SharedStorage&) : params_(&params) {}
    const Params* params_{};
    CUTLASS_DEVICE bool is_producer_load_needed() const { return false; }
    CUTLASS_DEVICE bool is_C_load_needed() const { return false; }
    template<class... Args>
    CUTLASS_DEVICE auto get_producer_load_callbacks(const cutlass::epilogue::fusion::ProducerLoadArgs<Args...>&) {
        return cutlass::epilogue::fusion::EmptyProducerLoadCallbacks{};
    }
    template<class Coordinates>
    struct Consumer : cutlass::epilogue::fusion::EmptyConsumerStoreCallbacks {
        Coordinates coordinates;
        const Params* params;
        CUTLASS_DEVICE Consumer(Coordinates c, const Params* p) : coordinates(c), params(p) {}
        template<class Accumulator, int FragmentSize>
        CUTLASS_DEVICE auto visit(const cutlass::Array<Accumulator,FragmentSize>& acc,
                                 int epi_v, int epi_m, int epi_n) {
            using namespace cute;
            static_assert(FragmentSize==kFragmentSize,
                          "Revalidate the SM120 consumer pairing after changing the epilogue fragment");
            const auto coords = coalesce(coordinates(_,_,_,epi_m,epi_n));
            cutlass::Array<cutlass::half_t,FragmentSize> rounded;
            const uint32_t padded_rows=((params->rows+15)/16)*16;
            const uint32_t output_bytes=params->rows*1024;
            // Every consumer lane participates, including lanes with no live
            // packed-prefix bytes. Bounds only predicate loads and final stores.
            const unsigned lane_mask=__activemask();
            CUTLASS_PRAGMA_UNROLL
            for (int i=0;i<FragmentSize;i+=2) {
                const auto coord0=coords(epi_v*FragmentSize+i);
                const auto coord1=coords(epi_v*FragmentSize+i+1);
                const uint32_t col=get<0>(coord0),row0=get<1>(coord0),row1=get<1>(coord1);
                const __half2 gemm=__floats2half2_rn(
                    row0<params->rows ? float(acc[i]) : 0.0f,
                    row1<params->rows ? float(acc[i+1]) : 0.0f);
                const __half2_raw gemm_raw=gemm;
                rounded[i]=cutlass::half_t::bitcast(gemm_raw.x);
                rounded[i+1]=cutlass::half_t::bitcast(gemm_raw.y);
                const uint32_t byte0=nr_original_pair_byte(row0,col/2,1024)+(col&1);
                const uint32_t byte1=nr_original_pair_byte(row1,col/2,1024)+(col&1);
                // The old epilogue consumes a packed prefix for non-16-row tails.
                // It may reference zero-padded GEMM rows, never bytes beyond it.
                const bool live0=col<1024 && row0<padded_rows && byte0<output_bytes;
                const bool live1=col<1024 && row1<padded_rows && byte1<output_bytes;
                const uint16_t skip_bits=uint16_t(live0 ? params->skip[byte0] : 0)
                    | (uint16_t(live1 ? params->skip[byte1] : 0)<<8);
                const __half2 skip=__nv_cvt_fp8x2_to_halfraw2(skip_bits,__NV_E4M3);
                const __half cosine=col<1024 ? params->cosine[col] : __float2half_rn(0.0f);
                const __half2 product=__hmul2_rn(skip,__half2half2(cosine));
                const __half2 value=__hadd2(gemm,product);
                const uint32_t packed=__nv_cvt_halfraw2_to_fp8x2(value,__NV_SATFINITE,__NV_E4M3);
                // Exchange the two completed FP8 rows, then transpose the
                // 2x2 register tile into aligned neighboring-column stores.
                const uint32_t neighbor=__shfl_xor_sync(lane_mask,packed,kNeighborLaneXor);
                if ((col&1)==0) {
                    if(live0 && byte0+1<output_bytes)
                        *reinterpret_cast<uint16_t*>(params->output+byte0)=uint16_t((packed&255)|((neighbor&255)<<8));
                    if(live1 && byte1+1<output_bytes)
                        *reinterpret_cast<uint16_t*>(params->output+byte1)=uint16_t((packed>>8)|(neighbor&0xff00));
                }
            }
            return rounded;
        }
    };
    template<bool ReferenceSrc,class... Args>
    CUTLASS_DEVICE auto get_consumer_store_callbacks(const cutlass::epilogue::fusion::ConsumerStoreArgs<Args...>& args) {
        using namespace cute;
        auto [M,N,K,L]=args.problem_shape_mnkl;
        const auto identity=make_identity_tensor(make_shape(M,N,L));
        auto coordinates=cutlass::epilogue::fusion::detail::sm90_partition_for_epilogue<ReferenceSrc>(
            identity,args.tile_shape_mnk,args.tile_coord_mnkl,args.epi_tile,args.tiled_copy,args.thread_idx);
        return Consumer<decltype(coordinates)>(coordinates,params_);
    }
};
}
