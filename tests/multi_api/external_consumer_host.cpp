// Test-only registration host. It never renders an overlay, presents a game,
// supplies a fake NGX function, or changes the production DLL's DllMain.
// Reuse the existing real WARP allocation/upload/readback fixture helpers.
#define main unused_transport_test_main
#include "external_provider_warp.cpp"
#undef main
#include <string>
#if defined(NR_HOST_NGX_BOOTSTRAP)
#include <nvsdk_ngx.h>
#endif
#include "src/interop/nr_external_provider_producer.h"
#include "src/interop/nr_external_provider_completion.h"

extern "C" __declspec(dllexport) bool ReShadeRegisterAddon(void *, unsigned version) {
    return version == 18;
}
extern "C" __declspec(dllexport) void ReShadeUnregisterAddon(void *) {}
extern "C" __declspec(dllexport) void ReShadeLogMessage(void *, int, const char *) {}
extern "C" __declspec(dllexport) const void *ReShadeGetImGuiFunctionTable(unsigned) {
    // Non-null registration token only. No UI callback or table entry is called.
    static const void *unused_table[1024]{};
    return unused_table;
}

int main(int argc, char **argv) {
    if (argc != 3) Fail("usage: host <production-core-dll> <off|on>");
    const bool hardware = std::string(argv[2]) == "hardware";
    const bool enabled = hardware || std::string(argv[2]) == "on";
    HMODULE core = LoadLibraryA(argv[1]);
    if (!core) { std::printf("LoadLibrary error=%lu\n", GetLastError()); Fail("production DllMain"); }
    auto query = reinterpret_cast<nr_external_provider_client::QueryFn>(GetProcAddress(core,"NRExternalProvider_QueryV1"));
    auto claim = reinterpret_cast<nr_external_provider_client::ClaimFn>(GetProcAddress(core,"NRExternalProvider_ClaimOwnershipV1"));
    auto release = reinterpret_cast<nr_external_provider_client::ReleaseFn>(GetProcAddress(core,"NRExternalProvider_ReleaseOwnershipV1"));
    auto submit = reinterpret_cast<nr_external_provider_client::SubmitFn>(GetProcAddress(core,"NRExternalProvider_SubmitFrameV1"));
    if (!query || !claim || !release || !submit) Fail("missing actual consumer export");
    NrExternalStatusV1 status{};
    for (unsigned i = 0; i < 20; ++i) {
        query(kNrExternalProviderAbi, sizeof(status), 1, &status);
        if (status.reason != NrExternalProviderReason::ObserverUncertain) break;
        Sleep(25);
    }
    std::printf("actual consumer query=%s epoch=%llu\n",NrExternalProviderReasonName(status.reason), status.observer_epoch);
    if (!claim(kNrExternalProviderAbi,sizeof(status),kNrExternalProviderIdDl5f,&status)) Fail("actual claim");
    if (enabled != (status.reason == NrExternalProviderReason::Ready)) Fail("fixture config did not load");

    IDXGIFactory4 *factory=nullptr; IDXGIAdapter *adapter=nullptr; ID3D12Device *device=nullptr;
    ID3D12CommandQueue *queue=nullptr; ID3D12CommandAllocator *allocator=nullptr;
    ID3D12GraphicsCommandList *list=nullptr; ID3D12Fence *fence=nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) Fail("factory");
    if (hardware) {
        IDXGIAdapter1 *candidate=nullptr;
        for (UINT i=0; factory->EnumAdapters1(i,&candidate)!=DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 description{}; candidate->GetDesc1(&description);
            if (description.VendorId==0x10de && !(description.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)) {
                adapter=candidate; std::printf("adapter=NVIDIA vendor=%04X device=%04X\n",description.VendorId,description.DeviceId); break;
            }
            candidate->Release();
        }
    } else if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)))) Fail("WARP adapter");
    if (!adapter || FAILED(D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)))) Fail("device");
#if defined(NR_HOST_NGX_BOOTSTRAP)
    if (hardware) {
        const auto init=NVSDK_NGX_D3D12_Init(0x1000000ULL,L".",device,nullptr,NVSDK_NGX_Version_API);
        std::printf("official NGX bootstrap=0x%08X coreLoaded=%d\n",init,GetModuleHandleW(L"_nvngx.dll")!=nullptr);
        if (NVSDK_NGX_FAILED(init)) Fail("actual official NGX bootstrap failed");
    }
#endif
    D3D12_COMMAND_QUEUE_DESC q{};
    if (FAILED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))) ||
        FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))) ||
        FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator,nullptr,IID_PPV_ARGS(&list))) ||
        FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)))) Fail("WARP commands");
    const UINT w=hardware?640:8,h=hardware?360:4;
    auto color=CreateTex(device,w,h,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    auto output=CreateTex(device,w,h,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
    auto depth=CreateTex(device,w,h,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    auto motion=CreateTex(device,w,h,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
    std::vector<std::uint32_t> marker(w*h,0xff194371u); ID3D12Resource *upload=nullptr;
    FillUploadAndCopy(device,list,output,w,h,marker,&upload);
    ID3D12Resource *color_upload=nullptr,*depth_upload=nullptr,*motion_upload=nullptr;
    std::vector<std::uint32_t> source(w*h);
    for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x)source[y*w+x]=0xff000000u|((x*255/w)<<16)|((y*255/h)<<8)|64u;
    FillUploadAndCopy(device,list,color,w,h,source,&color_upload);
    FillUploadAndCopy(device,list,depth,w,h,std::vector<std::uint32_t>(w*h,0x3f000000u),&depth_upload,DXGI_FORMAT_R32_FLOAT);
    FillUploadAndCopy(device,list,motion,w,h,std::vector<std::uint32_t>(w*h,0),&motion_upload,DXGI_FORMAT_R16G16_FLOAT);
    for(auto *guide:{depth,motion}){
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource=guide;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        list->ResourceBarrier(1,&barrier);
    }
    NrExternalFrameV1 frame{};
    frame.provider_id=kNrExternalProviderIdDl5f; frame.view_id=1; frame.epoch=1; frame.frame_id=1;
    frame.command_list=reinterpret_cast<std::uint64_t>(list); frame.queue=reinterpret_cast<std::uint64_t>(queue);
    frame.fence=reinterpret_cast<std::uint64_t>(fence);
    frame.lease_flags=static_cast<std::uint32_t>(NrExternalLeaseFlags::CommandListOwnedByProducer) |
                      static_cast<std::uint32_t>(NrExternalLeaseFlags::QueueFenceRequired);
    auto resource=[&](ID3D12Resource *p, DXGI_FORMAT format,D3D12_RESOURCE_STATES state){
        NrExternalResourceV1 r{}; r.resource=reinterpret_cast<std::uint64_t>(p);
        r.width=r.rect_w=w;r.height=r.rect_h=h;r.format=format;
        r.state_before=r.state_after=state;r.provenance=NrGuideProvenance::Synthetic;return r;
    };
    frame.color=resource(color,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_COPY_DEST);
    frame.output=resource(output,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_COPY_DEST);
    frame.depth=resource(depth,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    frame.motion=resource(motion,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    NrExternalSubmitResultV1 result{};
    unsigned recordings=0,changed_recordings=0;
    for(unsigned attempt=0;attempt<(hardware?8u:1u);++attempt){
    frame.frame_id=attempt+1;
    const int accepted=submit(kNrExternalProviderAbi,&frame,&result);
    std::printf("actual Submit accepted=%d reason=%s recorded=%u frame=%llu\n",accepted,
        NrExternalProviderReasonName(result.reason),result.writeback_performed,result.admitted_frame_id);
    if (!hardware && result.writeback_performed) Fail("WARP fixture must not claim actual NR model execution");
    if (!enabled && (!accepted || result.reason!=NrExternalProviderReason::Disabled)) Fail("disabled identity contract");
    if (!hardware && enabled && (accepted || result.reason!=NrExternalProviderReason::RuntimeUnavailable)) Fail("unavailable-runtime contract");
    const auto pixels=Readback(device,queue,list,allocator,output,w,h);
    if (!hardware && pixels!=marker) Fail("declined consumer changed this frame output");
    if(hardware){
        unsigned changed=0;for(unsigned i=0;i<pixels.size();++i)changed+=pixels[i]!=source[i];
        recordings+=result.writeback_performed!=0;
        changed_recordings+=(result.writeback_performed!=0 && changed!=0);
        std::uint32_t hash=2166136261u;for(auto pixel:pixels)hash=(hash^pixel)*16777619u;
        std::printf("NVIDIA completion/readback attempt=%u recorded=%u pixelsDifferentFromInput=%u outputHash=%08X\n",attempt+1,result.writeback_performed,changed,hash);
        if(result.reason==NrExternalProviderReason::RuntimeUnavailable)break;
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource=output;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
        list->ResourceBarrier(1,&barrier);
    }
    }
    if (!release(kNrExternalProviderAbi,kNrExternalProviderIdDl5f)) Fail("actual release");
    query(kNrExternalProviderAbi,sizeof(status),0,&status);
    if(status.owner_provider_id) Fail("ownership not released");
    if(hardware)std::printf("NVIDIA experiment complete: recordedFrames=%u; ReShade registration stub, artificial guides, no game claim\n",recordings);
    else std::puts("PASS actual DLL exports + real WARP retained pixels; ReShade registration stub only; no NGX model/game claim");
    motion_upload->Release();depth_upload->Release();color_upload->Release();upload->Release(); motion->Release(); depth->Release(); output->Release(); color->Release();
    fence->Release(); list->Release(); allocator->Release();queue->Release();device->Release();adapter->Release();factory->Release();
    // The DLL's worker is process scoped; do not simulate unsupported dynamic unload.
    return hardware && (!recordings || !changed_recordings) ? 2 : 0;
}
