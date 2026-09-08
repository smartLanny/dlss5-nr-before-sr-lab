#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

// Test-only contract for issue #153's proposed two-stage carrier activation.
// This is deliberately not included by the carrier generator. It makes the
// admission and loader-ledger rules executable before production hooks change.
namespace carrier_activation_contract {

enum class Api : std::uint8_t { d3d11, d3d12, vulkan };

struct ModuleEvent {
    std::uintptr_t base = 0;
    std::uint64_t epoch = 0;
    bool loaded = false;
};

class LoaderLedger {
public:
    static constexpr std::size_t capacity = 4;

    void Record(std::uintptr_t base, bool loaded) noexcept {
        const auto epoch = epoch_.fetch_add(1, std::memory_order_relaxed) + 1;
        for (auto &slot : events_) {
            std::uint8_t empty = 0;
            if (slot.state.compare_exchange_strong(empty, 1, std::memory_order_acquire)) {
                slot.event = {base, epoch, loaded};
                slot.state.store(2, std::memory_order_release);
                return;
            }
        }
        needs_rescan_.store(true, std::memory_order_release);
    }

    template <typename IsCurrentGeneration, typename Visit>
    void Drain(IsCurrentGeneration is_current_generation, Visit visit) noexcept {
        for (auto &slot : events_) {
            std::uint8_t ready = 2;
            if (!slot.state.compare_exchange_strong(ready, 1, std::memory_order_acquire))
                continue;
            const ModuleEvent event = slot.event;
            // An address-only loaded test is insufficient: A may unload and B
            // may reuse its base. Validate this event's generation/identity
            // against the safe caller's current module snapshot.
            if (event.loaded && is_current_generation(event)) visit(event);
            slot.event = {};
            slot.state.store(0, std::memory_order_release);
        }
    }

    bool NeedsRescan() const noexcept { return needs_rescan_.load(std::memory_order_acquire); }
    std::uint64_t Epoch() const noexcept { return epoch_.load(std::memory_order_acquire); }
    // Clear before taking the safe module snapshot. An overflow racing the
    // snapshot sets the bit again and therefore cannot be erased afterward.
    void BeginFullRescan() noexcept { needs_rescan_.exchange(false, std::memory_order_acq_rel); }

private:
    struct Slot {
        // 0 free, 1 owned by a writer/reader, 2 ready for the safe pump.
        std::atomic<std::uint8_t> state{0};
        ModuleEvent event{};
    };
    std::array<Slot, capacity> events_{};
    std::atomic<std::uint64_t> epoch_{0};
    std::atomic<bool> needs_rescan_{false};
};

struct CreateRecord {
    std::uintptr_t handle = 0;
    std::uintptr_t device = 0;
    std::uint32_t flags = 0;
};

class Activation {
public:
    static constexpr std::size_t device_capacity = 4;
    static constexpr std::size_t create_capacity = 12;

    void InitDevice(Api api, std::uintptr_t device) noexcept {
        if (api != Api::d3d11 || device == 0) return;
        for (const auto candidate : d3d11_devices_)
            if (candidate == device) return;
        for (auto &candidate : d3d11_devices_)
            if (candidate == 0) {
                candidate = device;
                observer_armed_ = true;
                return;
            }
        device_overflow_ = true;
    }

    void DestroyDevice(std::uintptr_t device) noexcept {
        for (auto &candidate : d3d11_devices_)
            if (candidate == device) candidate = 0;
        if (confirmed_device_ == device) confirmed_device_ = 0;
        for (auto &record : creates_)
            if (record.device == device) record = {};
    }

    void InitSwapchain(Api api, std::uintptr_t device) noexcept {
        if (api != Api::d3d11 || !IsCandidate(device)) return;
        confirmed_device_ = device;
    }

    void DestroySwapchain(std::uintptr_t device) noexcept {
        if (confirmed_device_ == device) confirmed_device_ = 0;
    }

    template <typename NativeCreate>
    std::uint32_t ObserveCreate(std::uintptr_t device, std::uint32_t flags,
                                NativeCreate native) noexcept {
        // Observation never substitutes for NGX: native Create is called once,
        // and the handle is read from its real output only after it succeeds.
        std::uintptr_t handle = 0;
        const std::uint32_t result = native(handle);
        if (result != 1 || !observer_armed_ || !IsCandidate(device) || handle == 0)
            return result;
        for (auto &record : creates_)
            if (record.handle == handle || record.handle == 0) {
                record = {handle, device, flags};
                return result;
            }
        create_overflow_ = true;
        return result;
    }

    void ObserveRelease(std::uintptr_t handle) noexcept {
        for (auto &record : creates_)
            if (record.handle == handle) record = {};
    }

    bool AllowsBridge(std::uintptr_t handle) const noexcept {
        if (confirmed_device_ == 0) return false;
        for (const auto &record : creates_)
            if (record.handle == handle && record.device == confirmed_device_) return true;
        return false; // Late/missed Create stays explicitly incomplete.
    }

    bool ObserverArmed() const noexcept { return observer_armed_; }
    bool DeviceOverflow() const noexcept { return device_overflow_; }
    bool CreateOverflow() const noexcept { return create_overflow_; }
    unsigned PrivateD3d12Creates() const noexcept { return private_d3d12_creates_; }
    void NotePrivateD3d12Create(std::uintptr_t handle) noexcept {
        if (AllowsBridge(handle)) ++private_d3d12_creates_;
    }

private:
    bool IsCandidate(std::uintptr_t device) const noexcept {
        for (const auto candidate : d3d11_devices_)
            if (candidate == device) return true;
        return false;
    }

    std::array<std::uintptr_t, device_capacity> d3d11_devices_{};
    std::array<CreateRecord, create_capacity> creates_{};
    std::uintptr_t confirmed_device_ = 0;
    unsigned private_d3d12_creates_ = 0;
    bool observer_armed_ = false;
    bool device_overflow_ = false;
    bool create_overflow_ = false;
};

} // namespace carrier_activation_contract
