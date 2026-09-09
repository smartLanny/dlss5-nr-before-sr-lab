#pragma once
#include <stdint.h>
#include <stddef.h>

#if defined(_WIN32)
#define NR_EXPANSION_CALL __cdecl
#if defined(NR_EXPANSION_BUILD_DLL)
#define NR_EXPANSION_EXPORT __declspec(dllexport)
#else
#define NR_EXPANSION_EXPORT
#endif
#else
#define NR_EXPANSION_CALL
#define NR_EXPANSION_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define NR_EXPANSION_ABI_VERSION 1u
#define NR_EXPANSION_KERNEL_ID_BYTES 64u
enum NrExpansionStatus {
    NR_EXPANSION_OK = 0,
    NR_EXPANSION_BAD_ABI = 1,
    NR_EXPANSION_UNSUPPORTED = 2,
    NR_EXPANSION_BUFFER_TOO_SMALL = 3,
    NR_EXPANSION_BAD_CAPACITY = 4
};
enum NrExpansionVariant {
    NR_EXPANSION_TOKENS_64 = 0,
    NR_EXPANSION_TOKENS_32 = 1,
    NR_EXPANSION_TOKENS_128 = 2
};

/* The caller establishes actual D3D12/CUDA adapter identity and supplies its
 * observed capability fields. This helper does not load CUDA or query a GPU.
 * Size queries do not require pointers/capacities. Descriptor construction
 * validates all five buffer extents. Current model n=4096, k=1024, scale=1.
 * The original input is the approved NR module's E4M3 permutation. The weights
 * and weight scales are the exact locked NVFP4 expansion asset slices.
 */
typedef struct NrExpansionInput {
    uint32_t struct_bytes;
    uint32_t abi_version;
    uint64_t original_fp8;
    uint64_t weights;
    uint64_t weight_scales;
    uint64_t packed_output;
    uint64_t output_scales;
    uint64_t original_capacity;
    uint64_t weights_capacity;
    uint64_t weight_scales_capacity;
    uint64_t packed_output_capacity;
    uint64_t output_scales_capacity;
    uint32_t rows;
    uint32_t columns;
    uint32_t reduction;
    uint32_t sm_count;
    uint32_t compute_major;
    uint32_t compute_minor;
    uint32_t variant;
    uint32_t max_shared_bytes;
    float inverse_tensor_scale;
    uint32_t flags;
} NrExpansionInput;

/* No opaque tensor maps: this is the entire kernel argument. A cached blob may
 * change only original_fp8 through the explicit linear region reported by Info.
 * All allocation and policy checks remain outside the recording path.
 */
typedef struct NrExpansionParams {
    uint64_t original_fp8;
    uint64_t weights;
    uint64_t weight_scales;
    uint64_t packed_output;
    uint64_t output_scales;
    uint32_t rows;
    float inverse_tensor_scale;
    uint32_t flags;
    uint32_t reserved;
} NrExpansionParams;

typedef struct NrExpansionInfo {
    uint32_t struct_bytes;
    uint32_t abi_version;
    uint64_t params_bytes;
    uint64_t params_alignment;
    uint64_t original_bytes;
    uint64_t weights_bytes;
    uint64_t weight_scales_bytes;
    uint64_t packed_output_bytes;
    uint64_t output_scales_bytes;
    uint64_t linear_input_offset;
    uint64_t linear_input_bytes;
    uint32_t grid[3];
    uint32_t block[3];
    uint32_t shared_bytes;
    uint32_t token_tile;
    uint32_t sm_count;
    uint32_t reserved;
    char kernel_id[NR_EXPANSION_KERNEL_ID_BYTES];
} NrExpansionInfo;

NR_EXPANSION_EXPORT int32_t NR_EXPANSION_CALL nr_nvfp4_build_pack_expansion(
    const NrExpansionInput* input, void* params, uint64_t capacity,
    NrExpansionInfo* info, char* error, uint32_t error_capacity);
NR_EXPANSION_EXPORT const char* NR_EXPANSION_CALL nr_nvfp4_expansion_build_id_v1(void);

typedef int32_t (NR_EXPANSION_CALL *NrBuildPackExpansionFn)(
    const NrExpansionInput*, void*, uint64_t, NrExpansionInfo*, char*, uint32_t);

#ifdef __cplusplus
}
static_assert(sizeof(NrExpansionInput)==128,"Pack/expansion input ABI");
static_assert(sizeof(NrExpansionParams)==56,"Pack/expansion Params ABI");
static_assert(sizeof(NrExpansionInfo)==184,"Pack/expansion Info ABI");
#endif
