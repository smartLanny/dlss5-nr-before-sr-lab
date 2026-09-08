#define NR_NVFP4_BUILD_DLL
#include "nvfp4_fused_abi.h"
#include "fused_contract_kernel.cuh"
#include <cuda.h>
#include <cstdio>
#include <cstring>

extern "C" NR_NVFP4_EXPORT int32_t NR_NVFP4_CALL nr_nvfp4_describe_fused_layout_v1(NrNvfp4FusedInfo* fused_info) {
    if(!fused_info || fused_info->launch.struct_bytes!=sizeof(NrNvfp4ContractInfo) ||
       fused_info->launch.abi_version!=NR_NVFP4_ABI_VERSION)return NR_NVFP4_BAD_ABI;
    using K=nr_nvfp4::FusedContract::Kernel;
    K::Params probe{};
    auto& info=fused_info->launch;
    info.params_bytes=sizeof(K::Params);
    info.params_alignment=alignof(K::Params);
    info.shared_bytes=K::SharedStorageSize;
    std::snprintf(info.kernel_id,sizeof(info.kernel_id),"nr_nvfp4_contract_publish_v1");
    fused_info->runtime_bindings_offset=uint64_t(reinterpret_cast<const char*>(&probe.epilogue.thread.skip)-reinterpret_cast<const char*>(&probe));
    fused_info->runtime_bindings_bytes=sizeof(NrNvfp4RuntimeBindings);
    const auto endpoint=uint64_t(reinterpret_cast<const char*>(&probe.epilogue.thread.rows)-reinterpret_cast<const char*>(&probe));
    if(endpoint!=fused_info->runtime_bindings_offset+fused_info->runtime_bindings_bytes || endpoint>sizeof(probe))return NR_NVFP4_INTERNAL_ERROR;
    return NR_NVFP4_OK;
}

extern "C" NR_NVFP4_EXPORT int32_t NR_NVFP4_CALL nr_nvfp4_build_fused_contract(
    const NrNvfp4FusedInput* input, void* output, uint64_t capacity,
    NrNvfp4FusedInfo* fused_info, char* error, uint32_t error_capacity) {
    const auto fail=[&](int32_t status,const char* message) {
        if(error && error_capacity)std::snprintf(error,error_capacity,"%s",message);
        return status;
    };
    if(!input || !fused_info)return fail(NR_NVFP4_BAD_ABI,"Missing fused input/info");
    auto* info=&fused_info->launch;
    const auto& in=input->contract;
    if(in.variant!=0 || in.output)return fail(NR_NVFP4_UNSUPPORTED,"Fused variant uses no FP16 output");
    // Reuse the validated LUID/device/capacity query. It never records GPU work.
    const int32_t base=nr_nvfp4_build_contract(&in,nullptr,0,info,error,error_capacity);
    if(base!=NR_NVFP4_OK)return base;
    using C=nr_nvfp4::FusedContract;
    using K=C::Kernel;
    using namespace cute;
    if(nr_nvfp4_describe_fused_layout_v1(fused_info)!=NR_NVFP4_OK)
        return fail(NR_NVFP4_INTERNAL_ERROR,"Unexpected fused linear-bindings layout");
    info->output_bytes=uint64_t(in.m)*1024;
    int limit=0;
    if(cuDeviceGetAttribute(&limit,CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK_OPTIN,int(info->cuda_device_ordinal)) ||
       info->shared_bytes>uint32_t(limit))return fail(NR_NVFP4_UNSUPPORTED,"Fused shared memory exceeds device limit");
    if(!output && !capacity)return NR_NVFP4_OK;
    if(!output || capacity<info->params_bytes || uintptr_t(output)%info->params_alignment)
        return fail(NR_NVFP4_BUFFER_TOO_SMALL,"Fused Params buffer capacity/alignment mismatch");
    for(auto p:{in.weights,in.weight_scales,in.activations,in.activation_scales,input->skip,input->cosine,input->original_output})
        if(!p || p%16)return fail(NR_NVFP4_UNSUPPORTED,"Fused GPU buffers require 16-byte alignment");
    for(auto p:{input->done,input->counter,input->expand_done})
        if(p%4)return fail(NR_NVFP4_UNSUPPORTED,"Fused counter requires 4-byte alignment");
    try {
        const auto shape=make_shape(int(in.n),int(in.m),int(in.k),1);
        const auto sa=cutlass::make_cute_packed_stride(K::StrideA{},{int(in.n),int(in.k),1});
        const auto sb=cutlass::make_cute_packed_stride(K::StrideB{},{int(in.m),int(in.k),1});
        const auto sd=cutlass::make_cute_packed_stride(K::StrideD{},{int(in.n),int(in.m),1});
        const auto sfa=C::ScaleConfig::tile_atom_to_shape_SFA(shape);
        const auto sfb=C::ScaleConfig::tile_atom_to_shape_SFB(shape);
        K::Arguments args{
            cutlass::gemm::GemmUniversalMode::kGemm,shape,
            {reinterpret_cast<const cutlass::float_e2m1_t*>(in.weights),sa,
             reinterpret_cast<const cutlass::float_e2m1_t*>(in.activations),sb,
             reinterpret_cast<const C::Scale*>(in.weight_scales),sfa,
             reinterpret_cast<const C::Scale*>(in.activation_scales),sfb},
            {{reinterpret_cast<const uint8_t*>(input->skip),reinterpret_cast<const __half*>(input->cosine),
              reinterpret_cast<uint8_t*>(input->original_output),reinterpret_cast<uint32_t*>(input->done),
              reinterpret_cast<uint32_t*>(input->counter),reinterpret_cast<uint32_t*>(input->expand_done),in.m,0},
              nullptr,sd,nullptr,sd},
            {int(info->cuda_device_ordinal),int(in.sm_count)}
        };
        info->workspace_bytes=K::get_workspace_size(args);
        if(info->workspace_bytes || !K::can_implement(args))return fail(NR_NVFP4_UNSUPPORTED,"Fused operands or workspace rejected");
        CUcontext context{};
        const CUdevice device=int(info->cuda_device_ordinal);
        if(cuDevicePrimaryCtxRetain(&context,device))return fail(NR_NVFP4_CUDA_ERROR,"Cannot retain CUDA context");
        struct Release { CUdevice device; ~Release(){cuDevicePrimaryCtxRelease(device);} } release{device};
        if(cuCtxPushCurrent(context))return fail(NR_NVFP4_CUDA_ERROR,"Cannot enter CUDA context");
        struct Pop {~Pop(){CUcontext previous{};cuCtxPopCurrent(&previous);}} pop;
        const auto params=K::to_underlying_arguments(args,nullptr);
        const auto grid=K::get_grid_shape(params), block=K::get_block_shape();
        info->grid[0]=grid.x;info->grid[1]=grid.y;info->grid[2]=grid.z;
        info->block[0]=block.x;info->block[1]=block.y;info->block[2]=block.z;
        std::memcpy(output,&params,sizeof(params));
        return NR_NVFP4_OK;
    } catch (...) {return fail(NR_NVFP4_INTERNAL_ERROR,"Fused descriptor construction failed");}
}
