#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../src/interop/nr_fsr31_dlss.h"

using Microsoft::WRL::ComPtr;

namespace {
constexpr UINT kRenderWidth = 640, kRenderHeight = 360;
constexpr UINT kOutputWidth = 1280, kOutputHeight = 720;

void Check(HRESULT hr, const char *what) {
#ifdef NR_CASCADE_FAIL_CLOSED
    if (FAILED(hr)) {
        std::cerr << "FATAL_CASCADE: " << what << " hr=" << std::hex << unsigned(hr) << '\n';
        std::cerr.flush(); ExitProcess(2);
    }
#else
    if (FAILED(hr)) throw std::runtime_error(std::string(what) + " failed hr=0x" + [&] {
        char text[16]{}; sprintf_s(text, "%08X", static_cast<unsigned>(hr)); return std::string(text);
    }());
#endif
}
[[noreturn]] void FatalSubmittedGpu(const char *what, HRESULT hr = E_FAIL) {
    std::cerr << "FATAL_SUBMITTED_GPU: " << what << " hr=0x" << std::hex
              << static_cast<unsigned>(hr) << std::dec << "; process exits without releasing GPU references\n";
    std::cerr.flush(); std::cout.flush(); ExitProcess(2);
}

struct DeviceContext {
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    HANDLE event = nullptr;
    UINT64 next_fence = 1;
    std::wstring name;
    DeviceContext() = default;
    DeviceContext(const DeviceContext &) = delete;
    DeviceContext &operator=(const DeviceContext &) = delete;
    DeviceContext(DeviceContext &&other) noexcept
        : adapter(std::move(other.adapter)), device(std::move(other.device)), queue(std::move(other.queue)),
          allocator(std::move(other.allocator)), list(std::move(other.list)), fence(std::move(other.fence)),
          event(std::exchange(other.event, nullptr)), next_fence(other.next_fence), name(std::move(other.name)) {}
    ~DeviceContext() { if (event) CloseHandle(event); }
    void SubmitAndWait() {
        Check(list->Close(), "command list close");
        ID3D12CommandList *lists[] = {list.Get()};
        queue->ExecuteCommandLists(1, lists);
        const UINT64 value = next_fence++;
        const HRESULT signal = queue->Signal(fence.Get(), value);
        if (FAILED(signal)) FatalSubmittedGpu("queue signal failed after ExecuteCommandLists", signal);
        UINT64 completed = fence->GetCompletedValue();
        if (completed == UINT64_MAX) FatalSubmittedGpu("device removed before fence wait", device->GetDeviceRemovedReason());
        if (completed < value) {
            const HRESULT arm = fence->SetEventOnCompletion(value, event);
            if (FAILED(arm)) FatalSubmittedGpu("SetEventOnCompletion failed after submission", arm);
            const DWORD waited = WaitForSingleObject(event, 15000);
            completed = fence->GetCompletedValue();
            if (completed == UINT64_MAX) FatalSubmittedGpu("device removed during fence wait", device->GetDeviceRemovedReason());
            if (waited != WAIT_OBJECT_0 || completed < value)
                FatalSubmittedGpu("GPU fence timeout after submission", device->GetDeviceRemovedReason());
        }
    }
    void Reset() {
        Check(allocator->Reset(), "allocator reset");
        Check(list->Reset(allocator.Get(), nullptr), "command list reset");
    }
};

DeviceContext MakeDevice(bool warp) {
    DeviceContext out;
    ComPtr<IDXGIFactory6> factory;
    Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
    if (warp) {
        Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&out.adapter)), "EnumWarpAdapter");
    } else {
        for (UINT i = 0;; ++i) {
            ComPtr<IDXGIAdapter1> candidate;
            if (factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                    IID_PPV_ARGS(&candidate)) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 desc{}; candidate->GetDesc1(&desc);
            if (desc.VendorId == 0x10de && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_12_0,
                                            IID_PPV_ARGS(&out.device)))) {
                out.adapter = candidate; out.name = desc.Description; break;
            }
        }
        if (!out.device) throw std::runtime_error("no NVIDIA D3D12 adapter; hardware mode never falls back to WARP");
    }
    if (!out.device) Check(D3D12CreateDevice(out.adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                             IID_PPV_ARGS(&out.device)), "D3D12CreateDevice");
    DXGI_ADAPTER_DESC1 desc{}; out.adapter->GetDesc1(&desc); out.name = desc.Description;
    D3D12_COMMAND_QUEUE_DESC q{}; q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Check(out.device->CreateCommandQueue(&q, IID_PPV_ARGS(&out.queue)), "CreateCommandQueue");
    Check(out.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(&out.allocator)), "CreateCommandAllocator");
    Check(out.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, out.allocator.Get(), nullptr,
                                        IID_PPV_ARGS(&out.list)), "CreateCommandList");
    Check(out.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&out.fence)), "CreateFence");
    out.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!out.event) throw std::runtime_error("CreateEvent failed");
    return out;
}

ComPtr<ID3D12Resource> Texture(ID3D12Device *device, UINT w, UINT h, DXGI_FORMAT format,
                               D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w; d.Height = h; d.DepthOrArraySize = 1; d.MipLevels = 1;
    d.Format = format; d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; d.Flags = flags;
    ComPtr<ID3D12Resource> result;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, state, nullptr,
                                           IID_PPV_ARGS(&result)), "CreateCommittedResource(texture)");
    return result;
}

ComPtr<ID3D12Resource> Buffer(ID3D12Device *device, UINT64 bytes, D3D12_HEAP_TYPE type,
                              D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = type;
    D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = bytes;
    d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1; d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> result;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, state, nullptr,
                                           IID_PPV_ARGS(&result)), "CreateCommittedResource(buffer)");
    return result;
}

uint16_t Half(float value) {
    uint32_t bits; memcpy(&bits, &value, sizeof(bits));
    const uint32_t sign = (bits >> 16) & 0x8000, exp = (bits >> 23) & 0xff, mant = bits & 0x7fffff;
    if (exp == 255) return static_cast<uint16_t>(sign | 0x7c00 | (mant ? 0x200 : 0));
    int e = static_cast<int>(exp) - 127 + 15;
    if (e <= 0) return static_cast<uint16_t>(sign);
    if (e >= 31) return static_cast<uint16_t>(sign | 0x7c00);
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(e) << 10) | (mant >> 13));
}
float FloatFromHalf(uint16_t h) {
    const uint32_t sign = (h & 0x8000u) << 16, exp = (h >> 10) & 31, mant = h & 1023;
    uint32_t bits;
    if (!exp) bits = sign | (mant ? 0x33800000u : 0u); // inputs/outputs here should not rely on subnormal precision
    else if (exp == 31) bits = sign | 0x7f800000u | (mant << 13);
    else bits = sign | ((exp + 112) << 23) | (mant << 13);
    float value; memcpy(&value, &bits, sizeof(value)); return value;
}

void Upload(DeviceContext &ctx, ID3D12Resource *texture, const void *pixels, UINT source_pitch,
            D3D12_RESOURCE_STATES final_state, std::vector<ComPtr<ID3D12Resource>> &keepalive) {
    const auto td = texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT rows{}; UINT64 row_bytes{}, total{};
    ctx.device->GetCopyableFootprints(&td, 0, 1, 0, &fp, &rows, &row_bytes, &total);
    auto upload = Buffer(ctx.device.Get(), total, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    uint8_t *mapped{}; Check(upload->Map(0, nullptr, reinterpret_cast<void **>(&mapped)), "upload Map");
    for (UINT y = 0; y < rows; ++y)
        memcpy(mapped + fp.Offset + UINT64(y) * fp.Footprint.RowPitch,
               static_cast<const uint8_t *>(pixels) + UINT64(y) * source_pitch, static_cast<size_t>(row_bytes));
    upload->Unmap(0, nullptr);
    D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = texture; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = upload.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = fp;
    ctx.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = texture; b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    b.Transition.StateAfter = final_state; b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    ctx.list->ResourceBarrier(1, &b); keepalive.push_back(upload);
}

FfxApiResource View(ID3D12Resource *resource, uint32_t state) {
    return ffxApiGetResourceDX12(resource, state);
}

struct FrameResources {
    ComPtr<ID3D12Resource> color, depth, motion, exposure, output;
    ffxCreateBackendDX12Desc backend{};
    ffxCreateContextDescUpscale create{};
    ffxDispatchDescUpscale dispatch{};
};

FrameResources MakeFrame(DeviceContext &ctx, bool upload) {
    FrameResources f;
    f.color = Texture(ctx.device.Get(), kRenderWidth, kRenderHeight, DXGI_FORMAT_R16G16B16A16_FLOAT,
                      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    f.depth = Texture(ctx.device.Get(), kRenderWidth, kRenderHeight, DXGI_FORMAT_R32_FLOAT,
                      D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    f.motion = Texture(ctx.device.Get(), kRenderWidth, kRenderHeight, DXGI_FORMAT_R16G16_FLOAT,
                       D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    f.exposure = Texture(ctx.device.Get(), 1, 1, DXGI_FORMAT_R32_FLOAT,
                         D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    f.output = Texture(ctx.device.Get(), kOutputWidth, kOutputHeight, DXGI_FORMAT_R16G16B16A16_FLOAT,
                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    std::vector<ComPtr<ID3D12Resource>> uploads;
    if (upload) {
        std::vector<uint16_t> color(size_t(kRenderWidth) * kRenderHeight * 4);
        for (UINT y = 0; y < kRenderHeight; ++y) for (UINT x = 0; x < kRenderWidth; ++x) {
            const size_t i = (size_t(y) * kRenderWidth + x) * 4;
            color[i] = Half(0.05f + 0.8f * x / float(kRenderWidth - 1));
            color[i + 1] = Half(0.1f + 0.7f * y / float(kRenderHeight - 1));
            color[i + 2] = Half(((x / 32 + y / 32) & 1) ? 0.75f : 0.2f); color[i + 3] = Half(1.0f);
        }
        std::vector<float> depth(size_t(kRenderWidth) * kRenderHeight);
        for (UINT y = 0; y < kRenderHeight; ++y) for (UINT x = 0; x < kRenderWidth; ++x)
            depth[size_t(y) * kRenderWidth + x] = 0.95f - 0.5f * y / float(kRenderHeight - 1);
        std::vector<uint16_t> motion(size_t(kRenderWidth) * kRenderHeight * 2, Half(0.0f));
        const float exposure = 1.0f;
        Upload(ctx, f.color.Get(), color.data(), kRenderWidth * 8, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, uploads);
        Upload(ctx, f.depth.Get(), depth.data(), kRenderWidth * 4, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, uploads);
        Upload(ctx, f.motion.Get(), motion.data(), kRenderWidth * 4, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, uploads);
        Upload(ctx, f.exposure.Get(), &exposure, 4, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, uploads);
        ctx.SubmitAndWait(); ctx.Reset();
    }
    f.backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; f.backend.device = ctx.device.Get();
    f.create.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE; f.create.header.pNext = &f.backend.header;
    f.create.flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_DEPTH_INVERTED;
    f.create.maxRenderSize = {kRenderWidth, kRenderHeight}; f.create.maxUpscaleSize = {kOutputWidth, kOutputHeight};
    f.dispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE; f.dispatch.commandList = ctx.list.Get();
    f.dispatch.color = View(f.color.Get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
    f.dispatch.depth = View(f.depth.Get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
    f.dispatch.motionVectors = View(f.motion.Get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
    f.dispatch.exposure = View(f.exposure.Get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
    f.dispatch.output = View(f.output.Get(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    f.dispatch.jitterOffset = {0.25f, -0.25f}; f.dispatch.motionVectorScale = {-float(kRenderWidth), -float(kRenderHeight)};
    f.dispatch.renderSize = {kRenderWidth, kRenderHeight}; f.dispatch.upscaleSize = {kOutputWidth, kOutputHeight};
    f.dispatch.frameTimeDelta = 16.6667f; f.dispatch.preExposure = 1.0f; f.dispatch.reset = true;
    f.dispatch.cameraNear = 0.1f; f.dispatch.cameraFar = 1000.0f; f.dispatch.cameraFovAngleVertical = 1.0f;
    f.dispatch.viewSpaceToMetersFactor = 1.0f;
    return f;
}
void BindFrame(FrameResources &f, DeviceContext &ctx) {
    f.backend.header.pNext = nullptr; f.backend.device = ctx.device.Get();
    f.create.header.pNext = &f.backend.header; f.dispatch.commandList = ctx.list.Get();
}

void Expect(const char *label, nr_fsr31_dlss::Status expected, ID3D12Device *device,
            const ffxCreateContextDescUpscale &c, const ffxDispatchDescUpscale &d,
            const nr_fsr31_dlss::Quality &quality) {
    nr_fsr31_dlss::Mapping m{}; const auto actual = nr_fsr31_dlss::Map(device, c, d, m, quality);
    std::cout << "case=" << label << " status=" << nr_fsr31_dlss::Name(actual) << '\n';
    if (actual != expected) throw std::runtime_error(std::string(label) + " expected " + nr_fsr31_dlss::Name(expected));
}

void RunMapCases(DeviceContext &ctx) {
    auto f = MakeFrame(ctx, false); BindFrame(f, ctx);
    const nr_fsr31_dlss::Quality quality{NVSDK_NGX_PerfQuality_Value_MaxPerf, 320, 180, 640, 360};
    Expect("valid-backend-chain", nr_fsr31_dlss::Status::Ready, ctx.device.Get(), f.create, f.dispatch, quality);
    nr_fsr31_dlss::Mapping temporal{};
    if (nr_fsr31_dlss::Map(ctx.device.Get(), f.create, f.dispatch, temporal, quality) != nr_fsr31_dlss::Status::Ready ||
        temporal.evaluate.InJitterOffsetX != 0.25f || temporal.evaluate.InJitterOffsetY != -0.25f)
        throw std::runtime_error("nonzero jitter was not passed through production Map");
    auto bad_create = f.create; ffxCreateContextDescHeader unknown{0x7fffffffu, nullptr}; bad_create.header.pNext = &unknown;
    Expect("unknown-create-extension", nr_fsr31_dlss::Status::UnsupportedExtension, ctx.device.Get(), bad_create, f.dispatch, quality);
    auto cyclic_backend = f.backend; cyclic_backend.header.pNext = &cyclic_backend.header; bad_create = f.create; bad_create.header.pNext = &cyclic_backend.header;
    Expect("cyclic-create-chain", nr_fsr31_dlss::Status::UnsupportedExtension, ctx.device.Get(), bad_create, f.dispatch, quality);
    auto bad_dispatch = f.dispatch; ffxDispatchDescHeader dispatch_unknown{0x7fffffffu, nullptr}; bad_dispatch.header.pNext = &dispatch_unknown;
    Expect("unknown-dispatch-extension", nr_fsr31_dlss::Status::UnsupportedExtension, ctx.device.Get(), f.create, bad_dispatch, quality);
    bad_create = f.create; bad_create.flags |= FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
    Expect("nonlinear-colour", nr_fsr31_dlss::Status::UnsupportedFlags, ctx.device.Get(), bad_create, f.dispatch, quality);
    bad_dispatch = f.dispatch; bad_dispatch.reactive = f.dispatch.depth;
    Expect("reactive-mask", nr_fsr31_dlss::Status::UnsupportedMask, ctx.device.Get(), f.create, bad_dispatch, quality);
    bad_dispatch = f.dispatch; bad_dispatch.transparencyAndComposition = f.dispatch.depth;
    Expect("transparency-mask", nr_fsr31_dlss::Status::UnsupportedMask, ctx.device.Get(), f.create, bad_dispatch, quality);
    bad_dispatch = f.dispatch; bad_dispatch.jitterOffset.x = std::numeric_limits<float>::quiet_NaN();
    Expect("invalid-jitter", nr_fsr31_dlss::Status::InvalidTemporalInput, ctx.device.Get(), f.create, bad_dispatch, quality);
    bad_dispatch = f.dispatch; bad_dispatch.motionVectorScale.x = 0;
    Expect("invalid-motion-scale", nr_fsr31_dlss::Status::InvalidTemporalInput, ctx.device.Get(), f.create, bad_dispatch, quality);
    bad_dispatch = f.dispatch; bad_dispatch.preExposure = 0;
    Expect("invalid-exposure", nr_fsr31_dlss::Status::InvalidTemporalInput, ctx.device.Get(), f.create, bad_dispatch, quality);
    auto narrow_quality = quality; narrow_quality.max_width = 639;
    Expect("quality-range", nr_fsr31_dlss::Status::QualityUnavailable, ctx.device.Get(), f.create, f.dispatch, narrow_quality);
    bad_dispatch = f.dispatch; bad_dispatch.output = bad_dispatch.color;
    Expect("aliased-output", nr_fsr31_dlss::Status::AliasedOutput, ctx.device.Get(), f.create, bad_dispatch, quality);
    bad_dispatch = f.dispatch; bad_dispatch.color.state = FFX_API_RESOURCE_STATE_COPY_SRC;
    Expect("wrong-state", nr_fsr31_dlss::Status::UnsupportedState, ctx.device.Get(), f.create, bad_dispatch, quality);
    bad_dispatch = f.dispatch; bad_dispatch.color.description.format = FFX_API_SURFACE_FORMAT_R32_FLOAT;
    Expect("wrong-format", nr_fsr31_dlss::Status::UnsupportedResource, ctx.device.Get(), f.create, bad_dispatch, quality);
    // A second D3D12CreateDevice call on the same WARP adapter may return the same COM device.
    // Use the NVIDIA adapter only as a distinct resource owner; no NGX inference occurs here.
    auto other = MakeDevice(false); auto cross = Texture(other.device.Get(), kRenderWidth, kRenderHeight,
        DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    bad_dispatch = f.dispatch; bad_dispatch.color = View(cross.Get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
    Expect("cross-device-resource", nr_fsr31_dlss::Status::DeviceMismatch, ctx.device.Get(), f.create, bad_dispatch, quality);
    auto foreign_backend = f.backend; foreign_backend.device = other.device.Get(); bad_create = f.create; bad_create.header.pNext = &foreign_backend.header;
    Expect("cross-device-backend", nr_fsr31_dlss::Status::DeviceMismatch, ctx.device.Get(), bad_create, f.dispatch, quality);
    std::wcout << L"adapter=" << ctx.name << L" mode=warp map_cases=16 quality=fixture-not-NGX-capability game_verified=false NR=false FG=false\n";
}

struct ReadbackStats {
    size_t finite = 0, nonfinite = 0;
    float rgb_minimum = INFINITY, rgb_maximum = -INFINITY, rgb_first = 0, rgb_last = 0;
    uint64_t hash = 1469598103934665603ull;
};
ReadbackStats ReadOutput(DeviceContext &ctx, ID3D12Resource *output) {
    const auto d = output->GetDesc(); D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT rows{}; UINT64 row_bytes{}, total{};
    ctx.device->GetCopyableFootprints(&d, 0, 1, 0, &fp, &rows, &row_bytes, &total);
    auto readback = Buffer(ctx.device.Get(), total, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_RESOURCE_BARRIER before{}; before.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; before.Transition.pResource = output;
    before.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS; before.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    before.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; ctx.list->ResourceBarrier(1, &before);
    D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = output; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = readback.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = fp;
    ctx.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    std::swap(before.Transition.StateBefore, before.Transition.StateAfter); ctx.list->ResourceBarrier(1, &before);
    ctx.SubmitAndWait();
    uint8_t *data{}; Check(readback->Map(0, nullptr, reinterpret_cast<void **>(&data)), "readback Map");
    ReadbackStats s;
    for (UINT y = 0; y < rows; ++y) {
        const auto *p = reinterpret_cast<const uint16_t *>(data + fp.Offset + UINT64(y) * fp.Footprint.RowPitch);
        for (UINT x = 0; x < kOutputWidth * 4; ++x) {
            const float v = FloatFromHalf(p[x]); s.hash = (s.hash ^ p[x]) * 1099511628211ull;
            if (std::isfinite(v)) {
                ++s.finite;
                if ((x & 3u) != 3u) { s.rgb_minimum = std::min(s.rgb_minimum, v); s.rgb_maximum = std::max(s.rgb_maximum, v); }
                if (y == 0 && x == 0) s.rgb_first = v;
                if (y + 1 == rows && x + 1 == kOutputWidth * 4 - 1) s.rgb_last = FloatFromHalf(p[x - 1]);
            }
            else ++s.nonfinite;
        }
    }
    readback->Unmap(0, nullptr); return s;
}

void RunHardware(const std::wstring &runtime_dir) {
    auto ctx = MakeDevice(false); auto f = MakeFrame(ctx, true); BindFrame(f, ctx);
    // The controlled renderer is static, so its temporal inputs are static too.
    f.dispatch.jitterOffset = {0.0f, 0.0f};
    const wchar_t *paths[] = {runtime_dir.c_str()}; NVSDK_NGX_FeatureCommonInfo common{};
    common.PathListInfo.Path = paths; common.PathListInfo.Length = 1;
    const std::wstring data_dir = runtime_dir + L"\\ngx-data"; CreateDirectoryW(data_dir.c_str(), nullptr);
    constexpr const char *project = "7a94da60-f8c9-4b8b-9d8a-22f0293c4801";
    NVSDK_NGX_Result result = NVSDK_NGX_D3D12_Init_with_ProjectID(project, NVSDK_NGX_ENGINE_TYPE_CUSTOM,
        "direct-sr-controlled-harness-1", data_dir.c_str(), ctx.device.Get(), &common, NVSDK_NGX_Version_API);
    std::cout << "ngx_init=0x" << std::hex << static_cast<unsigned>(result) << std::dec << '\n';
    if (NVSDK_NGX_FAILED(result)) throw std::runtime_error("NGX init failed");
    NVSDK_NGX_Parameter *caps = nullptr, *params = nullptr; NVSDK_NGX_Handle *feature = nullptr;
    bool initialized = true;
    try {
        result = NVSDK_NGX_D3D12_GetCapabilityParameters(&caps);
        std::cout << "ngx_caps=0x" << std::hex << static_cast<unsigned>(result) << std::dec << '\n';
        if (NVSDK_NGX_FAILED(result) || !caps) throw std::runtime_error("NGX capability query failed");
        int available = 0; caps->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &available);
        std::cout << "dlss_available=" << available << '\n'; if (!available) throw std::runtime_error("DLSS unavailable");
        nr_fsr31_dlss::Quality quality{};
        const auto selected = nr_fsr31_dlss::SelectQuality(caps, f.create, f.dispatch, quality);
        std::cout << "select_quality=" << nr_fsr31_dlss::Name(selected) << " mode=" << static_cast<int>(quality.mode)
                  << " range=" << quality.min_width << 'x' << quality.min_height << ".." << quality.max_width << 'x' << quality.max_height << '\n';
        if (selected != nr_fsr31_dlss::Status::Ready) throw std::runtime_error("no real NGX quality covers R/O");
        nr_fsr31_dlss::Mapping mapping{};
        const auto mapped = nr_fsr31_dlss::Map(ctx.device.Get(), f.create, f.dispatch, mapping, quality);
        std::cout << "map=" << nr_fsr31_dlss::Name(mapped) << '\n';
        if (mapped != nr_fsr31_dlss::Status::Ready) throw std::runtime_error("production Map rejected hardware frame");
        result = NVSDK_NGX_D3D12_AllocateParameters(&params);
        if (NVSDK_NGX_FAILED(result) || !params) throw std::runtime_error("NGX AllocateParameters failed");
        result = NGX_D3D12_CREATE_DLSS_EXT(ctx.list.Get(), 1, 1, &feature, params, &mapping.create);
        std::cout << "ngx_create=0x" << std::hex << static_cast<unsigned>(result) << std::dec << '\n';
        ctx.SubmitAndWait(); std::cout << "create_submitted=true create_fence_complete=true\n"; ctx.Reset();
        if (NVSDK_NGX_FAILED(result) || !feature) throw std::runtime_error("DLSS CreateFeature failed");
        std::vector<ReadbackStats> frames;
        for (unsigned frame = 0; frame < 3; ++frame) {
            mapping.evaluate.InReset = frame == 0 ? 1 : 0;
            mapping.evaluate.InJitterOffsetX = 0.0f; mapping.evaluate.InJitterOffsetY = 0.0f;
            result = NGX_D3D12_EVALUATE_DLSS_EXT(ctx.list.Get(), feature, params, &mapping.evaluate);
            std::cout << "frame=" << frame << " ngx_evaluate=0x" << std::hex << static_cast<unsigned>(result) << std::dec << '\n';
            if (NVSDK_NGX_FAILED(result)) throw std::runtime_error("DLSS EvaluateFeature failed");
            ctx.SubmitAndWait(); std::cout << "frame=" << frame << " submitted=true fence_complete=true\n"; ctx.Reset();
            frames.push_back(ReadOutput(ctx, f.output.Get())); ctx.Reset();
            std::cout << "frame=" << frame << " finite=" << frames.back().finite << " nonfinite=" << frames.back().nonfinite
                      << " rgb_min=" << frames.back().rgb_minimum << " rgb_max=" << frames.back().rgb_maximum
                      << " rgb_first=" << frames.back().rgb_first << " rgb_last=" << frames.back().rgb_last
                      << " hash=" << frames.back().hash << '\n';
        }
        if (frames.back().nonfinite || frames.back().finite != size_t(kOutputWidth) * kOutputHeight * 4)
            throw std::runtime_error("output contains non-finite values");
        if (!(frames.back().rgb_maximum - frames.back().rgb_minimum > 0.05f) ||
            std::abs(frames.back().rgb_first - frames.back().rgb_last) < 0.01f)
            throw std::runtime_error("RGB output lacks spatial variation");
        std::cout << "repeat_hash_equal=" << (frames[1].hash == frames[2].hash ? "true" : "false")
                  << " R=640x360 O=1280x720 game_verified=false NR=false FG=false\n";
        result = NVSDK_NGX_D3D12_ReleaseFeature(feature); feature = nullptr;
        std::cout << "ngx_release=0x" << std::hex << static_cast<unsigned>(result) << std::dec << '\n';
        if (NVSDK_NGX_FAILED(result)) throw std::runtime_error("NGX ReleaseFeature failed");
        result = NVSDK_NGX_D3D12_DestroyParameters(params); params = nullptr;
        std::cout << "ngx_destroy_parameters=0x" << std::hex << static_cast<unsigned>(result) << std::dec << '\n';
        if (NVSDK_NGX_FAILED(result)) throw std::runtime_error("NGX DestroyParameters failed");
        result = NVSDK_NGX_D3D12_DestroyParameters(caps); caps = nullptr;
        std::cout << "ngx_destroy_capabilities=0x" << std::hex << static_cast<unsigned>(result) << std::dec << '\n';
        if (NVSDK_NGX_FAILED(result)) throw std::runtime_error("NGX capability DestroyParameters failed");
        result = NVSDK_NGX_D3D12_Shutdown1(ctx.device.Get()); initialized = false;
        std::cout << "ngx_shutdown=0x" << std::hex << static_cast<unsigned>(result) << std::dec << '\n';
        if (NVSDK_NGX_FAILED(result)) throw std::runtime_error("NGX Shutdown failed");
    } catch (...) {
        // Every submitted command has passed its fence before resources are released here.
        if (feature) std::cerr << "cleanup_release=0x" << std::hex << static_cast<unsigned>(NVSDK_NGX_D3D12_ReleaseFeature(feature)) << std::dec << '\n';
        if (params) std::cerr << "cleanup_destroy=0x" << std::hex << static_cast<unsigned>(NVSDK_NGX_D3D12_DestroyParameters(params)) << std::dec << '\n';
        if (caps) std::cerr << "cleanup_destroy_capabilities=0x" << std::hex << static_cast<unsigned>(NVSDK_NGX_D3D12_DestroyParameters(caps)) << std::dec << '\n';
        if (initialized) std::cerr << "cleanup_shutdown=0x" << std::hex << static_cast<unsigned>(NVSDK_NGX_D3D12_Shutdown1(ctx.device.Get())) << std::dec << '\n';
        throw;
    }
    std::wcout << L"adapter=" << ctx.name << L" hardware=true SDK_commit=a291cc7d2cc642a51566f3dfd5376f635cd1b284\n";
}
} // namespace

int wmain(int argc, wchar_t **argv) {
    try {
        if (argc < 2) throw std::runtime_error("usage: direct_sr_gpu.exe --warp | --hardware <ngx-runtime-dir>");
        if (wcscmp(argv[1], L"--warp") == 0) { auto ctx = MakeDevice(true); RunMapCases(ctx); return 0; }
        if (wcscmp(argv[1], L"--hardware") == 0 && argc == 3) { RunHardware(argv[2]); return 0; }
        throw std::runtime_error("invalid arguments");
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << "\n"; return 1;
    }
}
