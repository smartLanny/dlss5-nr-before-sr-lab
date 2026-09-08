#define NR_NVFP4_BUILD_DLL
#include "nvfp4_kernel_abi.h"
#include "contract_kernel.cuh"
#include "nr_nvfp4_build_id.h"
#include <cuda.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <map>
#include <stdexcept>

namespace {
void check(CUresult result) {
    if (result != CUDA_SUCCESS) {
        const char* name = "CUDA_ERROR";
        cuGetErrorName(result, &name);
        throw std::runtime_error(name ? name : "CUDA_ERROR");
    }
}
struct Context {
    CUdevice device{};
    CUcontext context{};
    int sms{};
    int shared{};
};
std::mutex context_mutex;
std::map<uint64_t, Context> contexts;
Context get_context(uint64_t luid) {
    std::lock_guard<std::mutex> lock(context_mutex);
    const auto it = contexts.find(luid);
    if (it != contexts.end()) return it->second;
    check(cuInit(0));
    int count = 0;
    check(cuDeviceGetCount(&count));
    for (int ordinal = 0; ordinal < count; ++ordinal) {
        Context result;
        check(cuDeviceGet(&result.device, ordinal));
        uint64_t candidate = 0;
        unsigned node_mask = 0;
        check(cuDeviceGetLuid(reinterpret_cast<char*>(&candidate), &node_mask, result.device));
        if (candidate != luid) continue;
        int major = 0, minor = 0;
        check(cuDeviceGetAttribute(&major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, result.device));
        check(cuDeviceGetAttribute(&minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, result.device));
        if (major != 12 || minor != 0) throw std::runtime_error("Only SM120 is supported");
        check(cuDeviceGetAttribute(&result.sms, CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT, result.device));
        check(cuDeviceGetAttribute(&result.shared, CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK_OPTIN, result.device));
        check(cuDevicePrimaryCtxRetain(&result.context, result.device));
        contexts.emplace(luid, result);
        return result;
    }
    throw std::runtime_error("DXGI adapter LUID has no matching CUDA device");
}
struct PushContext {
    explicit PushContext(CUcontext c) { check(cuCtxPushCurrent(c)); }
    ~PushContext() { CUcontext previous{}; cuCtxPopCurrent(&previous); }
};
void write_error(char* out, uint32_t capacity, const char* message) {
    if (out && capacity) std::snprintf(out, capacity, "%s", message);
}
}

template<class C>
int32_t build_contract_variant(
    const char* kernel_name, const NrNvfp4ContractInput* in, void* output, uint64_t capacity,
    NrNvfp4ContractInfo* info, char* error, uint32_t error_capacity) {
    write_error(error, error_capacity, "");
    if (!in || !info || in->struct_bytes != sizeof(*in) || in->abi_version != NR_NVFP4_ABI_VERSION ||
        info->struct_bytes != sizeof(*info) || info->abi_version != NR_NVFP4_ABI_VERSION) {
        write_error(error, error_capacity, "Input/info ABI mismatch");
        return NR_NVFP4_BAD_ABI;
    }
    if (!in->m || in->m > 16384 || in->n != 1024 || in->k != 4096 ||
        !in->sm_count || in->variant > 3 || in->flags) {
        write_error(error, error_capacity, "Unsupported contraction shape or variant");
        return NR_NVFP4_UNSUPPORTED;
    }
    try {
        using K = typename C::Kernel;
        using namespace cute;
        const auto context = get_context(in->adapter_luid);
        if (in->sm_count != static_cast<uint32_t>(context.sms)) {
            write_error(error, error_capacity, "SM count does not match the DXGI adapter");
            return NR_NVFP4_UNSUPPORTED;
        }
        const auto shape = make_shape(int(in->n), int(in->m), int(in->k), 1);
        auto sa = cutlass::make_cute_packed_stride(typename K::StrideA{}, {int(in->n),int(in->k),1});
        auto sb = cutlass::make_cute_packed_stride(typename K::StrideB{}, {int(in->m),int(in->k),1});
        auto sd = cutlass::make_cute_packed_stride(typename K::StrideD{}, {int(in->n),int(in->m),1});
        auto sfa = C::ScaleConfig::tile_atom_to_shape_SFA(shape);
        auto sfb = C::ScaleConfig::tile_atom_to_shape_SFB(shape);
        typename K::Arguments args{
            cutlass::gemm::GemmUniversalMode::kGemm, shape,
            {reinterpret_cast<const cutlass::float_e2m1_t*>(in->weights), sa,
             reinterpret_cast<const cutlass::float_e2m1_t*>(in->activations), sb,
             reinterpret_cast<const typename C::Scale*>(in->weight_scales), sfa,
             reinterpret_cast<const typename C::Scale*>(in->activation_scales), sfb},
            {{1.0f,0.0f}, nullptr, sd, reinterpret_cast<typename C::Output*>(in->output), sd},
            {int(context.device), context.sms}
        };
        NrNvfp4ContractInfo result{};
        result.struct_bytes = sizeof(result);
        result.abi_version = NR_NVFP4_ABI_VERSION;
        result.params_bytes = sizeof(typename K::Params);
        result.params_alignment = alignof(typename K::Params);
        result.workspace_bytes = K::get_workspace_size(args);
        result.output_bytes = uint64_t(in->m) * in->n * 2;
        result.weight_scale_bytes = size(filter_zeros(sfa));
        result.activation_scale_bytes = size(filter_zeros(sfb));
        result.shared_bytes = K::SharedStorageSize;
        result.cuda_device_ordinal = context.device;
        result.sm_count = context.sms;
        std::snprintf(result.kernel_id, sizeof(result.kernel_id), "%s", kernel_name);
        *info = result;
        if (result.workspace_bytes != 0 || result.shared_bytes > uint32_t(context.shared)) {
            write_error(error, error_capacity, "Kernel requires unsupported workspace/shared memory");
            return NR_NVFP4_UNSUPPORTED;
        }
        if (!output && !capacity) return NR_NVFP4_OK;
        if (!output || capacity < sizeof(typename K::Params) ||
            reinterpret_cast<uintptr_t>(output) % alignof(typename K::Params)) {
            write_error(error, error_capacity, "Params buffer too small or misaligned");
            return NR_NVFP4_BUFFER_TOO_SMALL;
        }
        for (const auto pointer : {in->weights,in->weight_scales,in->activations,in->activation_scales,in->output}) {
            if (!pointer || (pointer % 16)) {
                write_error(error, error_capacity, "GPU buffers require nonzero 16-byte aligned addresses");
                return NR_NVFP4_UNSUPPORTED;
            }
        }
        if (!K::can_implement(args)) {
            write_error(error, error_capacity, "CUTLASS can_implement rejected the operands");
            return NR_NVFP4_UNSUPPORTED;
        }
        PushContext push(context.context);
        const auto params = K::to_underlying_arguments(args, nullptr);
        const auto grid = K::get_grid_shape(params);
        const auto block = K::get_block_shape();
        info->grid[0]=grid.x; info->grid[1]=grid.y; info->grid[2]=grid.z;
        info->block[0]=block.x; info->block[1]=block.y; info->block[2]=block.z;
        std::memcpy(output, &params, sizeof(params));
        return NR_NVFP4_OK;
    } catch (const std::exception& e) {
        write_error(error, error_capacity, e.what());
        return NR_NVFP4_CUDA_ERROR;
    } catch (...) {
        write_error(error, error_capacity, "Unexpected builder failure");
        return NR_NVFP4_INTERNAL_ERROR;
    }
}

extern "C" NR_NVFP4_EXPORT int32_t NR_NVFP4_CALL nr_nvfp4_build_contract(
    const NrNvfp4ContractInput* input, void* params, uint64_t capacity,
    NrNvfp4ContractInfo* info, char* error, uint32_t error_capacity) {
    if (!input) return NR_NVFP4_BAD_ABI;
    switch (input->variant) {
    case 0: return build_contract_variant<nr_nvfp4::DefaultContract>("nr_nvfp4_contract_v1_128x128x128", input, params, capacity, info, error, error_capacity);
    case 1: return build_contract_variant<nr_nvfp4::NarrowContract>("nr_nvfp4_contract_v1_128x64x128", input, params, capacity, info, error, error_capacity);
    case 2: return build_contract_variant<nr_nvfp4::DeepNarrowContract>("nr_nvfp4_contract_v1_128x64x256", input, params, capacity, info, error, error_capacity);
    case 3: return build_contract_variant<nr_nvfp4::PingpongContract>("nr_nvfp4_contract_v1_128x64x128_pingpong", input, params, capacity, info, error, error_capacity);
    default: write_error(error, error_capacity, "Unknown kernel variant"); return NR_NVFP4_UNSUPPORTED;
    }
}
extern "C" NR_NVFP4_EXPORT const char* NR_NVFP4_CALL nr_nvfp4_build_id_v1(void) {
    return NR_NVFP4_BUILD_ID;
}
