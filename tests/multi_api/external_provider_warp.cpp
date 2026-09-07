#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include "src/interop/nr_external_provider_d3d12.h"

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")

static void Fail(const char *msg) {
    std::fprintf(stderr, "external_provider_warp FAIL: %s\n", msg);
    std::exit(1);
}

static ID3D12Resource *CreateTex(ID3D12Device *device, UINT w, UINT h,
                                 DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags,
                                 D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = w;
    desc.Height = h;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Flags = flags;
    ID3D12Resource *resource = nullptr;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                               state, nullptr, IID_PPV_ARGS(&resource))))
        Fail("CreateCommittedResource");
    return resource;
}

static void FillUploadAndCopy(ID3D12Device *device, ID3D12GraphicsCommandList *list,
                              ID3D12Resource *texture, UINT w, UINT h,
                              const std::vector<std::uint32_t> &pixels,
                              ID3D12Resource **upload_out) {
    const UINT row_pitch = (w * 4 + 255u) & ~255u;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = row_pitch * h;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource *upload = nullptr;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ,
                                               nullptr, IID_PPV_ARGS(&upload))))
        Fail("upload create");
    std::uint8_t *mapped = nullptr;
    if (FAILED(upload->Map(0, nullptr, reinterpret_cast<void **>(&mapped))))
        Fail("upload map");
    for (UINT y = 0; y < h; ++y) {
        auto *row = reinterpret_cast<std::uint32_t *>(mapped + y * row_pitch);
        for (UINT x = 0; x < w; ++x) row[x] = pixels[y * w + x];
    }
    upload->Unmap(0, nullptr);
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = texture;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = upload;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    src.PlacedFootprint.Footprint.Width = w;
    src.PlacedFootprint.Footprint.Height = h;
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = row_pitch;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    *upload_out = upload;
}

static std::vector<std::uint32_t> Readback(ID3D12Device *device,
                                           ID3D12CommandQueue *queue,
                                           ID3D12GraphicsCommandList *list,
                                           ID3D12CommandAllocator *allocator,
                                           ID3D12Resource *texture, UINT w, UINT h) {
    const UINT row_pitch = (w * 4 + 255u) & ~255u;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = row_pitch * h;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource *readback = nullptr;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_COPY_DEST,
                                               nullptr, IID_PPV_ARGS(&readback))))
        Fail("readback create");

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = texture;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = readback;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    dst.PlacedFootprint.Footprint.Width = w;
    dst.PlacedFootprint.Footprint.Height = h;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = row_pitch;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    if (FAILED(list->Close())) Fail("close readback list");
    ID3D12CommandList *lists[] = {list};
    queue->ExecuteCommandLists(1, lists);
    ID3D12Fence *fence = nullptr;
    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
        Fail("fence");
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    queue->Signal(fence, 1);
    fence->SetEventOnCompletion(1, event);
    WaitForSingleObject(event, INFINITE);
    CloseHandle(event);

    std::uint8_t *mapped = nullptr;
    if (FAILED(readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped))))
        Fail("readback map");
    std::vector<std::uint32_t> pixels(w * h);
    for (UINT y = 0; y < h; ++y) {
        auto *row = reinterpret_cast<const std::uint32_t *>(mapped + y * row_pitch);
        for (UINT x = 0; x < w; ++x) pixels[y * w + x] = row[x];
    }
    readback->Unmap(0, nullptr);
    readback->Release();
    fence->Release();
    if (FAILED(allocator->Reset()) || FAILED(list->Reset(allocator, nullptr)))
        Fail("reset after readback");
    return pixels;
}

int main() {
    IDXGIFactory4 *factory = nullptr;
    IDXGIAdapter *adapter = nullptr;
    ID3D12Device *device = nullptr;
    ID3D12CommandQueue *queue = nullptr;
    ID3D12CommandAllocator *allocator = nullptr;
    ID3D12GraphicsCommandList *list = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
        FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))) ||
        FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
        Fail("WARP device");
    D3D12_COMMAND_QUEUE_DESC qdesc{};
    qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&queue))) ||
        FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(&allocator))) ||
        FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator,
                                         nullptr, IID_PPV_ARGS(&list))))
        Fail("queue/list");

    constexpr UINT kW = 8, kH = 4;
    ID3D12Resource *color = CreateTex(device, kW, kH, DXGI_FORMAT_R8G8B8A8_UNORM,
                                      D3D12_RESOURCE_FLAG_NONE,
                                      D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Resource *output = CreateTex(device, kW, kH, DXGI_FORMAT_R8G8B8A8_UNORM,
                                       D3D12_RESOURCE_FLAG_NONE,
                                       D3D12_RESOURCE_STATE_COMMON);
    std::vector<std::uint32_t> source(kW * kH);
    for (UINT i = 0; i < source.size(); ++i)
        source[i] = 0xFF000000u | ((i * 17u) & 0xFFu) | (((i * 29u) & 0xFFu) << 8) |
                    (((i * 43u) & 0xFFu) << 16);
    ID3D12Resource *upload = nullptr;
    FillUploadAndCopy(device, list, color, kW, kH, source, &upload);
    D3D12_RESOURCE_BARRIER ready{};
    ready.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    ready.Transition.pResource = color;
    ready.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    ready.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    ready.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1, &ready);

    nr_external_provider::Session session;
    nr_external_provider::ConfigSnapshot cfg;
    cfg.master_enabled = true;
    cfg.transfer_strength = 1.0f;
    cfg.observer_epoch = 2;
    if (!nr_external_provider::ClaimOwnership(session, cfg, kNrExternalProviderIdDl5f))
        Fail("claim");

    NrExternalFrameV1 frame{};
    frame.provider_id = kNrExternalProviderIdDl5f;
    frame.view_id = 1;
    frame.epoch = 2;
    frame.frame_id = 1;
    frame.command_list = reinterpret_cast<std::uint64_t>(list);
    frame.color.resource = reinterpret_cast<std::uint64_t>(color);
    frame.color.width = kW;
    frame.color.height = kH;
    frame.color.rect_w = kW;
    frame.color.rect_h = kH;
    frame.color.format = static_cast<std::uint32_t>(DXGI_FORMAT_R8G8B8A8_UNORM);
    frame.color.state_before = D3D12_RESOURCE_STATE_COPY_SOURCE;
    frame.color.state_after = D3D12_RESOURCE_STATE_COPY_SOURCE;
    frame.color.provenance = NrGuideProvenance::Synthetic;
    frame.output.resource = reinterpret_cast<std::uint64_t>(output);
    frame.output.width = kW;
    frame.output.height = kH;
    frame.output.rect_w = kW;
    frame.output.rect_h = kH;
    frame.output.format = frame.color.format;
    frame.output.state_before = D3D12_RESOURCE_STATE_COMMON;
    frame.output.state_after = D3D12_RESOURCE_STATE_COPY_DEST;
    frame.output.provenance = NrGuideProvenance::Synthetic;
    frame.depth.provenance = NrGuideProvenance::Unknown;
    frame.motion.provenance = NrGuideProvenance::Unknown;

    auto processed = nr_external_provider_d3d12::ProcessSameFrame(session, cfg, frame);
    if (!processed.admit.admit || !processed.writeback)
        Fail("same-frame writeback not recorded");

    auto pixels = Readback(device, queue, list, allocator, output, kW, kH);
    if (pixels != source) Fail("writeback pixels mismatch (deterministic fake NR)");

    // F6 off: no GPU writeback work; prior output retained only if we do not run.
    cfg.master_enabled = false;
    frame.frame_id = 2;
    // Reset output marker by clearing through a new upload of zeros is unnecessary:
    // Disabled path must not schedule copies.
    auto off = nr_external_provider_d3d12::ProcessSameFrame(session, cfg, frame);
    if (!off.admit.admit || off.admit.run_nr || off.writeback)
        Fail("F6-off must admit identity without writeback work");

    upload->Release();
    color->Release();
    output->Release();
    list->Release();
    allocator->Release();
    queue->Release();
    device->Release();
    adapter->Release();
    factory->Release();
    std::puts("WARP MC1 external provider same-frame writeback passed "
              "(NR_TEST_MULTIAPI deterministic-copy double only; "
              "production uses core Feature18 hook, not this path)");
    return 0;
}
