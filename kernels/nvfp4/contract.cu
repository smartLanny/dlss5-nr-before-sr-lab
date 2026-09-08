#include "contract_kernel.cuh"
#include "nr_nvfp4_build_id.h"

extern "C" __global__
__launch_bounds__(nr_nvfp4::DefaultContract::Kernel::MaxThreadsPerBlock,
                  nr_nvfp4::DefaultContract::Kernel::MinBlocksPerMultiprocessor)
void nr_nvfp4_contract_v1_128x128x128(
    CUTLASS_GRID_CONSTANT nr_nvfp4::DefaultContract::Params const params) {
    extern __shared__ char shared[];
    nr_nvfp4::DefaultContract::Kernel kernel;
    kernel(params, shared);
}

#define NR_DEFINE_CONTRACT(NAME, TYPE) \
extern "C" __global__ __launch_bounds__(TYPE::Kernel::MaxThreadsPerBlock, TYPE::Kernel::MinBlocksPerMultiprocessor) \
void NAME(CUTLASS_GRID_CONSTANT TYPE::Params const params) { \
    extern __shared__ char shared[]; TYPE::Kernel kernel; kernel(params, shared); \
}
NR_DEFINE_CONTRACT(nr_nvfp4_contract_v1_128x64x128, nr_nvfp4::NarrowContract)
NR_DEFINE_CONTRACT(nr_nvfp4_contract_v1_128x64x256, nr_nvfp4::DeepNarrowContract)
NR_DEFINE_CONTRACT(nr_nvfp4_contract_v1_128x64x128_pingpong, nr_nvfp4::PingpongContract)
