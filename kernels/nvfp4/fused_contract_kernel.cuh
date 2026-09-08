#pragma once
#include "contract_kernel.cuh"
#include "fused_store.cuh"

namespace nr_nvfp4 {
struct FusedContract {
    using Element = DefaultContract::Element;
    using Scale = DefaultContract::Scale;
    using Output = DefaultContract::Output;
    using Arch = DefaultContract::Arch;
    using Op = DefaultContract::Op;
    using Tile = DefaultContract::Tile;
    using Cluster = DefaultContract::Cluster;
    using Epilogue = typename cutlass::epilogue::collective::CollectiveBuilder<
        Arch, Op, Tile, Cluster, cutlass::epilogue::collective::EpilogueTileAuto,
        float, float, void, cutlass::layout::ColumnMajor, 8,
        void, cutlass::layout::ColumnMajor, 8,
        cutlass::epilogue::collective::EpilogueScheduleAuto, OriginalFp8Store>::CollectiveOp;
    using Mainloop = typename cutlass::gemm::collective::CollectiveBuilder<
        Arch, Op, Element, cutlass::layout::RowMajor, 32,
        Element, cutlass::layout::ColumnMajor, 32, float, Tile, Cluster,
        cutlass::gemm::collective::StageCountAutoCarveout<sizeof(typename Epilogue::SharedStorage)>,
        cutlass::gemm::collective::KernelScheduleAuto>::CollectiveOp;
    using Kernel = cutlass::gemm::kernel::GemmUniversal<Shape<int,int,int,int>, Mainloop, Epilogue>;
    using Params = Kernel::Params;
    using ScaleConfig = Mainloop::Sm1xxBlkScaledConfig;
};
}
