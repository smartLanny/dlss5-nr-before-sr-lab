#pragma once
#include "nvfp4_kernel_abi.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Independent extension; the existing contraction-only C ABI is unchanged.
 * contract.output must be zero: this variant never writes an FP16 tensor.
 * original_output is M*1024 bytes in the reference FP8 permutation. The three
 * counter arrays require 8,8,32 entries per ceil(M/128) respectively.
 * Counter pointers may be NULL only when the surrounding protocol omits them.
 * A GPU-wide UAV dependency is required immediately after the kernel.
 */
typedef struct NrNvfp4FusedInput {
    NrNvfp4ContractInput contract;
    uint64_t skip;
    uint64_t cosine;
    uint64_t original_output;
    uint64_t done;
    uint64_t counter;
    uint64_t expand_done;
} NrNvfp4FusedInput;
typedef struct NrNvfp4RuntimeBindings {
    uint64_t skip;
    uint64_t cosine;
    uint64_t original_output;
    uint64_t done;
    uint64_t counter;
    uint64_t expand_done;
} NrNvfp4RuntimeBindings;
typedef struct NrNvfp4FusedInfo {
    NrNvfp4ContractInfo launch;
    uint64_t runtime_bindings_offset;
    uint64_t runtime_bindings_bytes;
} NrNvfp4FusedInfo;
/* After preparation the caller may overwrite only this explicitly described
 * 48-byte linear binding region of a private, preallocated Params blob. The
 * remaining bytes, including every TMA descriptor, remain completely opaque.
 * Descriptor build ID and artifact-pair hashes must have been checked first.
 */
NR_NVFP4_EXPORT int32_t NR_NVFP4_CALL nr_nvfp4_build_fused_contract(
    const NrNvfp4FusedInput* input, void* params, uint64_t capacity,
    NrNvfp4FusedInfo* info, char* error, uint32_t error_capacity);
typedef int32_t (NR_NVFP4_CALL *NrNvfp4BuildFusedFn)(
    const NrNvfp4FusedInput*, void*, uint64_t, NrNvfp4FusedInfo*, char*, uint32_t);
/* Pure CPU metadata: no CUDA initialization, device query, or descriptor
 * encoding. launch.struct_bytes/abi_version must be initialized by the caller.
 */
NR_NVFP4_EXPORT int32_t NR_NVFP4_CALL nr_nvfp4_describe_fused_layout_v1(NrNvfp4FusedInfo* info);
#ifdef __cplusplus
}
static_assert(sizeof(NrNvfp4FusedInput)==144,"NVFP4 fused input ABI");
static_assert(sizeof(NrNvfp4RuntimeBindings)==48,"NVFP4 runtime bindings ABI");
static_assert(sizeof(NrNvfp4FusedInfo)==216,"NVFP4 fused info ABI");
#endif
