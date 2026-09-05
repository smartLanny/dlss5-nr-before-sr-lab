#pragma once
#include <cstdint>
#include <cstddef>

// Owned by the bridge and used only while g_bridge_cs is held. No pointer
// dereferences. A full table disables the fast path for new handles, rather
// than forgetting a native history that may need Reset after skipped frames.
class NrNativeBridgeHistory {
    struct Entry { std::uintptr_t handle = 0; bool dirty = false; };
    Entry entries_[16]{};
    Entry* Find(std::uintptr_t handle) noexcept {
        for (auto& e : entries_) if (e.handle == handle && handle) return &e;
        return nullptr;
    }
public:
    bool Admit(std::uintptr_t handle, bool resetKnown) noexcept {
        if (!handle || !resetKnown) return false;
        if (Find(handle)) return true;
        for (auto& e : entries_) if (!e.handle) { e.handle = handle; return true; }
        return false;
    }
    bool NeedsReset(std::uintptr_t handle) noexcept {
        const auto* e = Find(handle); return e && e->dirty;
    }
    void Mirrored(std::uintptr_t handle) noexcept {
        if (auto* e = Find(handle)) e->dirty = true;
    }
    void NativeCompleted(std::uintptr_t handle, bool succeeded, bool resetKnown) noexcept {
        if (succeeded && resetKnown) if (auto* e = Find(handle)) e->dirty = false;
    }
    void Created(std::uintptr_t handle) noexcept {
        if (auto* e = Find(handle)) *e = {};
    }
};
