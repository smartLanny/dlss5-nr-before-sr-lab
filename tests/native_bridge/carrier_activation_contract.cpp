#include "tests/native_bridge/carrier_activation_contract.h"

#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace carrier_activation_contract;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (false)

int main(int argc, char **argv) {
    Activation a;
    constexpr std::uintptr_t auxiliary = 0x11, game = 0x22;
    constexpr std::uintptr_t early = 0x101, missed = 0x102;
    unsigned native_calls = 0;

    CHECK(!a.ObserverArmed() && a.PrivateD3d12Creates() == 0);
    a.InitDevice(Api::d3d12, 0x12);
    a.InitDevice(Api::vulkan, 0x13);
    CHECK(!a.ObserverArmed());

    a.InitDevice(Api::d3d11, auxiliary);
    a.InitDevice(Api::d3d11, game);
    CHECK(a.ObserverArmed());
    CHECK(a.ObserveCreate(game, early, 37, [&] { ++native_calls; return 1u; }) == 1);
    CHECK(native_calls == 1 && a.PrivateD3d12Creates() == 0);
    CHECK(!a.AllowsBridge(early));

    // A swapchain on another API or the auxiliary device does not activate the
    // game handle. Only a D3D11 chain on the same device does.
    a.InitSwapchain(Api::d3d12, game);
    CHECK(!a.AllowsBridge(early));
    a.InitSwapchain(Api::d3d11, auxiliary);
    CHECK(!a.AllowsBridge(early));
    a.InitSwapchain(Api::d3d11, game);
    CHECK(a.AllowsBridge(early));
    a.NotePrivateD3d12Create(early);
    CHECK(a.PrivateD3d12Creates() == 1);

    // A Create missed before the observation hook existed has no record. The
    // confirmed swapchain cannot manufacture its handle or flags.
    CHECK(!a.AllowsBridge(missed));
    a.ObserveRelease(early);
    CHECK(!a.AllowsBridge(early));
    a.DestroySwapchain(game);
    a.DestroyDevice(game);

    LoaderLedger ledger;
    ledger.Record(0x1000, true);
    ledger.Record(0x2000, true);
    ledger.Record(0x1000, false);
    unsigned visits = 0;
    std::uintptr_t visited_base = 0;
    ledger.Drain([](std::uintptr_t base) { return base == 0x2000; },
                 [&](const ModuleEvent &event) { ++visits; visited_base = event.base; });
    CHECK(visits == 1 && visited_base == 0x2000); // stale/unloaded 0x1000 is never acted on
    for (unsigned i = 0; i != LoaderLedger::capacity + 1; ++i)
        ledger.Record(0x3000 + i, true);
    CHECK(ledger.NeedsRescan());
    ledger.BeginFullRescan();
    CHECK(!ledger.NeedsRescan() && ledger.Epoch() == 8);

#ifdef _WIN32
    if (argc == 2) {
        HMODULE stub = LoadLibraryA(argv[1]);
        CHECK(stub != nullptr);
        using Create = unsigned (__cdecl *)(void *, void *, void *);
        using Count = unsigned (__cdecl *)();
        auto create = reinterpret_cast<Create>(GetProcAddress(stub, "NVSDK_NGX_D3D11_CreateFeature"));
        auto count = reinterpret_cast<Count>(GetProcAddress(stub, "NativeCreateCount"));
        CHECK(create != nullptr && count != nullptr && count() == 0);
        Activation real_dll_boundary;
        real_dll_boundary.InitDevice(Api::d3d11, game);
        const auto result = real_dll_boundary.ObserveCreate(game, early, 9,
            [&] { return create(nullptr, nullptr, nullptr); });
        CHECK(result == 1 && count() == 1);
        CHECK(real_dll_boundary.PrivateD3d12Creates() == 0);
        FreeLibrary(stub);
    }
#else
    (void)argc; (void)argv;
#endif
    std::puts("carrier two-stage activation contract: passed (prototype only, no GPU)");
}
