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
            const auto coords = coalesce(coordinates(_,_,_,epi_m,epi_n));
            cutlass::Array<cutlass::half_t,FragmentSize> rounded;
            CUTLASS_PRAGMA_UNROLL
            for (int i=0;i<FragmentSize;++i) {
                const auto coord=coords(epi_v*FragmentSize+i);
                const uint32_t col=get<0>(coord), row=get<1>(coord);
                const __half gemm=row<params->rows ? __float2half_rn(float(acc[i])) : __float2half_rn(0.0f);
                rounded[i]=cutlass::half_t(__half2float(gemm));
                const uint32_t byte=nr_original_pair_byte(row,col/2,1024)+(col&1);
                // The old epilogue consumes a packed prefix for non-16-row tails.
                // It may reference zero-padded GEMM rows, never bytes beyond it.
                if (col<1024 && row<((params->rows+15)/16)*16 && byte<params->rows*1024) {
                    const __half skip=__nv_cvt_fp8_to_halfraw(params->skip[byte],__NV_E4M3);
                    const __half product=__hmul_rn(skip,params->cosine[col]);
                    const __half value=__hadd(gemm,product);
                    params->output[byte]=__nv_cvt_halfraw_to_fp8(value,__NV_SATFINITE,__NV_E4M3);
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
