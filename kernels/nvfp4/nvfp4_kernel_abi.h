#pragma once
/* Versioned C ABI shared by the MinGW Core and the separately built MSVC helper.
 * No C++ types, ownership transfer, or renderer work cross this boundary.
 * All calls prepare CPU descriptors only. The caller owns every GPU allocation.
 */
#include <stdint.h>
#if defined(_WIN32)
#define NR_NVFP4_CALL __cdecl
#if defined(NR_NVFP4_BUILD_DLL)
#define NR_NVFP4_EXPORT __declspec(dllexport)
#else
#define NR_NVFP4_EXPORT
#endif
#else
#define NR_NVFP4_CALL
#define NR_NVFP4_EXPORT
#endif
#ifdef __cplusplus
extern "C" {
#endif

#define NR_NVFP4_ABI_VERSION 1u
#define NR_NVFP4_KERNEL_ID_CAPACITY 96u

enum NrNvfp4Status {
    NR_NVFP4_OK = 0,
    NR_NVFP4_BAD_ABI = 1,
    NR_NVFP4_UNSUPPORTED = 2,
    NR_NVFP4_BUFFER_TOO_SMALL = 3,
    NR_NVFP4_CUDA_ERROR = 4,
    NR_NVFP4_INTERNAL_ERROR = 5
};
enum NrNvfp4Variant {
    NR_NVFP4_COOPERATIVE_128_128_128 = 0,
    NR_NVFP4_COOPERATIVE_128_64_128 = 1,
    NR_NVFP4_COOPERATIVE_128_64_256 = 2,
    NR_NVFP4_PINGPONG_128_64_128 = 3
};

/* Caller supplies this in Prepare, never in the intercept/record hot path.
 * A is packed weights [n,k]; B is packed activations [m,k]. Both use the
 * CUTLASS SM1xx UE4M3 block-16 scale layout. D is FP16 [m,n] row-major.
 * m need not be divisible by the CTA tile. k and n are fixed for this FFN.
 * Variant 0 is the conservative 128x128x128 contraction; additional variants
 * are explicit and never change the opaque Params schema of a cached entry.
 * adapter_luid is the byte-for-byte Windows DXGI LUID, not a device ordinal.
 */
typedef struct NrNvfp4ContractInput {
    uint32_t struct_bytes;
    uint32_t abi_version;
    uint64_t adapter_luid;
    uint32_t m;
    uint32_t n;
    uint32_t k;
    uint32_t sm_count;
    uint32_t variant;
    uint32_t flags; /* reserved, must be zero */
    uint64_t weights;
    uint64_t weight_scales;
    uint64_t activations;
    uint64_t activation_scales;
    uint64_t output;
    uint64_t workspace;
    uint64_t workspace_capacity;
} NrNvfp4ContractInput;

typedef struct NrNvfp4ContractInfo {
    uint32_t struct_bytes;
    uint32_t abi_version;
    uint64_t params_bytes;
    uint64_t params_alignment;
    uint64_t workspace_bytes;
    uint64_t workspace_zero_bytes;
    uint64_t output_bytes;
    uint64_t weight_scale_bytes;
    uint64_t activation_scale_bytes;
    uint32_t grid[3];
    uint32_t block[3];
    uint32_t shared_bytes;
    uint32_t cuda_device_ordinal;
    uint32_t sm_count;
    uint32_t reserved;
    char kernel_id[NR_NVFP4_KERNEL_ID_CAPACITY];
} NrNvfp4ContractInfo;

/* A size query uses params=NULL/capacity=0 and does not encode any GPU address.
 * A successful non-query emits an aligned opaque Params blob for exactly the
 * matching kernel_id. Never patch fields in the blob or reuse an older ABI.
 * Error text is caller-owned, optional, always bounded and NUL terminated.
 */
NR_NVFP4_EXPORT int32_t NR_NVFP4_CALL nr_nvfp4_build_contract(
    const NrNvfp4ContractInput* input, void* params, uint64_t capacity,
    NrNvfp4ContractInfo* info, char* error, uint32_t error_capacity);

typedef int32_t (NR_NVFP4_CALL *NrNvfp4BuildContractFn)(
    const NrNvfp4ContractInput*, void*, uint64_t, NrNvfp4ContractInfo*, char*, uint32_t);

/* Immutable identifier of all source/toolchain inputs used for this helper and
 * its cubin. The caller compares it with the trusted artifact-pair receipt.
 */
NR_NVFP4_EXPORT const char* NR_NVFP4_CALL nr_nvfp4_build_id_v1(void);
typedef const char* (NR_NVFP4_CALL *NrNvfp4BuildIdFn)(void);

#ifdef __cplusplus
}
static_assert(sizeof(NrNvfp4ContractInput) == 96, "NVFP4 input ABI");
static_assert(sizeof(NrNvfp4ContractInfo) == 200, "NVFP4 info ABI");
#endif
