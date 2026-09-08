#include "fused_contract_kernel.cuh"
#include "nr_nvfp4_build_id.h"
extern "C" __global__
__launch_bounds__(nr_nvfp4::FusedContract::Kernel::MaxThreadsPerBlock,
                  nr_nvfp4::FusedContract::Kernel::MinBlocksPerMultiprocessor)
void nr_nvfp4_contract_publish_v1(CUTLASS_GRID_CONSTANT nr_nvfp4::FusedContract::Params const params) {
    extern __shared__ char shared[];
    nr_nvfp4::FusedContract::Kernel kernel;
    kernel(params, shared);
    // As in the old epilogue these are ordinary stores, not a grid-wide fence.
    // A mandatory subsequent global UAV dependency gates all consumers.
    if (blockIdx.x==0 && blockIdx.y==0 && blockIdx.z==0 && threadIdx.x==0) {
        const auto& p=params.epilogue.thread;
        const uint32_t tiles=(p.rows+127)/128;
        for (uint32_t i=0;i<tiles*8;++i) {
            if(p.done)p.done[i]=0;
            if(p.counter)p.counter[i]=3;
        }
        if(p.expand_done)for(uint32_t i=0;i<tiles*32;++i)p.expand_done[i]=0;
    }
}
