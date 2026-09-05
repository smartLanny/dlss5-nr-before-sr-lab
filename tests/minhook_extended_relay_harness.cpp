#include <windows.h>

#include <cstdint>
#include <cstring>
#include <cstdio>

#include "MinHook.h"

namespace {

constexpr std::uintptr_t kCandidateBases[] = {
    0x0000000200000000ull,
    0x0000000300000000ull,
    0x0000000400000000ull,
};
constexpr SIZE_T kOldRelayRange = 0x40000000ull;
constexpr SIZE_T kAllocationGranularity = 0x10000ull;

int WINAPI DetourTarget() { return 11; }

struct Fixture {
    void *below = nullptr;
    void *target = nullptr;
    void *above = nullptr;
};

void DestroyFixture(Fixture &fixture) {
    if (fixture.above != nullptr) VirtualFree(fixture.above, 0, MEM_RELEASE);
    if (fixture.target != nullptr) VirtualFree(fixture.target, 0, MEM_RELEASE);
    if (fixture.below != nullptr) VirtualFree(fixture.below, 0, MEM_RELEASE);
    fixture = {};
}

bool CreateFixture(Fixture &fixture) {
    for (const std::uintptr_t base_value : kCandidateBases) {
        auto *const base = reinterpret_cast<unsigned char *>(base_value);
        fixture.target = VirtualAlloc(base, kAllocationGranularity,
            MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (fixture.target != base) {
            DestroyFixture(fixture);
            continue;
        }
        fixture.below = VirtualAlloc(base - kOldRelayRange,
            kOldRelayRange, MEM_RESERVE, PAGE_NOACCESS);
        fixture.above = VirtualAlloc(base + kAllocationGranularity,
            kOldRelayRange, MEM_RESERVE, PAGE_NOACCESS);
        if (fixture.below == base - kOldRelayRange &&
            fixture.above == base + kAllocationGranularity) {
            // Use a real RIP-relative load in the copied prologue. The data is
            // near the target, so the extended relay remains representable.
            const unsigned char body[] = {
                0x8B, 0x05, 0xFA, 0x00, 0x00, 0x00, // mov eax,[rip+0xfa]
                0xC3,                               // ret
            };
            std::memcpy(fixture.target, body, sizeof(body));
            const std::uint32_t value = 7;
            std::memcpy(static_cast<unsigned char *>(fixture.target) + 0x100,
                        &value, sizeof(value));
            FlushInstructionCache(GetCurrentProcess(), fixture.target,
                                  sizeof(body));
            return true;
        }
        DestroyFixture(fixture);
    }
    return false;
}

} // namespace

int main() {
    Fixture fixture{};
    if (!CreateFixture(fixture)) {
        std::puts("SKIP: could not reserve the deterministic low-address fixture");
        return 77;
    }

    const MH_STATUS initialized = MH_Initialize();
    if (initialized != MH_OK) {
        std::printf("FAIL: MH_Initialize=%d\n", static_cast<int>(initialized));
        DestroyFixture(fixture);
        return 2;
    }

    using TargetFn = int (WINAPI *)();
    TargetFn original = nullptr;
    const MH_STATUS created = MH_CreateHook(
        fixture.target, reinterpret_cast<void *>(&DetourTarget),
        reinterpret_cast<void **>(&original));
    if (created != MH_OK) {
        std::printf("FAIL: MH_CreateHook=%d (%s); no relay allocation beyond the old 1 GiB window\n",
                    static_cast<int>(created), MH_StatusToString(created));
        MH_Uninitialize();
        DestroyFixture(fixture);
        return 3;
    }

    const MH_STATUS enabled = MH_EnableHook(fixture.target);
    auto target = reinterpret_cast<TargetFn>(fixture.target);
    const int hooked = enabled == MH_OK ? target() : -1;
    const int trampoline = original != nullptr ? original() : -1;
    bool okay = enabled == MH_OK && hooked == 11 && trampoline == 7;
    std::printf("extended RIP-relative relay: create=%d enable=%d hooked=%d original=%d %s\n",
                static_cast<int>(created), static_cast<int>(enabled), hooked,
                trampoline, okay ? "PASS" : "FAIL");

    const MH_STATUS removed = MH_RemoveHook(fixture.target);
    // This instruction's original RIP-relative destination is almost 2 GiB
    // above the target. With the deterministic relay more than 1 GiB below,
    // relocation exceeds signed rel32 and must be rejected rather than
    // silently truncated.
    const unsigned char unsafe_body[] = {
        0x8B, 0x05, 0xFF, 0xFF, 0xFF, 0x7F,
        0xC3,
    };
    std::memcpy(fixture.target, unsafe_body, sizeof(unsafe_body));
    FlushInstructionCache(GetCurrentProcess(), fixture.target,
                          sizeof(unsafe_body));
    TargetFn unsafe_original = nullptr;
    const MH_STATUS unsafe_created = MH_CreateHook(
        fixture.target, reinterpret_cast<void *>(&DetourTarget),
        reinterpret_cast<void **>(&unsafe_original));
    const bool overflow_rejected = unsafe_created == MH_ERROR_UNSUPPORTED_FUNCTION;
    std::printf("extended RIP-relative overflow: remove=%d create=%d (%s) %s\n",
                static_cast<int>(removed), static_cast<int>(unsafe_created),
                MH_StatusToString(unsafe_created),
                overflow_rejected ? "PASS" : "FAIL");
    okay = okay && removed == MH_OK && overflow_rejected;

    MH_Uninitialize();
    DestroyFixture(fixture);
    return okay ? 0 : 4;
}
