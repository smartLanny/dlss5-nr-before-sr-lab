#pragma once
#include <cstdint>

// CPU handshake only. Never passes GPU pointers, adopts resources or clears a
// core guard. Exact version/size; x64 Windows WINAPI at the DLL boundary.
constexpr std::uint32_t kNrNativeBridgeAbi = 0x00010000u;
enum class NrNativeBridgeReason : std::uint32_t {
    Ready, Starting, ObserverUncertain, Disabled, CompetingNr, Busy
};
struct NrNativeBridgeStatusV1 {
    std::uint32_t size = sizeof(NrNativeBridgeStatusV1);
    std::uint32_t abi = kNrNativeBridgeAbi;
    NrNativeBridgeReason reason = NrNativeBridgeReason::Starting;
    std::uint32_t reserved = 0;
    std::uint64_t observerEpoch = 0;
};
static_assert(sizeof(NrNativeBridgeStatusV1) == 24);
inline bool NrNativeBridgeStatusAllows(const NrNativeBridgeStatusV1& s) noexcept {
    return s.size == sizeof(s) && s.abi == kNrNativeBridgeAbi && !s.reserved &&
        s.reason == NrNativeBridgeReason::Ready && !(s.observerEpoch & 1u);
}
