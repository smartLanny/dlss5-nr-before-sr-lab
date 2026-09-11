#pragma once

#include <cstddef>

// Select only among slots that already represent the requested external
// resource/exposure pair.  Empty-slot allocation and old-slot recycling stay
// in FindOrCreateSlot, where the existing resource/lifetime contract owns
// those mutations.  A busy slot with a different Motion binding is skipped so
// it cannot hide a later complete or safely writable match.
namespace nr_motion_slot {

template <typename Slot, typename SameExternal, typename CompleteBinding,
          typename CanWrite>
Slot *SelectExisting(Slot *slots, std::size_t count,
                     SameExternal same_external,
                     CompleteBinding complete_binding,
                     CanWrite can_write) noexcept {
    if (slots == nullptr) return nullptr;
    for (std::size_t i = 0; i < count; ++i) {
        if (same_external(slots[i]) && complete_binding(slots[i]))
            return &slots[i];
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (same_external(slots[i]) && can_write(slots[i]))
            return &slots[i];
    }
    return nullptr;
}

} // namespace nr_motion_slot
