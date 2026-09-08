#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace nr_carrier_activation {

struct ModuleEvent {
    std::uintptr_t base = 0;
    std::uint64_t generation = 0;
    bool loaded = false;
};

class ModuleLedger {
public:
    static constexpr std::size_t event_capacity = 64;
    static constexpr std::size_t module_capacity = 64;

    void Record(std::uintptr_t base, bool loaded) noexcept {
        if (base == 0) return;
        const auto generation = next_generation_.fetch_add(1, std::memory_order_relaxed) + 1;
        bool generation_recorded = false;
        for (auto &slot : modules_) {
            auto current = slot.base.load(std::memory_order_acquire);
            if (current == base || (current == 0 && slot.base.compare_exchange_strong(
                    current, base, std::memory_order_acq_rel))) {
                slot.generation.store(loaded ? generation : 0, std::memory_order_release);
                generation_recorded = true;
                break;
            }
        }
        for (auto &slot : events_) {
            std::uint8_t empty = 0;
            if (slot.state.compare_exchange_strong(empty, 1, std::memory_order_acq_rel)) {
                slot.event = {base, generation, loaded};
                slot.state.store(2, std::memory_order_release);
                if (!generation_recorded) needs_rescan_.store(true, std::memory_order_release);
                return;
            }
        }
        needs_rescan_.store(true, std::memory_order_release);
    }

    std::uint64_t CurrentGeneration(std::uintptr_t base) const noexcept {
        for (const auto &slot : modules_)
            if (slot.base.load(std::memory_order_acquire) == base)
                return slot.generation.load(std::memory_order_acquire);
        return 0;
    }

    template <typename Visit>
    void Drain(Visit visit) noexcept {
        for (auto &slot : events_) {
            std::uint8_t ready = 2;
            if (!slot.state.compare_exchange_strong(ready, 1, std::memory_order_acq_rel))
                continue;
            const ModuleEvent event = slot.event;
            if (!event.loaded || CurrentGeneration(event.base) == event.generation)
                visit(event);
            slot.event = {};
            slot.state.store(0, std::memory_order_release);
        }
    }

    bool NeedsRescan() const noexcept { return needs_rescan_.load(std::memory_order_acquire); }
    void BeginFullRescan() noexcept { needs_rescan_.exchange(false, std::memory_order_acq_rel); }
    // Snapshot before enumeration and verify after acquiring module references.
    // Unlike the bounded identity table, this also covers table overflow.
    std::uint64_t Epoch() const noexcept {
        return next_generation_.load(std::memory_order_acquire);
    }

private:
    struct EventSlot {
        std::atomic<std::uint8_t> state{0};
        ModuleEvent event{};
    };
    struct ModuleSlot {
        std::atomic<std::uintptr_t> base{0};
        std::atomic<std::uint64_t> generation{0};
    };
    static_assert(std::atomic<std::uintptr_t>::is_always_lock_free,
                  "loader notification storage must be lock-free");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "loader generation storage must be lock-free");
    std::array<EventSlot, event_capacity> events_{};
    std::array<ModuleSlot, module_capacity> modules_{};
    std::atomic<std::uint64_t> next_generation_{0};
    std::atomic<bool> needs_rescan_{false};
};

struct FeatureRecord {
    std::uintptr_t handle = 0;
    std::uintptr_t device = 0;
    std::uint64_t generation = 0;
    std::uint32_t flags = 0;
    bool flags_known = false;
};

class Registry {
public:
    static constexpr std::size_t device_capacity = 8;
    static constexpr std::size_t feature_capacity = 32;
    static constexpr std::size_t swapchain_capacity = 16;

    bool AddD3d11Device(std::uintptr_t device) noexcept {
        if (device == 0) return false;
        for (const auto &entry : devices_)
            if (entry.device == device) return true;
        for (auto &entry : devices_)
            if (entry.device == 0) { entry.device = device; return true; }
        device_overflow_ = true;
        return false;
    }

    bool AddSwapchain(std::uintptr_t swapchain, std::uintptr_t device, bool resize) noexcept {
        if (swapchain == 0 || !IsDevice(device)) return false;
        for (auto &entry : swapchains_)
            if (entry.swapchain == swapchain) {
                entry.device = device;
                entry.resizing = false;
                return true;
            }
        for (auto &entry : swapchains_)
            if (entry.swapchain == 0) {
                entry = {swapchain, device, false};
                return true;
            }
        (void)resize;
        swapchain_overflow_ = true;
        return false;
    }

    bool RemoveSwapchain(std::uintptr_t swapchain, std::uintptr_t device, bool resize) noexcept {
        for (auto &entry : swapchains_)
            if (entry.swapchain == swapchain && entry.device == device) {
                if (resize) {
                    entry.resizing = true;
                    return false;
                }
                entry = {};
                return !HasSwapchain(device);
            }
        return false;
    }

    bool RemoveDevice(std::uintptr_t device) noexcept {
        bool existed = false;
        for (auto &entry : devices_)
            if (entry.device == device) { entry = {}; existed = true; }
        for (auto &swapchain : swapchains_)
            if (swapchain.device == device) swapchain = {};
        for (auto &feature : features_)
            if (feature.device == device) feature = {};
        return existed;
    }

    std::uint64_t Created(std::uintptr_t handle, std::uintptr_t device,
                          std::uint32_t flags, bool flags_known) noexcept {
        if (handle == 0 || !IsDevice(device)) return 0;
        const auto generation = ++next_feature_generation_;
        for (auto &feature : features_)
            if (feature.handle == handle) {
                feature = {handle, device, generation, flags, flags_known};
                return generation;
            }
        for (auto &feature : features_)
            if (feature.handle == 0) {
                feature = {handle, device, generation, flags, flags_known};
                return generation;
            }
        feature_overflow_ = true;
        return 0;
    }

    bool Released(std::uintptr_t handle, std::uintptr_t *device = nullptr) noexcept {
        for (auto &feature : features_)
            if (feature.handle == handle) {
                if (device != nullptr) *device = feature.device;
                feature = {};
                return true;
            }
        return false;
    }

    bool Allows(std::uintptr_t handle, std::uintptr_t device,
                FeatureRecord *record = nullptr) const noexcept {
        if (!HasSwapchain(device)) return false;
        for (const auto &feature : features_)
            if (feature.handle == handle && feature.device == device && feature.flags_known) {
                if (record != nullptr) *record = feature;
                return true;
            }
        return false;
    }

    bool StillCurrent(const FeatureRecord &record) const noexcept {
        for (const auto &feature : features_)
            if (feature.handle == record.handle && feature.device == record.device &&
                feature.generation == record.generation) return true;
        return false;
    }

    bool HasLiveFeature(std::uintptr_t device) const noexcept {
        for (const auto &feature : features_)
            if (feature.handle != 0 && feature.device == device) return true;
        return false;
    }

    bool DeviceOverflow() const noexcept { return device_overflow_; }
    bool FeatureOverflow() const noexcept { return feature_overflow_; }
    bool SwapchainOverflow() const noexcept { return swapchain_overflow_; }

private:
    struct DeviceRecord { std::uintptr_t device = 0; };
    struct SwapchainRecord {
        std::uintptr_t swapchain = 0;
        std::uintptr_t device = 0;
        bool resizing = false;
    };
    bool IsDevice(std::uintptr_t device) const noexcept {
        for (const auto &entry : devices_) if (entry.device == device) return true;
        return false;
    }
    bool HasSwapchain(std::uintptr_t device) const noexcept {
        for (const auto &entry : swapchains_)
            if (entry.device == device && entry.swapchain != 0 && !entry.resizing) return true;
        return false;
    }
    std::array<DeviceRecord, device_capacity> devices_{};
    std::array<SwapchainRecord, swapchain_capacity> swapchains_{};
    std::array<FeatureRecord, feature_capacity> features_{};
    std::uint64_t next_feature_generation_ = 0;
    bool device_overflow_ = false;
    bool feature_overflow_ = false;
    bool swapchain_overflow_ = false;
};

} // namespace nr_carrier_activation
