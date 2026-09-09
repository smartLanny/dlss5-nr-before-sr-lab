#include <cuda_runtime.h>
#include <cuda_fp16.h>
#include <cuda_fp8.h>
#include <cuda_fp4.h>
#include "cute/arch/mma_sm120.hpp"
#include "cutlass/functional.h"
#include "expansion_abi.h"
#include "fragment_layout.h"
#include "../nvfp4_io/original_layout.cuh"

namespace nr_expansion {
using Mma=cute::SM120::BLOCKSCALED::SM120_16x8x64_TN_VS<
    cute::float_e2m1_t,cute::float_e2m1_t,float,cute::float_ue4m3_t,16>;

__device__ __forceinline__ __half2 gate(__half2 x) {
    // Same explicit half rounding and polynomial as the recovered reference.
    const __half2 t=__hmax2(__hmin2(x,__float2half2_rn(4)),__float2half2_rn(-4));
    const __half2 q=__hfma2(__habs2(t),__half2half2(__ushort_as_half(0xab28)),__float2half2_rn(0.447265625f));
    return __hmul2_rn(x,__hfma2(t,q,__float2half2_rn(0.89453125f)));
}

template<uint32_t TokenRows>
__device__ __forceinline__ void pack_expand(NrExpansionParams p,uint8_t* shared) {
    const auto* original=reinterpret_cast<const uint8_t*>(p.original_fp8);
    const auto* weights=reinterpret_cast<const uint8_t*>(p.weights);
    const auto* weight_scales=reinterpret_cast<const uint8_t*>(p.weight_scales);
    auto* output=reinterpret_cast<uint8_t*>(p.packed_output);
    auto* output_scales=reinterpret_cast<uint8_t*>(p.output_scales);
    auto* packed=shared;
    auto* scales=shared+TokenRows*packed_shared_stride;
    const uint32_t row_base=blockIdx.y*TokenRows,feature_base=blockIdx.x*128;
    const uint32_t thread=threadIdx.x,lane=thread&31,warp=thread>>5;
    constexpr unsigned FullWarp=0xffffffffu;

    if(row_base>=p.rows) {
        // Integrated CUTLASS block-scale output has no positive floor. The
        // reference's unused padded destinations start at zero.
        for(uint32_t i=thread;i<TokenRows*8;i+=256) output_scales[nr_scale_byte(row_base+i/8,feature_base/16+i%8,4096)]=0;
        for(uint32_t i=thread;i<TokenRows*64;i+=256) output[(row_base+i/64)*2048+feature_base/2+i%64]=0;
        return;
    }

    // Each CTA packs only the tokens it consumes into private shared memory.
    // There is no global packed-input tensor, cross-CTA handoff or second launch.
    // A full row has 512 pair lanes, so every 8-lane scale group is intact even
    // when the final token tile contains invalid rows.
    for(uint32_t i=thread;i<TokenRows*512;i+=256) {
        const uint32_t local_row=i/512,pair=i%512,row=row_base+local_row;
        uint16_t raw=0;
        if(row<p.rows) raw=*reinterpret_cast<const uint16_t*>(original+nr_original_pair_byte(row,pair,1024));
        const float2 value=__half22float2(__nv_cvt_fp8x2_to_halfraw2(raw,__NV_E4M3));
        float maximum=fmaxf(fabsf(value.x),fabsf(value.y));
        maximum=fmaxf(maximum,__shfl_xor_sync(FullWarp,maximum,1));
        maximum=fmaxf(maximum,__shfl_xor_sync(FullWarp,maximum,2));
        maximum=fmaxf(maximum,__shfl_xor_sync(FullWarp,maximum,4));
        const uint8_t sf=__nv_cvt_float_to_fp8(fmaxf(maximum*(1.f/6.f),1.f/512.f),__NV_SATFINITE,__NV_E4M3);
        const float inverse=1.f/__half2float(__nv_cvt_fp8_to_halfraw(sf,__NV_E4M3));
        packed[local_row*packed_shared_stride+pair]=__nv_cvt_halfraw2_to_fp4x2(__floats2half2_rn(value.x*inverse,value.y*inverse),__NV_E2M1,cudaRoundNearest);
        if((i&7)==0) scales[local_row*scale_shared_stride+pair/8]=sf;
    }
    __syncthreads();

    constexpr uint32_t TokenGroups=TokenRows/8;
    float accum[TokenGroups][4]{};
    const uint32_t feature=feature_base+warp*16;
    const uint32_t a_row=feature+(lane>>2),a_sf_row=feature+a_scale_row(lane);
    // K ordering is increasing 64-element NVFP4 MMA groups, with FP32
    // accumulation. GPU/reference equality remains a separate acceptance gate.
    #pragma unroll 1
    for(uint32_t k=0;k<1024;k+=64) {
        const uint32_t pair=(k+8*(lane&3))/2;
        const uint32_t a0=*reinterpret_cast<const uint32_t*>(weights+a_row*512+pair);
        const uint32_t a1=*reinterpret_cast<const uint32_t*>(weights+(a_row+8)*512+pair);
        const uint32_t a2=*reinterpret_cast<const uint32_t*>(weights+a_row*512+pair+16);
        const uint32_t a3=*reinterpret_cast<const uint32_t*>(weights+(a_row+8)*512+pair+16);
        const uint32_t sfa=*reinterpret_cast<const uint32_t*>(weight_scales+nr_scale_byte(a_sf_row,k/16,1024));
        #pragma unroll
        for(uint32_t token=0;token<TokenGroups;++token) {
            const uint32_t row=token*8+b_scale_row(lane);
            const uint32_t b0=*reinterpret_cast<const uint32_t*>(packed+row*packed_shared_stride+pair);
            const uint32_t b1=*reinterpret_cast<const uint32_t*>(packed+row*packed_shared_stride+pair+16);
            const uint32_t sfb=*reinterpret_cast<const uint32_t*>(scales+row*scale_shared_stride+k/16);
            auto& d=accum[token];
            Mma::fma(d[0],d[1],d[2],d[3],a0,a1,a2,a3,b0,b1,d[0],d[1],d[2],d[3],sfa,sfb);
        }
    }

    #pragma unroll
    for(uint32_t token=0;token<TokenGroups;++token) {
        auto& d=accum[token];
        const __half2 lower=gate(__floats2half2_rn(d[0],d[1]));
        const __half2 upper=gate(__floats2half2_rn(d[2],d[3]));
        const float2 x=__half22float2(lower),y=__half22float2(upper);
        cutlass::maximum_absolute_value_reduction<float,true> maximum;
        float max0=maximum(maximum(0.f,x.x),y.x),max1=maximum(maximum(0.f,x.y),y.y);
        #pragma unroll
        for(uint32_t mask=4;mask<=16;mask*=2) {
            max0=maximum(max0,__shfl_xor_sync(FullWarp,max0,mask));
            max1=maximum(max1,__shfl_xor_sync(FullWarp,max1,mask));
        }
        // This is the integrated fused_expand epilogue, not the standalone
        // gate_to_nvfp4 IO helper: no 1/512 SF floor, approximate FTZ reciprocal,
        // finite cap on its multiplier, then direct FP32 -> FP4 conversion.
        // Pinned upstream SASS: MUFU.RCP, FMNMX.NAN FLT_MAX, F2FP.E2M1.F32.
        const float norm_scaled_down=p.inverse_tensor_scale*(1.f/6.f);
        const uint8_t sf0=__nv_cvt_float_to_fp8(max0*norm_scaled_down,__NV_SATFINITE,__NV_E4M3);
        const uint8_t sf1=__nv_cvt_float_to_fp8(max1*norm_scaled_down,__NV_SATFINITE,__NV_E4M3);
        cutlass::reciprocal_approximate_ftz<float> reciprocal;
        cutlass::minimum_with_nan_propagation<float> minimum;
        const float inv0=minimum(p.inverse_tensor_scale*reciprocal(__half2float(__nv_cvt_fp8_to_halfraw(sf0,__NV_E4M3))),3.40282346638528859812e+38f);
        const float inv1=minimum(p.inverse_tensor_scale*reciprocal(__half2float(__nv_cvt_fp8_to_halfraw(sf1,__NV_E4M3))),3.40282346638528859812e+38f);
        const float next_x0=__shfl_xor_sync(FullWarp,x.x,4),next_x1=__shfl_xor_sync(FullWarp,x.y,4);
        const float next_y0=__shfl_xor_sync(FullWarp,y.x,4),next_y1=__shfl_xor_sync(FullWarp,y.y,4);
        const uint32_t row0=row_base+token*8+2*(lane&3),row1=row0+1;
        const uint32_t col=feature+(lane>>2);
        if(((lane>>2)&1)==0) {
            const uint8_t x0=__nv_cvt_float2_to_fp4x2(make_float2(x.x*inv0,next_x0*inv0),__NV_E2M1,cudaRoundNearest);
            const uint8_t x1=__nv_cvt_float2_to_fp4x2(make_float2(x.y*inv1,next_x1*inv1),__NV_E2M1,cudaRoundNearest);
            const uint8_t y0=__nv_cvt_float2_to_fp4x2(make_float2(y.x*inv0,next_y0*inv0),__NV_E2M1,cudaRoundNearest);
            const uint8_t y1=__nv_cvt_float2_to_fp4x2(make_float2(y.y*inv1,next_y1*inv1),__NV_E2M1,cudaRoundNearest);
            output[row0*2048+col/2]=row0<p.rows ? x0 : 0;
            output[row1*2048+col/2]=row1<p.rows ? x1 : 0;
            output[row0*2048+(col+8)/2]=row0<p.rows ? y0 : 0;
            output[row1*2048+(col+8)/2]=row1<p.rows ? y1 : 0;
        }
        if((lane>>2)==0) {
            output_scales[nr_scale_byte(row0,feature/16,4096)]=row0<p.rows ? sf0 : 0;
            output_scales[nr_scale_byte(row1,feature/16,4096)]=row1<p.rows ? sf1 : 0;
        }
    }
    // The caller records a GPU-wide UAV dependency before contraction. There
    // are no producer counters or cross-CTA consumers inside this kernel.
}
} // namespace nr_expansion

#define NR_PACK_EXPANSION_KERNEL(NAME,TOKENS) \
extern "C" __global__ __launch_bounds__(256) void NAME(NrExpansionParams params) { \
    extern __shared__ __align__(128) uint8_t storage[]; \
    nr_expansion::pack_expand<TOKENS>(params,storage); \
}
NR_PACK_EXPANSION_KERNEL(nr_pack_expand_v1_m64n128,64)
NR_PACK_EXPANSION_KERNEL(nr_pack_expand_v1_m32n128,32)
NR_PACK_EXPANSION_KERNEL(nr_pack_expand_v1_m128n128,128)
