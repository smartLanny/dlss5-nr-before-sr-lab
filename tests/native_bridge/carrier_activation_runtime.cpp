#include "src/interop/nr_carrier_activation_runtime.h"

#include <cstdio>

using namespace nr_carrier_activation;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (false)

int main() {
    Registry registry;
    constexpr std::uintptr_t auxiliary = 0x11, game = 0x22;
    constexpr std::uintptr_t first = 0x101, reused = 0x102;
    constexpr std::uintptr_t auxiliary_swapchain = 0x501, game_swapchain = 0x502;
    CHECK(registry.AddD3d11Device(auxiliary));
    CHECK(registry.AddD3d11Device(game));
    CHECK(registry.Created(first, game, 37, true) != 0);
    CHECK(!registry.Allows(first, game));
    CHECK(registry.AddSwapchain(auxiliary_swapchain, auxiliary, false));
    CHECK(!registry.Allows(first, game));
    CHECK(registry.AddSwapchain(game_swapchain, game, false));
    FeatureRecord observed{};
    CHECK(registry.Allows(first, game, &observed) && observed.flags == 37 && observed.flags_known);
    CHECK(!registry.Allows(first, auxiliary));
    CHECK(registry.Created(reused, game, 0, false) != 0);
    CHECK(!registry.Allows(reused, game));
    CHECK(registry.Created(reused, game, 0, true) != 0);
    CHECK(registry.Allows(reused, game));
    const auto old = observed;
    CHECK(registry.Created(first, game, 41, true) != old.generation);
    CHECK(!registry.StillCurrent(old));
    CHECK(registry.Released(first));
    CHECK(!registry.Allows(first, game));
    // Resize destroy/init pairs preserve exactly one real swapchain identity.
    for (int i = 0; i != 4; ++i) {
        CHECK(!registry.RemoveSwapchain(game_swapchain, game, true));
        CHECK(!registry.Allows(reused, game));
        CHECK(registry.AddSwapchain(game_swapchain, game, true));
        CHECK(registry.Allows(reused, game));
    }
    CHECK(registry.RemoveSwapchain(game_swapchain, game, false));
    CHECK(!registry.Allows(reused, game));
    CHECK(registry.AddSwapchain(game_swapchain, game, false));
    CHECK(registry.RemoveDevice(auxiliary));
    CHECK(registry.Allows(reused, game));
    CHECK(registry.RemoveDevice(game));
    CHECK(!registry.Allows(reused, game));

    // Matching handles are replaced before a free-table hole is considered.
    Registry reuse_registry;
    CHECK(reuse_registry.AddD3d11Device(game));
    CHECK(reuse_registry.AddSwapchain(game_swapchain, game, false));
    constexpr std::uintptr_t handle_a = 0x201, handle_b = 0x202;
    CHECK(reuse_registry.Created(handle_a, game, 1, true) != 0);
    CHECK(reuse_registry.Created(handle_b, game, 2, true) != 0);
    CHECK(reuse_registry.Released(handle_a));
    const auto replacement_generation = reuse_registry.Created(handle_b, game, 3, true);
    CHECK(replacement_generation != 0);
    FeatureRecord replacement{};
    CHECK(reuse_registry.Allows(handle_b, game, &replacement));
    CHECK(replacement.generation == replacement_generation && replacement.flags == 3);
    CHECK(reuse_registry.Released(handle_b));
    CHECK(!reuse_registry.Allows(handle_b, game));

    ModuleLedger ledger;
    ledger.Record(0x1000, true);
    ledger.Record(0x1000, false);
    ledger.Record(0x1000, true);
    unsigned loads = 0, unloads = 0;
    std::uint64_t load_generation = 0;
    ledger.Drain([&](const ModuleEvent &event) {
        if (event.loaded) { ++loads; load_generation = event.generation; }
        else ++unloads;
    });
    CHECK(loads == 1 && unloads == 1 && load_generation == ledger.CurrentGeneration(0x1000));
    for (std::size_t i = 0; i != ModuleLedger::event_capacity + 1; ++i)
        ledger.Record(0x2000 + i, true);
    CHECK(ledger.NeedsRescan());
    ledger.BeginFullRescan();
    CHECK(!ledger.NeedsRescan());
    std::puts("carrier production activation runtime: passed (CPU, no GPU/game claim)");
}
