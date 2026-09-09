#define NR_EXPANSION_BUILD_DLL
#include "expansion_abi.h"
#include "fragment_layout.h"
#include "nr_expansion_build_id.h"
#include <cstdio>
#include <cstring>

extern "C" NR_EXPANSION_EXPORT int32_t NR_EXPANSION_CALL nr_nvfp4_build_pack_expansion(
    const NrExpansionInput* in,void* params,uint64_t capacity,NrExpansionInfo* out,char* error,uint32_t error_capacity) {
    const auto fail=[&](int32_t code,const char* text) {
        if(error && error_capacity) std::snprintf(error,error_capacity,"%s",text);
        return code;
    };
    if(error && error_capacity) error[0]=0;
    if(!in || !out || in->struct_bytes!=sizeof(*in) || in->abi_version!=NR_EXPANSION_ABI_VERSION ||
       out->struct_bytes!=sizeof(*out) || out->abi_version!=NR_EXPANSION_ABI_VERSION)
        return fail(NR_EXPANSION_BAD_ABI,"Pack/expansion ABI mismatch");
    if(!in->rows || in->rows>16384 || in->columns!=4096 || in->reduction!=1024 ||
       !in->sm_count || in->compute_major!=12 || in->compute_minor!=0 || in->variant>2 ||
       in->flags || in->inverse_tensor_scale!=1.0f)
        return fail(NR_EXPANSION_UNSUPPORTED,"Unapproved shape, capability, variant or tensor scale");
    NrExpansionInfo info{}; info.struct_bytes=sizeof(info); info.abi_version=NR_EXPANSION_ABI_VERSION;
    info.params_bytes=sizeof(NrExpansionParams); info.params_alignment=alignof(NrExpansionParams);
    info.original_bytes=uint64_t((in->rows+15u)&~15u)*1024;
    info.weights_bytes=4096ull*1024/2; info.weight_scales_bytes=4096ull*1024/16;
    info.packed_output_bytes=uint64_t(nr_expansion::padded_rows(in->rows))*4096/2;
    info.output_scales_bytes=uint64_t(nr_expansion::padded_rows(in->rows))*4096/16;
    info.linear_input_offset=offsetof(NrExpansionParams,original_fp8); info.linear_input_bytes=sizeof(uint64_t);
    info.token_tile=nr_expansion::token_tile(in->variant); info.shared_bytes=nr_expansion::shared_bytes(info.token_tile);
    info.grid[0]=4096/128; info.grid[1]=nr_expansion::padded_rows(in->rows)/info.token_tile; info.grid[2]=1;
    info.block[0]=256; info.block[1]=1; info.block[2]=1; info.sm_count=in->sm_count;
    const char* names[]={"nr_pack_expand_v1_m64n128","nr_pack_expand_v1_m32n128","nr_pack_expand_v1_m128n128"};
    std::snprintf(info.kernel_id,sizeof(info.kernel_id),"%s",names[in->variant]);
    *out=info;
    if(in->max_shared_bytes<info.shared_bytes) return fail(NR_EXPANSION_UNSUPPORTED,"Actual device shared memory cannot hold this tile");
    if(!params && !capacity) return NR_EXPANSION_OK;
    if(!params || capacity<sizeof(NrExpansionParams) || uintptr_t(params)%alignof(NrExpansionParams))
        return fail(NR_EXPANSION_BUFFER_TOO_SMALL,"Params buffer size/alignment mismatch");
    const uint64_t addresses[]={in->original_fp8,in->weights,in->weight_scales,in->packed_output,in->output_scales};
    const uint64_t extents[]={info.original_bytes,info.weights_bytes,info.weight_scales_bytes,info.packed_output_bytes,info.output_scales_bytes};
    for(uint64_t address:addresses) if(!address || address%16) return fail(NR_EXPANSION_BAD_CAPACITY,"All GPU buffers require nonzero 16-byte alignment");
    for(uint32_t i=0;i<5;++i) if(addresses[i]>UINT64_MAX-extents[i]) return fail(NR_EXPANSION_BAD_CAPACITY,"GPU access extent overflows its address");
    for(uint32_t write=3;write<5;++write) for(uint32_t other=0;other<5;++other) if(write!=other &&
        addresses[write]<addresses[other]+extents[other] && addresses[other]<addresses[write]+extents[write])
        return fail(NR_EXPANSION_BAD_CAPACITY,"Expansion output may not overlap another operand");
    if(in->original_capacity<info.original_bytes || in->weights_capacity<info.weights_bytes ||
       in->weight_scales_capacity<info.weight_scales_bytes || in->packed_output_capacity<info.packed_output_bytes ||
       in->output_scales_capacity<info.output_scales_bytes)
        return fail(NR_EXPANSION_BAD_CAPACITY,"A GPU buffer is smaller than the declared access footprint");
    NrExpansionParams p{in->original_fp8,in->weights,in->weight_scales,in->packed_output,in->output_scales,
        in->rows,in->inverse_tensor_scale,0,0};
    std::memcpy(params,&p,sizeof(p));
    return NR_EXPANSION_OK;
}
extern "C" NR_EXPANSION_EXPORT const char* NR_EXPANSION_CALL nr_nvfp4_expansion_build_id_v1(void) {
    return NR_EXPANSION_BUILD_ID;
}
