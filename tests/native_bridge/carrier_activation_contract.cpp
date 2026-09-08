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
    CHECK(a.ObserveCreate(game, 37, [&](std::uintptr_t &out) {
        ++native_calls; out = early; return 1u;
    }) == 1);
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
    CHECK(a.ObserveCreate(game, 41, [&](std::uintptr_t &out) {
        ++native_calls; out = missed; return 0xBAD00001u;
    }) == 0xBAD00001u);
    CHECK(!a.AllowsBridge(missed));
    a.ObserveRelease(early);
    CHECK(!a.AllowsBridge(early));
    a.DestroySwapchain(game);
    a.DestroyDevice(game);

    LoaderLedger ledger;
    ledger.Record(0x1000, true);
    ledger.Record(0x1000, false);
    ledger.Record(0x1000, true); // a different module reused the same base
    ledger.Record(0x2000, true);
    unsigned visits = 0;
    std::uint64_t visited_epoch_sum = 0;
    ledger.Drain([](const ModuleEvent &event) {
                     // The snapshot identifies generation 3 at reused base,
                     // rather than accepting old generation 1 by address.
                     return (event.base == 0x1000 && event.epoch == 3) ||
                            (event.base == 0x2000 && event.epoch == 4);
                 },
                 [&](const ModuleEvent &event) { ++visits; visited_epoch_sum += event.epoch; });
    CHECK(visits == 2 && visited_epoch_sum == 7);
    for (unsigned i = 0; i != LoaderLedger::capacity + 1; ++i)
        ledger.Record(0x3000 + i, true);
    CHECK(ledger.NeedsRescan());
    ledger.BeginFullRescan();
    CHECK(!ledger.NeedsRescan() && ledger.Epoch() == 9);

#ifdef _WIN32
    if (argc == 2) {
        HMODULE stub = LoadLibraryA(argv[1]);
        CHECK(stub != nullptr);
        using Create = unsigned (__cdecl *)(void *, int, void *, void **);
        using Count = unsigned (__cdecl *)();
        auto create = reinterpret_cast<Create>(GetProcAddress(stub, "NVSDK_NGX_D3D11_CreateFeature"));
        auto count = reinterpret_cast<Count>(GetProcAddress(stub, "NativeCreateCount"));
        CHECK(create != nullptr && count != nullptr && count() == 0);
        Activation real_dll_boundary;
        real_dll_boundary.InitDevice(Api::d3d11, game);
        std::uintptr_t actual_handle = 0;
        const auto result = real_dll_boundary.ObserveCreate(game, 9,
            [&](std::uintptr_t &out) {
                void *native_out = nullptr;
                const auto value = create(nullptr, 1, nullptr, &native_out);
                out = reinterpret_cast<std::uintptr_t>(native_out);
                actual_handle = out;
                return value;
            });
        CHECK(result == 1 && count() == 1 && actual_handle != early && actual_handle != 0);
        real_dll_boundary.InitSwapchain(Api::d3d11, game);
        CHECK(real_dll_boundary.AllowsBridge(actual_handle));
        CHECK(real_dll_boundary.PrivateD3d12Creates() == 0);
        FreeLibrary(stub);
    }
#else
    (void)argc; (void)argv;
#endif
    std::puts("carrier two-stage activation contract: passed (prototype only, no GPU)");
}
