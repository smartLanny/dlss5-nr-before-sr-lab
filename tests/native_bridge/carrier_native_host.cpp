// This harness embeds the complete, unchanged generated carrier translation unit.
// The DLL exports and MinHook patches are real, the D3D11 device is WARP, and the
// ReShade device/swapchain wrappers provide controlled callback identities.
#include <thread>
#include <cstdlib>
// GENERATED_CARRIER_INCLUDE
// GENERATED_API_WRAPPERS

class CarrierParams : public NVSDK_NGX_Parameter {
public:
    unsigned flags = 37;
    bool known = true;
#define SET(T) void Set(const char *, T) override {}
    SET(unsigned long long) SET(float) SET(double) SET(int)
    SET(ID3D11Resource *) SET(ID3D12Resource *) SET(void *)
#undef SET
    void Set(const char *key, unsigned value) override {
        if (std::strcmp(key, "DLSS.Feature.Create.Flags") == 0) flags = value;
    }
#define GET(T) int Get(const char *, T *out) const override { *out = {}; return -1; }
    GET(unsigned long long) GET(float) GET(double)
    GET(ID3D11Resource *) GET(ID3D12Resource *) GET(void *)
#undef GET
    int Get(const char *key, int *out) const override {
        *out = 0; return std::strcmp(key, "Reset") == 0 ? 1 : -1;
    }
    int Get(const char *key, unsigned *out) const override {
        if (std::strcmp(key, "DLSS.Feature.Create.Flags") == 0) { *out = flags; return known ? 1 : -1; }
        if (std::strcmp(key, "Width") == 0 || std::strcmp(key, "Height") == 0 ||
            std::strcmp(key, "OutWidth") == 0 || std::strcmp(key, "OutHeight") == 0) { *out = 64; return 1; }
        *out = 0; return -1;
    }
    void Reset() override {}
};

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int wmain(int argc, wchar_t **argv) {
    CHECK(argc == 2);
    g_self = GetModuleHandleW(nullptr);
    InitializeCriticalSection(&g_log_cs);
    InitializeCriticalSection(&g_hook_cs);
    InitializeCriticalSection(&g_ngx_cs); g_ngx_cs_ready = true;
    InitializeCriticalSection(&g_bridge_cs);
    InitializeCriticalSection(&g_carrier_cs);
    CHECK(MH_Initialize() == MH_OK);
    StartWatchingForNgx();
    CHECK(g_carrier_notifications_ready.load());
    HMODULE native = LoadLibraryW(argv[1]);
    CHECK(native != nullptr);
    auto create = reinterpret_cast<PFN_Create>(GetProcAddress(native, "NVSDK_NGX_D3D11_CreateFeature"));
    auto eval = reinterpret_cast<PFN_Evaluate>(GetProcAddress(native, "NVSDK_NGX_D3D11_EvaluateFeature"));
    auto release = reinterpret_cast<PFN_Release>(GetProcAddress(native, "NVSDK_NGX_D3D11_ReleaseFeature"));
    auto count = reinterpret_cast<unsigned (*)(unsigned)>(GetProcAddress(native, "CarrierTestCount"));
    auto barrier = reinterpret_cast<void (*)(HANDLE, HANDLE, HANDLE)>(GetProcAddress(native, "CarrierTestBarrier"));
    CHECK(create && eval && release && count && barrier);
    TryInstallHooks(); CHECK(g_layer_count == 0);

    ID3D11Device *d1 = nullptr, *d2 = nullptr;
    ID3D11DeviceContext *ctx1 = nullptr, *ctx2 = nullptr;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                                    D3D11_SDK_VERSION, &d1, nullptr, &ctx1)));
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                                    D3D11_SDK_VERSION, &d2, nullptr, &ctx2)));
    CarrierTestDevice device1, device2, other_api;
    device1.native = reinterpret_cast<std::uintptr_t>(d1);
    device2.native = reinterpret_cast<std::uintptr_t>(d2);
    other_api.api = reshade::api::device_api::d3d12;
    OnCarrierInitDevice(&other_api); CHECK(g_layer_count == 0);
    OnCarrierInitDevice(&device1);
    CHECK(g_layer_count == 1 && g_layer[0].create.active && g_layer[0].release.active);
    CHECK(!g_layer[0].eval.active && !g_bridge.dev12);

    CarrierParams params;
    NVSDK_NGX_Handle *handle = nullptr;
    CHECK(create(ctx1, 1, &params, &handle) == 1 && count(0) == 1);
    CHECK(handle != nullptr && params.flags == 999);
    CHECK(!g_carrier_registry.Allows(reinterpret_cast<std::uintptr_t>(handle), device1.native));
    OnCarrierInitDevice(&device2);
    CarrierTestSwapchain swap1, swap2, extra;
    swap1.native = 0x100; swap1.owner = &device1;
    swap2.native = 0x200; swap2.owner = &device2;
    extra.native = 0x300; extra.owner = &device1;
    // A saturated slot ledger must still promote its previously installed pair.
    g_layer_count = kMaxLayers;
    OnCarrierInitSwapchain(&swap2, false);
    CHECK(g_layer[0].eval.active && !g_bridge.dev12);
    g_layer_count = 1;
    CHECK(eval(ctx1, handle, &params, nullptr) == 1 && count(1) == 1);
    CHECK(!g_bridge.dev12);
    OnCarrierInitSwapchain(&swap1, false);
    nr_carrier_activation::FeatureRecord record;
    CHECK(g_carrier_registry.Allows(reinterpret_cast<std::uintptr_t>(handle), device1.native, &record));
    CHECK(record.flags == 37 && record.flags_known);
    g_eval_handle = handle;
    unsigned saved = 0; CHECK(NrSavedCreateFlags(&saved) && saved == 37);
    for (unsigned resize = 0; resize < 4; ++resize) {
        OnCarrierDestroySwapchain(&swap1, true);
        CHECK(!g_carrier_registry.Allows(reinterpret_cast<std::uintptr_t>(handle), device1.native));
        OnCarrierInitSwapchain(&swap1, true);
        CHECK(g_carrier_registry.Allows(reinterpret_cast<std::uintptr_t>(handle), device1.native));
    }
    OnCarrierInitSwapchain(&extra, false);
    OnCarrierDestroySwapchain(&extra, false);
    CHECK(g_carrier_registry.Allows(reinterpret_cast<std::uintptr_t>(handle), device1.native));
    OnCarrierDestroySwapchain(&swap2, false);
    OnCarrierDestroyDevice(&device2);
    CHECK(g_carrier_registry.Allows(reinterpret_cast<std::uintptr_t>(handle), device1.native));

    HANDLE entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE proceed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE released = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE release_started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE destroyed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    CHECK(entered && proceed && released && release_started && destroyed);
    barrier(entered, proceed, released);
    int eval_result = 0, release_result = 0;
    std::thread evaluation([&] { eval_result = eval(ctx1, handle, &params, nullptr); });
    CHECK(WaitForSingleObject(entered, 2000) == WAIT_OBJECT_0);
    std::thread releasing([&] { SetEvent(release_started); release_result = release(handle); });
    CHECK(WaitForSingleObject(release_started, 2000) == WAIT_OBJECT_0);
    std::thread destroying([&] { OnCarrierDestroySwapchain(&swap1, false); SetEvent(destroyed); });
    CHECK(WaitForSingleObject(released, 50) == WAIT_TIMEOUT);
    CHECK(WaitForSingleObject(destroyed, 50) == WAIT_TIMEOUT);
    SetEvent(proceed);
    evaluation.join(); releasing.join(); destroying.join();
    CHECK(eval_result == 1 && release_result == 1 && count(0) == 1 && count(1) == 2 && count(2) == 1);
    CHECK(!g_carrier_registry.Allows(reinterpret_cast<std::uintptr_t>(handle), device1.native));
    CHECK(!g_bridge.dev12);
    barrier(nullptr, nullptr, nullptr);
    for (HANDLE event : {entered, proceed, released, release_started, destroyed}) CloseHandle(event);
    params.known = false;
    CHECK(create(ctx1, 1, &params, &handle) == 1 && count(0) == 2);
    OnCarrierInitSwapchain(&swap1, false);
    CHECK(!g_carrier_registry.Allows(reinterpret_cast<std::uintptr_t>(handle), device1.native));
    CHECK(release(handle) == 1 && count(2) == 2);
    OnCarrierDestroySwapchain(&swap1, false); OnCarrierDestroyDevice(&device1);

    // Real WARP queue/fence retirement, without creating any NVIDIA feature.
    IDXGIFactory4 *factory = nullptr;
    IDXGIAdapter *warp = nullptr;
    CHECK(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
    CHECK(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))));
    CHECK(SUCCEEDED(D3D12CreateDevice(warp, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_bridge.dev12))));
    warp->Release(); factory->Release();
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    CHECK(SUCCEEDED(g_bridge.dev12->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&g_bridge.queue))));
    CHECK(SUCCEEDED(g_bridge.dev12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_bridge.fence12))));
    CHECK(SUCCEEDED(g_bridge.dev12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_bridge.fence_progress))));
    CHECK(BridgeMakeDepthShader(d1) && BridgeMakeMVShader(d1));
    for (unsigned i = 0; i < 2; ++i) {
        CHECK(SUCCEEDED(g_bridge.dev12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                             IID_PPV_ARGS(&g_bridge.pq.alloc[i]))));
        CHECK(SUCCEEDED(g_bridge.dev12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
             g_bridge.pq.alloc[i], nullptr, IID_PPV_ARGS(&g_bridge.pq.list[i]))));
        CHECK(SUCCEEDED(g_bridge.pq.list[i]->Close()));
    }
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{};
    heap_desc.NumDescriptors = 8;
    heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    CHECK(SUCCEEDED(g_bridge.dev12->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&g_bridge.pq.heap))));
    CHECK(SUCCEEDED(g_bridge.dev12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_bridge.pq.fence))));
    HANDLE pq_event = g_bridge.pq.ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    CHECK(pq_event != nullptr);
    ID3D12Device *external_device = g_bridge.dev12;
    external_device->AddRef();
    g_carrier_bridge_owner = device1.native;
    InterlockedExchange(&g_probe_done, 1);
    {
        CarrierOperationGuard operation;
        CarrierRetireLocked(device2.native);
        CHECK(g_bridge.dev12 == external_device && g_probe_done == 1);
        CarrierRetireLocked(device1.native);
        CHECK(!g_bridge.dev12 && !g_bridge.queue && !g_bridge.fence12 && !g_bridge.fence_progress);
        CHECK(!g_nr_carrier_quarantined && g_carrier_bridge_owner == 0);
        CHECK(g_probe_done == 0); // The next eligible frame can bootstrap again.
        CHECK(!g_bridge.depth_cs && !g_bridge.mv_cs && !g_bridge.pq.heap && !g_bridge.pq.fence && !g_bridge.pq.ev);
        CHECK(!g_bridge.pq.list[0] && !g_bridge.pq.list[1] && !g_bridge.pq.alloc[0] && !g_bridge.pq.alloc[1]);
    }
    DWORD event_flags = 0;
    CHECK(!GetHandleInformation(pq_event, &event_flags));
    CHECK(BridgeMakeDepthShader(d2) && BridgeMakeMVShader(d2));
    ID3D11Device *shader_owner = nullptr;
    g_bridge.depth_cs->GetDevice(&shader_owner); CHECK(shader_owner == d2); shader_owner->Release();
    g_bridge.mv_cs->GetDevice(&shader_owner); CHECK(shader_owner == d2); shader_owner->Release();
    CHECK(BridgeReleaseSession() && !g_bridge.depth_cs && !g_bridge.mv_cs);
    CHECK(SUCCEEDED(external_device->GetDeviceRemovedReason()));
    // Missing completion proof keeps the owner, bootstrap latch and resources.
    g_bridge.dev12 = external_device;
    CHECK(SUCCEEDED(g_bridge.dev12->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&g_bridge.queue))));
    g_carrier_bridge_owner = device1.native;
    InterlockedExchange(&g_probe_done, 1);
    {
        CarrierOperationGuard operation;
        CarrierRetireLocked(device1.native);
        CHECK(g_nr_carrier_quarantined && g_bridge.dev12 == external_device && g_bridge.queue);
        CHECK(g_carrier_bridge_owner == device1.native && g_probe_done == 1);
    }
    ctx1->Release(); ctx2->Release(); d1->Release(); d2->Release();
    CHECK(FreeLibrary(native));
    CHECK(GetModuleHandleW(L"carrier-native-fixture.dll") == native);
    CHECK(!HookUninstall(g_layer[0].create));
    std::puts("PASS: real DLL/MinHook Create/Evaluate/Release exactly once; WARP D3D11 devices;");
    std::puts("controlled ReShade identities, resize/multiswapchain, unknown flags and concurrent release/destroy.");
    std::puts("Real WARP D3D12 queue/fence retirement releases owned resources and rearms; missing proof quarantines.");
    std::puts("No NVIDIA model, private bridge frame, real ReShade dispatch, or game claim.");
    return 0;
}
