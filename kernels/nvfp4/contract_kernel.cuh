#pragma once
#include "cutlass/cutlass.h"
#include "cute/tensor.hpp"
#include "cutlass/gemm/collective/collective_builder.hpp"
#include "cutlass/epilogue/collective/collective_builder.hpp"
#include "cutlass/gemm/kernel/gemm_universal.hpp"
#include "cutlass/util/packed_stride.hpp"

namespace nr_nvfp4 {
using namespace cute;
template<int TileN = 128, int TileK = 128, bool Pingpong = false>
struct Contract {
    using Element = cutlass::nv_float4_t<cutlass::float_e2m1_t>;
    using Scale = cutlass::float_ue4m3_t;
    using Output = cutlass::half_t;
    using Arch = cutlass::arch::Sm120;
    using Op = cutlass::arch::OpClassBlockScaledTensorOp;
    using Tile = Shape<_128, Int<TileN>, Int<TileK>>;
    using Cluster = Shape<_1, _1, _1>;
    using Schedule = std::conditional_t<Pingpong, cutlass::gemm::KernelTmaWarpSpecializedPingpong,
                                                cutlass::gemm::collective::KernelScheduleAuto>;
    using Epilogue = typename cutlass::epilogue::collective::CollectiveBuilder<
        Arch, Op, Tile, Cluster, cutlass::epilogue::collective::EpilogueTileAuto,
        float, float,
        Output, cutlass::layout::ColumnMajor, 8,
        Output, cutlass::layout::ColumnMajor, 8,
        cutlass::epilogue::collective::EpilogueScheduleAuto>::CollectiveOp;
    using Mainloop = typename cutlass::gemm::collective::CollectiveBuilder<
        Arch, Op,
        Element, cutlass::layout::RowMajor, 32,
        Element, cutlass::layout::ColumnMajor, 32,
        float, Tile, Cluster,
        cutlass::gemm::collective::StageCountAutoCarveout<sizeof(typename Epilogue::SharedStorage)>,
        Schedule>::CollectiveOp;
    using Kernel = cutlass::gemm::kernel::GemmUniversal<Shape<int,int,int,int>, Mainloop, Epilogue>;
    using Params = typename Kernel::Params;
    using Arguments = typename Kernel::Arguments;
    using ScaleConfig = typename Mainloop::Sm1xxBlkScaledConfig;
};
using DefaultContract = Contract<128,128>;
using NarrowContract = Contract<64,128>;
using DeepNarrowContract = Contract<64,256>;
using PingpongContract = Contract<64,128,true>;
}
