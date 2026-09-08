#include <cuda_runtime.h>
#include <cuda_fp16.h>
#include <cuda_fp8.h>
#include <cuda_fp4.h>
#include "original_layout.cuh"

struct PackParams {const uint8_t*input;uint8_t*output;uint8_t*scale;uint32_t rows,cols;};
struct EpilogueParams {const __half*gemm;const uint8_t*skip;const __half*cosine;uint8_t*output;uint32_t rows,pad;uint32_t*done,*counter,*expand_done;};
static_assert(sizeof(PackParams)==32&&sizeof(EpilogueParams)==64,"reference IO ABI");

extern "C" __global__ void nr_pack_original_fp8(PackParams p) {
    const uint32_t i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i>=p.rows*p.cols/2)return;
    const uint32_t row=i/(p.cols/2),pair=i%(p.cols/2);
    const uint16_t packed=*reinterpret_cast<const uint16_t*>(p.input+nr_original_pair_byte(row,pair,p.cols));
    const __half2 h=__nv_cvt_fp8x2_to_halfraw2(packed,__NV_E4M3);
    const float2 v=__half22float2(h);
    float maximum=fmaxf(fabsf(v.x),fabsf(v.y));
    const unsigned mask=__activemask();
    maximum=fmaxf(maximum,__shfl_xor_sync(mask,maximum,1));
    maximum=fmaxf(maximum,__shfl_xor_sync(mask,maximum,2));
    maximum=fmaxf(maximum,__shfl_xor_sync(mask,maximum,4));
    const uint8_t sf=__nv_cvt_float_to_fp8(fmaxf(maximum*(1.f/6.f),1.f/512.f),__NV_SATFINITE,__NV_E4M3);
    const __half scale=__nv_cvt_fp8_to_halfraw(sf,__NV_E4M3);
    const float inv=1.f/__half2float(scale);
    const __half2 normalized=__floats2half2_rn(v.x*inv,v.y*inv);
    p.output[i]=__nv_cvt_halfraw2_to_fp4x2(normalized,__NV_E2M1,cudaRoundNearest);
    if((i&7)==0)p.scale[nr_scale_byte(row,pair>>3,p.cols)]=sf;
}

extern "C" __global__ void nr_contract_epilogue_pair(EpilogueParams p) {
    const uint32_t i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i<p.rows*512){
        uint32_t row,col;nr_original_pair_coordinate(i,row,col);
        const uint16_t packed=reinterpret_cast<const uint16_t*>(p.skip)[i];
        const __half2 skip=__nv_cvt_fp8x2_to_halfraw2(packed,__NV_E4M3);
        const __half2 cosine=*reinterpret_cast<const __half2*>(p.cosine+col);
        const __half2 gemm=*reinterpret_cast<const __half2*>(p.gemm+row*1024+col);
        // Reference explicitly rounds multiply to half before adding the GEMM.
        const __half2 result=__hadd2(gemm,__hmul2_rn(skip,cosine));
        reinterpret_cast<uint16_t*>(p.output)[i]=__nv_cvt_halfraw2_to_fp8x2(result,__NV_SATFINITE,__NV_E4M3);
    }
    const uint32_t tiles=(p.rows+127)/128;
    if(i<tiles*8){if(p.done)p.done[i]=0;if(p.counter)p.counter[i]=3;}
    if(i<tiles*32&&p.expand_done)p.expand_done[i]=0;
    // As in the reference, consumers must be separated by a GPU-wide UAV
    // dependency. These ordinary stores are not a grid-wide publication fence.
}

extern "C" __global__ void nr_copy_bytes(const uint8_t*source,uint8_t*destination,uint64_t bytes){
    const uint64_t i=uint64_t(blockIdx.x)*blockDim.x+threadIdx.x;
    if(i<bytes)destination[i]=source[i];
}

__device__ __forceinline__ __half2 nr_gate(__half2 x){
    const __half2 t=__hmax2(__hmin2(x,__float2half2_rn(4)),__float2half2_rn(-4));
    const __half2 q=__hfma2(__habs2(t),__half2half2(__ushort_as_half(0xab28)),__float2half2_rn(0.447265625f));
    return __hmul2_rn(x,__hfma2(t,q,__float2half2_rn(0.89453125f)));
}
extern "C" __global__ void nr_gate_to_nvfp4(const __half* input,uint8_t* output,uint8_t* scale,uint32_t rows,uint32_t cols,float inverse_tensor_scale){
    const uint32_t i=blockIdx.x*blockDim.x+threadIdx.x;
    const uint32_t row=i/(cols/4),col=(i%(cols/4))*4;
    if(row>=((rows+127)/128)*128)return;
    __half2 a=__float2half2_rn(0),b=a;
    if(row<rows){a=nr_gate(*reinterpret_cast<const __half2*>(input+row*cols+col));b=nr_gate(*reinterpret_cast<const __half2*>(input+row*cols+col+2));}
    const float2 va=__half22float2(a),vb=__half22float2(b);
    float maximum=fmaxf(fmaxf(fabsf(va.x),fabsf(va.y)),fmaxf(fabsf(vb.x),fabsf(vb.y)));
    const unsigned mask=__activemask();
    maximum=fmaxf(maximum,__shfl_xor_sync(mask,maximum,1));
    maximum=fmaxf(maximum,__shfl_xor_sync(mask,maximum,2));
    const float raw_scale=fminf(maximum*(1.f/6.f)*inverse_tensor_scale,448.f);
    const uint8_t sf=__nv_cvt_float_to_fp8(fmaxf(raw_scale,1.f/512.f),__NV_SATFINITE,__NV_E4M3);
    const __half decoded=__nv_cvt_fp8_to_halfraw(sf,__NV_E4M3);
    const float multiplier=inverse_tensor_scale/__half2float(decoded);
    if(row<rows){const uint8_t x=__nv_cvt_halfraw2_to_fp4x2(__floats2half2_rn(va.x*multiplier,va.y*multiplier),__NV_E2M1,cudaRoundNearest);
        const uint8_t y=__nv_cvt_halfraw2_to_fp4x2(__floats2half2_rn(vb.x*multiplier,vb.y*multiplier),__NV_E2M1,cudaRoundNearest);
        *reinterpret_cast<uint16_t*>(output+(row*cols+col)/2)=uint16_t(x)|(uint16_t(y)<<8);}
    if((i&3)==0)scale[nr_scale_byte(row,col/16,cols)]=sf;
}
