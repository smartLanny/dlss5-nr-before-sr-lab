// Reusable WARP contract test for the production RR linear-depth shader.
//
// This file is intentionally standalone. It includes the production shader
// string, compiles it with the Windows D3DCompiler target cs_5_0, uploads
// R32_FLOAT and R16_FLOAT source textures, dispatches on D3D12 WARP, and reads
// back a private R32_FLOAT output. It never calls NGX or changes an RR input.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <dxgi1_4.h>

#include "../src/nr_rr_depth_shader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

constexpr DWORD kFenceTimeoutMs = 20000;

void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

void Check(HRESULT result, const char *message) {
    if (FAILED(result)) {
        std::fprintf(stderr, "%s: 0x%08lx\n", message,
                     static_cast<unsigned long>(result));
        throw std::runtime_error(message);
    }
}

template <typename T> struct Com {
    T *p = nullptr;
    Com() = default;
    ~Com() { if (p != nullptr) p->Release(); }
    Com(const Com &) = delete;
    Com &operator=(const Com &) = delete;
    Com(Com &&other) noexcept : p(other.p) { other.p = nullptr; }
    Com &operator=(Com &&other) noexcept {
        if (p != nullptr) p->Release();
        p = other.p;
        other.p = nullptr;
        return *this;
    }
    T *operator->() const { return p; }
    operator T *() const { return p; }
    void **address() { return reinterpret_cast<void **>(&p); }
};

std::uint16_t FloatToHalf(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    const std::uint32_t exponent = (bits >> 23) & 0xffu;
    const std::uint32_t mantissa = bits & 0x7fffffu;
    if (exponent == 0xffu) {
        return static_cast<std::uint16_t>(sign | 0x7c00u |
                                          (mantissa ? 0x0200u : 0u));
    }
    const int half_exponent = static_cast<int>(exponent) - 127 + 15;
    if (exponent == 0 || half_exponent <= 0) {
        if (half_exponent < -10) return static_cast<std::uint16_t>(sign);
        const std::uint32_t hidden = mantissa | 0x800000u;
        const int shift = 14 - half_exponent;
        const std::uint32_t rounded = hidden >> shift;
        return static_cast<std::uint16_t>(sign | rounded);
    }
    if (half_exponent >= 31) return static_cast<std::uint16_t>(sign | 0x7c00u);
    const std::uint32_t rounded_mantissa = (mantissa + 0x1000u) >> 13;
    if (rounded_mantissa == 0x400u) {
        if (half_exponent == 30)
            return static_cast<std::uint16_t>(sign | 0x7c00u);
        return static_cast<std::uint16_t>(
            sign | (static_cast<std::uint32_t>(half_exponent + 1) << 10));
    }
    return static_cast<std::uint16_t>(
        sign | (static_cast<std::uint32_t>(half_exponent) << 10) |
        rounded_mantissa);
}

float HalfToFloat(std::uint16_t value) {
    const std::uint32_t sign = (value & 0x8000u) << 16;
    const std::uint32_t exponent = (value >> 10) & 31u;
    const std::uint32_t mantissa = value & 1023u;
    std::uint32_t bits = 0;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            std::uint32_t normalized = mantissa;
            int exponent_value = -14;
            while ((normalized & 0x400u) == 0) {
                normalized <<= 1;
                --exponent_value;
            }
            normalized &= 0x3ffu;
            bits = sign |
                   (static_cast<std::uint32_t>(exponent_value + 127) << 23) |
                   (normalized << 13);
        }
    } else if (exponent == 31) {
        bits = sign | 0x7f800000u | (mantissa << 13);
    } else {
        bits = sign |
               (static_cast<std::uint32_t>(static_cast<int>(exponent) - 15 + 127)
                << 23) |
               (mantissa << 13);
    }
    float result = 0.0f;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

struct Texture {
    Com<ID3D12Resource> resource;
    Com<ID3D12Resource> upload;
    Com<ID3D12Resource> readback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 byte_size = 0;
    UINT width = 0;
    UINT height = 0;
    UINT bytes_per_pixel = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
};

struct ProjectionCase {
    const char *name;
    bool inverted;
    float a;
    float b;
    float near_z;
    float far_z;
};

// Independent oracle. This intentionally does not call nr_rr_depth::Convert.
float OracleDepth(const ProjectionCase &projection, float view_z) {
    const float far_depth = projection.inverted ? 0.0f : 1.0f;
    if (!std::isfinite(view_z) || view_z <= 0.0f) return far_depth;
    const float projected = projection.a + projection.b / view_z;
    if (!std::isfinite(projected)) return far_depth;
    return std::clamp(projected, 0.0f, 1.0f);
}

class Gpu {
public:
    Com<ID3D12Device> device;
    Com<ID3D12CommandQueue> queue;
    Com<ID3D12CommandAllocator> allocator;
    Com<ID3D12GraphicsCommandList> list;
    Com<ID3D12Fence> fence;
    Com<ID3D12RootSignature> root;
    Com<ID3D12PipelineState> pipeline;
    Com<ID3D12DescriptorHeap> descriptors;
    UINT descriptor_increment = 0;
    UINT64 fence_value = 0;
    HANDLE fence_event = nullptr;

    Gpu() {
        Com<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_ID3D12Debug, debug.address())))
            debug->EnableDebugLayer();

        Com<IDXGIFactory4> factory;
        Check(CreateDXGIFactory1(IID_IDXGIFactory4, factory.address()),
              "Create DXGI factory");
        Com<IDXGIAdapter1> warp;
        Check(factory->EnumWarpAdapter(IID_IDXGIAdapter1, warp.address()),
              "Find WARP adapter");
        DXGI_ADAPTER_DESC1 adapter_desc{};
        Check(warp->GetDesc1(&adapter_desc), "Read WARP adapter description");
        Check(D3D12CreateDevice(warp.p, D3D_FEATURE_LEVEL_11_0,
                                IID_ID3D12Device, device.address()),
              "Create WARP D3D12 device");
        std::printf("RR depth GPU adapter: WARP (%ls)\n",
                    adapter_desc.Description);

        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        Check(device->CreateCommandQueue(&queue_desc, IID_ID3D12CommandQueue,
                                         queue.address()),
              "Create command queue");
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             IID_ID3D12CommandAllocator,
                                             allocator.address()),
              "Create command allocator");
        Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                        allocator.p, nullptr,
                                        IID_ID3D12GraphicsCommandList,
                                        list.address()),
              "Create command list");
        Check(list->Close(), "Close initial command list");
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_ID3D12Fence,
                                  fence.address()),
              "Create fence");
        fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        Require(fence_event != nullptr, "Create fence event");

        CompileProductionShader();
        CreateRootSignature();
        CreatePipeline();

        D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
        heap_desc.NumDescriptors = 2;
        heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        Check(device->CreateDescriptorHeap(&heap_desc,
                                           IID_ID3D12DescriptorHeap,
                                           descriptors.address()),
              "Create shader-visible descriptor heap");
        descriptor_increment = device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        std::printf("RR depth GPU fence timeout: %lu ms\n",
                    static_cast<unsigned long>(kFenceTimeoutMs));
    }

    ~Gpu() {
        if (fence_event != nullptr) CloseHandle(fence_event);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle(UINT index) const {
        D3D12_CPU_DESCRIPTOR_HANDLE handle =
            descriptors->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(index) * descriptor_increment;
        return handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle(UINT index) const {
        D3D12_GPU_DESCRIPTOR_HANDLE handle =
            descriptors->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<UINT64>(index) * descriptor_increment;
        return handle;
    }

    void Begin() {
        Check(allocator->Reset(), "Reset command allocator");
        Check(list->Reset(allocator.p, nullptr), "Reset command list");
    }

    void SubmitAndWait() {
        Check(list->Close(), "Close command list");
        ID3D12CommandList *command_lists[] = {list.p};
        queue->ExecuteCommandLists(1, command_lists);
        ++fence_value;
        Check(queue->Signal(fence.p, fence_value), "Signal fence");
        if (fence->GetCompletedValue() < fence_value) {
            Check(fence->SetEventOnCompletion(fence_value, fence_event),
                  "Set fence completion event");
            const DWORD wait = WaitForSingleObject(fence_event,
                                                    kFenceTimeoutMs);
            if (wait != WAIT_OBJECT_0) {
                const HRESULT removed = device->GetDeviceRemovedReason();
                std::fprintf(stderr,
                             "FENCE_TIMEOUT_OR_FAILURE wait=%lu removed=0x%08lx\n",
                             static_cast<unsigned long>(wait),
                             static_cast<unsigned long>(removed));
                throw std::runtime_error("20 second fence wait failed");
            }
        }
        Check(device->GetDeviceRemovedReason(), "WARP device removed");
    }

    void Barrier(ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
                 D3D12_RESOURCE_STATES after) {
        if (before == after) return;
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &barrier);
    }

private:
    void CompileProductionShader() {
        Com<ID3DBlob> code;
        Com<ID3DBlob> errors;
        const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS |
                           D3DCOMPILE_IEEE_STRICTNESS |
                           D3DCOMPILE_OPTIMIZATION_LEVEL3;
        const HRESULT result = D3DCompile(
            kRrDepthShader, sizeof(kRrDepthShader) - 1,
            "src/nr_rr_depth_shader.h", nullptr, nullptr, "ConvertRrDepthCS",
            "cs_5_0", flags, 0, &code.p, &errors.p);
        if (errors.p != nullptr && errors->GetBufferSize() != 0)
            std::fprintf(stderr, "%s\n",
                         static_cast<const char *>(errors->GetBufferPointer()));
        Check(result, "Compile production RR depth shader as cs_5_0");
        Require(code->GetBufferSize() >= 4 &&
                    std::memcmp(code->GetBufferPointer(), "DXBC", 4) == 0,
                "D3DCompile did not return DXBC");
        Com<ID3D11ShaderReflection> reflection;
        Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(),
                         IID_ID3D11ShaderReflection, reflection.address()),
              "Reflect production RR depth DXBC");
        D3D11_SHADER_DESC shader_desc{};
        Check(reflection->GetDesc(&shader_desc),
              "Read production RR depth shader model");
        // The MinGW SDK header does not expose D3D11_SHVER_GET_* macros, so
        // decode the reflected DXBC version token directly: type is bits
        // 16..31, major is bits 4..7, and minor is bits 0..3.
        const UINT shader_type = (shader_desc.Version >> 16) & 0xffffu;
        const UINT shader_major = (shader_desc.Version >> 4) & 0xfu;
        const UINT shader_minor = shader_desc.Version & 0xfu;
        Require(shader_type == 5u && shader_major == 5u && shader_minor == 0u,
                "D3DCompile blob is not compute shader model 5.0");
        std::printf(
            "PASS production RR depth shader D3DCompile target=cs_5_0 "
            "DXBC shader_model=cs_%u_%u (%zu bytes)\n",
            shader_major, shader_minor, code->GetBufferSize());
        shader_code_.p = code.p;
        code.p = nullptr;
    }

    void CreateRootSignature() {
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[0].NumDescriptors = 1;
        ranges[0].BaseShaderRegister = 0;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[1].NumDescriptors = 1;
        ranges[1].BaseShaderRegister = 0;
        ranges[1].OffsetInDescriptorsFromTableStart = 1;

        D3D12_ROOT_PARAMETER parameters[2]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[0].DescriptorTable.NumDescriptorRanges = 2;
        parameters[0].DescriptorTable.pDescriptorRanges = ranges;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[1].Constants.ShaderRegister = 0;
        parameters[1].Constants.RegisterSpace = 0;
        parameters[1].Constants.Num32BitValues = 8;

        D3D12_ROOT_SIGNATURE_DESC description{};
        description.NumParameters = 2;
        description.pParameters = parameters;
        Com<ID3DBlob> serialized;
        Com<ID3DBlob> errors;
        Check(D3D12SerializeRootSignature(
                  &description, D3D_ROOT_SIGNATURE_VERSION_1,
                  &serialized.p, &errors.p),
              "Serialize RR depth root signature");
        Check(device->CreateRootSignature(
                  0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                  IID_ID3D12RootSignature, root.address()),
              "Create RR depth root signature");
    }

    void CreatePipeline() {
        D3D12_COMPUTE_PIPELINE_STATE_DESC description{};
        description.pRootSignature = root.p;
        description.CS.pShaderBytecode = shader_code_->GetBufferPointer();
        description.CS.BytecodeLength = shader_code_->GetBufferSize();
        Check(device->CreateComputePipelineState(
                  &description, IID_ID3D12PipelineState, pipeline.address()),
              "Create RR depth compute pipeline");
    }

    Com<ID3DBlob> shader_code_;
};

Texture CreateTexture(Gpu &gpu, UINT width, UINT height, DXGI_FORMAT format,
                      D3D12_RESOURCE_FLAGS flags,
                      D3D12_RESOURCE_STATES initial_state) {
    Texture result{};
    result.width = width;
    result.height = height;
    result.format = format;
    result.state = initial_state;
    result.bytes_per_pixel = format == DXGI_FORMAT_R16_FLOAT ? 2u : 4u;

    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width = width;
    description.Height = height;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = format;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    description.Flags = flags;

    D3D12_HEAP_PROPERTIES default_heap{};
    default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    Check(gpu.device->CreateCommittedResource(
              &default_heap, D3D12_HEAP_FLAG_NONE, &description, initial_state,
              nullptr, IID_ID3D12Resource, result.resource.address()),
          "Create texture resource");
    UINT64 row_size = 0;
    UINT row_count = 0;
    gpu.device->GetCopyableFootprints(&description, 0, 1, 0,
                                      &result.footprint, &row_count, &row_size,
                                      &result.byte_size);

    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = result.byte_size;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.Format = DXGI_FORMAT_UNKNOWN;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES upload_heap{};
    upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    Check(gpu.device->CreateCommittedResource(
              &upload_heap, D3D12_HEAP_FLAG_NONE, &buffer,
              D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
              IID_ID3D12Resource, result.upload.address()),
          "Create upload buffer");
    D3D12_HEAP_PROPERTIES readback_heap{};
    readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
    Check(gpu.device->CreateCommittedResource(
              &readback_heap, D3D12_HEAP_FLAG_NONE, &buffer,
              D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
              IID_ID3D12Resource, result.readback.address()),
          "Create readback buffer");
    return result;
}

void FillSourceUpload(Texture &texture, const std::vector<float> &values,
                      std::vector<std::uint8_t> &tight_bytes) {
    const UINT row_bytes = texture.width * texture.bytes_per_pixel;
    tight_bytes.resize(static_cast<size_t>(row_bytes) * texture.height);
    for (size_t i = 0; i < values.size(); ++i) {
        if (texture.format == DXGI_FORMAT_R16_FLOAT) {
            const std::uint16_t half = FloatToHalf(values[i]);
            std::memcpy(tight_bytes.data() + i * 2, &half, sizeof(half));
        } else {
            std::memcpy(tight_bytes.data() + i * 4, &values[i], sizeof(float));
        }
    }
    void *mapped = nullptr;
    const D3D12_RANGE no_reads{0, 0};
    Check(texture.upload->Map(0, &no_reads, &mapped), "Map source upload");
    std::memset(mapped, 0, static_cast<size_t>(texture.byte_size));
    auto *destination = static_cast<std::uint8_t *>(mapped);
    for (UINT y = 0; y < texture.height; ++y) {
        std::memcpy(destination + y * texture.footprint.Footprint.RowPitch,
                    tight_bytes.data() + static_cast<size_t>(y) * row_bytes,
                    row_bytes);
    }
    texture.upload->Unmap(0, nullptr);
}

std::vector<std::uint8_t> ReadTextureBytes(Texture &texture) {
    const UINT row_bytes = texture.width * texture.bytes_per_pixel;
    std::vector<std::uint8_t> result(
        static_cast<size_t>(row_bytes) * texture.height);
    void *mapped = nullptr;
    const D3D12_RANGE read_range{0, static_cast<SIZE_T>(texture.byte_size)};
    Check(texture.readback->Map(0, &read_range, &mapped), "Map texture readback");
    const auto *source = static_cast<const std::uint8_t *>(mapped);
    for (UINT y = 0; y < texture.height; ++y) {
        std::memcpy(result.data() + static_cast<size_t>(y) * row_bytes,
                    source + y * texture.footprint.Footprint.RowPitch,
                    row_bytes);
    }
    const D3D12_RANGE no_writes{0, 0};
    texture.readback->Unmap(0, &no_writes);
    return result;
}

void CopyTextureToReadback(Gpu &gpu, Texture &texture) {
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = texture.resource.p;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    source.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = texture.readback.p;
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = texture.footprint;
    gpu.list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
}

std::vector<float> MakeSourceValues(UINT width, UINT height, UINT base_x,
                                    UINT base_y, UINT output_width,
                                    UINT output_height,
                                    const ProjectionCase &projection) {
    std::vector<float> values(static_cast<size_t>(width) * height,
                              projection.near_z);
    for (UINT y = 0; y < height; ++y) {
        for (UINT x = 0; x < width; ++x) {
            const size_t index = static_cast<size_t>(y) * width + x;
            if (x < base_x || y < base_y || x >= base_x + output_width ||
                y >= base_y + output_height) {
                values[index] = projection.near_z * 2.0f;
                continue;
            }
            const UINT local_x = x - base_x;
            const UINT local_y = y - base_y;
            switch ((local_x + local_y * output_width) % 7u) {
            case 0: values[index] = 0.0f; break;
            case 1: values[index] = -1.0f; break;
            case 2: values[index] = std::numeric_limits<float>::quiet_NaN(); break;
            case 3: values[index] = std::numeric_limits<float>::infinity(); break;
            case 4: values[index] = projection.far_z * 2.0f; break;
            case 5: values[index] = projection.near_z * 1.5f; break;
            default: values[index] = projection.far_z * 0.75f; break;
            }
        }
    }
    return values;
}

void ValidateOutput(const ProjectionCase &projection,
                    const std::vector<float> &source_values,
                    const std::vector<std::uint8_t> &output_bytes,
                    UINT source_width, UINT base_x,
                    UINT base_y, UINT width, UINT height,
                    DXGI_FORMAT source_format) {
    Require(output_bytes.size() == static_cast<size_t>(width) * height * 4,
            "Unexpected output readback size");
    unsigned zero_count = 0;
    unsigned negative_count = 0;
    unsigned nan_count = 0;
    unsigned far_count = 0;
    for (UINT y = 0; y < height; ++y) {
        for (UINT x = 0; x < width; ++x) {
            const size_t source_index = static_cast<size_t>(y + base_y) *
                                            source_width + x + base_x;
            float source = source_values[source_index];
            if (source_format == DXGI_FORMAT_R16_FLOAT)
                source = HalfToFloat(FloatToHalf(source));
            float actual = 0.0f;
            std::memcpy(&actual,
                        output_bytes.data() +
                            (static_cast<size_t>(y) * width + x) * 4,
                        sizeof(actual));
            const float expected = OracleDepth(projection, source);
            if (std::isnan(source)) ++nan_count;
            if (source == 0.0f) ++zero_count;
            if (source < 0.0f) ++negative_count;
            if (std::isfinite(source) && source > projection.far_z)
                ++far_count;
            Require(std::isfinite(actual), "Output contains non-finite depth");
            Require(std::fabs(actual - expected) <= 2.0e-4f,
                    "Output differs from independent depth oracle");
            Require(actual >= 0.0f && actual <= 1.0f,
                    "Output is outside normalized hardware depth");
        }
    }
    Require(zero_count != 0, "GPU fixture did not exercise zero source depth");
    Require(negative_count != 0, "GPU fixture did not exercise negative source depth");
    Require(nan_count != 0, "GPU fixture did not exercise NaN source depth");
    Require(far_count != 0, "GPU fixture did not exercise far out-of-range depth");
    std::printf("PASS %s %s %ux%u offset=%u,%u\n", projection.name,
                source_format == DXGI_FORMAT_R16_FLOAT ? "R16_FLOAT" : "R32_FLOAT",
                width, height, base_x, base_y);
}

void RunCase(Gpu &gpu, const ProjectionCase &projection, DXGI_FORMAT format,
             UINT width, UINT height, UINT base_x, UINT base_y) {
    const UINT source_width = width + base_x + 2;
    const UINT source_height = height + base_y + 2;
    Texture source = CreateTexture(gpu, source_width, source_height, format,
                                   D3D12_RESOURCE_FLAG_NONE,
                                   D3D12_RESOURCE_STATE_COPY_DEST);
    Texture output = CreateTexture(gpu, width, height, DXGI_FORMAT_R32_FLOAT,
                                   D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const D3D12_RESOURCE_DESC output_desc = output.resource->GetDesc();
    Require(output_desc.Format == DXGI_FORMAT_R32_FLOAT,
            "Private RR output is not R32_FLOAT");
    Require((output_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0,
            "Private RR output is not UAV-capable");
    Require(source.resource.p != output.resource.p,
            "RR input and private output alias");

    const std::vector<float> source_values = MakeSourceValues(
        source_width, source_height, base_x, base_y, width, height, projection);
    std::vector<std::uint8_t> original_source_bytes;
    FillSourceUpload(source, source_values, original_source_bytes);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    gpu.device->CreateShaderResourceView(source.resource.p, &srv,
                                         gpu.CpuHandle(0));
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = DXGI_FORMAT_R32_FLOAT;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    gpu.device->CreateUnorderedAccessView(output.resource.p, nullptr, &uav,
                                          gpu.CpuHandle(1));

    gpu.Begin();
    D3D12_TEXTURE_COPY_LOCATION upload_location{};
    upload_location.pResource = source.upload.p;
    upload_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    upload_location.PlacedFootprint = source.footprint;
    D3D12_TEXTURE_COPY_LOCATION source_location{};
    source_location.pResource = source.resource.p;
    source_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    gpu.list->CopyTextureRegion(&source_location, 0, 0, 0, &upload_location,
                                nullptr);
    gpu.Barrier(source.resource.p, D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    gpu.list->SetPipelineState(gpu.pipeline.p);
    gpu.list->SetComputeRootSignature(gpu.root.p);
    ID3D12DescriptorHeap *heaps[] = {gpu.descriptors.p};
    gpu.list->SetDescriptorHeaps(1, heaps);
    gpu.list->SetComputeRootDescriptorTable(0, gpu.GpuHandle(0));
    std::uint32_t constants[8] = {
        width, height, base_x, base_y, 0, 0, 0, 0};
    std::memcpy(constants + 4, &projection.a, sizeof(float));
    std::memcpy(constants + 5, &projection.b, sizeof(float));
    const float far_depth = projection.inverted ? 0.0f : 1.0f;
    std::memcpy(constants + 6, &far_depth, sizeof(float));
    gpu.list->SetComputeRoot32BitConstants(1, 8, constants, 0);
    gpu.list->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
    gpu.Barrier(source.resource.p,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_COPY_SOURCE);
    gpu.Barrier(output.resource.p, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COPY_SOURCE);
    CopyTextureToReadback(gpu, source);
    CopyTextureToReadback(gpu, output);
    gpu.SubmitAndWait();

    const std::vector<std::uint8_t> source_after = ReadTextureBytes(source);
    Require(source_after == original_source_bytes,
            "Original RR input changed during private conversion");
    const std::vector<std::uint8_t> output_bytes = ReadTextureBytes(output);
    ValidateOutput(projection, source_values, output_bytes, source_width,
                   base_x, base_y, width, height, format);
    std::printf("PASS source unchanged and private R32 output asserted\n");
}

int main() {
    try {
        Gpu gpu;
        constexpr float near_z = 0.25f;
        constexpr float far_z = 100.0f;
        const ProjectionCase cases[] = {
            {"normal-finite", false, far_z / (far_z - near_z),
             -near_z * far_z / (far_z - near_z), near_z, far_z},
            {"normal-infinite", false, 1.0f, -near_z, near_z, far_z},
            {"reverse-finite", true, -near_z / (far_z - near_z),
             near_z * far_z / (far_z - near_z), near_z, far_z},
            {"reverse-infinite", true, 0.0f, near_z, near_z, far_z},
        };
        for (const ProjectionCase &projection : cases) {
            RunCase(gpu, projection, DXGI_FORMAT_R32_FLOAT, 13, 7, 3, 2);
            RunCase(gpu, projection, DXGI_FORMAT_R16_FLOAT, 11, 5, 2, 3);
        }
        std::puts("RR depth GPU contract: PASS (8 WARP cases, cs_5_0, 20s fence)");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "RR depth GPU contract: FAIL %s\n", error.what());
        return 1;
    }
}
