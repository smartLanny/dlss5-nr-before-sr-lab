#include "nr_motion_slot_selection.h"

#include <cstddef>
#include <cstdio>

struct Slot {
    int external = 0;
    int exposure = 0;
    int motion = 0;
    bool motion_proxy = false;
    bool writable = false;
    unsigned descriptor_revision = 0;
};

Slot *Select(Slot *slots, std::size_t count, int external, int exposure,
             int required_motion, bool require_motion) {
    return nr_motion_slot::SelectExisting(
        slots, count,
        [&](const Slot &slot) {
            return slot.external == external && slot.exposure == exposure;
        },
        [&](const Slot &slot) {
            return !require_motion ||
                (slot.motion == required_motion && slot.motion_proxy);
        },
        [&](const Slot &slot) { return slot.writable; });
}

Slot *FirstExternal(Slot *slots, std::size_t count, int external,
                    int exposure) {
    for (std::size_t i = 0; i < count; ++i) {
        if (slots[i].external == external && slots[i].exposure == exposure)
            return &slots[i];
    }
    return nullptr;
}

Slot *FirstEmpty(Slot *slots, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        if (slots[i].external == 0) return &slots[i];
    }
    return nullptr;
}

bool Check(bool condition, const char *oracle) {
    if (condition) return true;
    std::fprintf(stderr, "CHECK_FAIL:%s\n", oracle);
    return false;
}

int main() {
    Slot slots[8] = {};
    bool ok = true;

    // An early idle wrong-MV candidate must not hide a later complete binding.
    slots[0] = {1, 2, 10, true, true, 41};
    slots[1] = {1, 2, 20, true, false, 73};
    const unsigned before_a = slots[0].descriptor_revision;
    ok &= Check(FirstExternal(slots, 8, 1, 2) == &slots[0],
                "OLD_STRATEGY_ORACLE: old first-external selector expected slot A");
    ok &= Check(Select(slots, 8, 1, 2, 20, true) == &slots[1],
                "OLD_STRATEGY_ORACLE: busy/idle wrong-MV slot hid later exact binding");
    ok &= Check(slots[0].descriptor_revision == before_a,
                "slot A descriptor changed while selecting later exact binding");

    // A complete binding remains reusable even while it is busy.
    slots[0] = {1, 2, 20, true, false, 81};
    slots[1] = {1, 2, 10, true, true, 82};
    const unsigned busy_revision = slots[0].descriptor_revision;
    ok &= Check(Select(slots, 8, 1, 2, 20, true) == &slots[0],
                "complete busy binding was not reusable");
    ok &= Check(slots[0].descriptor_revision == busy_revision,
                "complete busy binding descriptor changed");

    // A busy slot with a missing proxy cannot be treated as a complete binding.
    slots[0] = {1, 2, 20, false, false, 91};
    for (std::size_t i = 1; i < 8; ++i)
        slots[i] = {1, 2, 10, true, false, 100u + static_cast<unsigned>(i)};
    ok &= Check(Select(slots, 8, 1, 2, 20, true) == nullptr,
                "busy binding with missing proxy was incorrectly admitted");

    // A mismatched busy slot does not consume the caller's empty-slot path.
    slots[0] = {1, 2, 10, true, false, 111};
    slots[1] = {};
    for (std::size_t i = 2; i < 8; ++i)
        slots[i] = {1, 2, 10, true, false, 120u + static_cast<unsigned>(i)};
    const unsigned before_busy = slots[0].descriptor_revision;
    ok &= Check(Select(slots, 8, 1, 2, 20, true) == nullptr,
                "busy mismatched slot should defer to empty-slot caller path");
    ok &= Check(FirstEmpty(slots, 8) == &slots[1],
                "empty-slot caller path was not preserved");
    ok &= Check(slots[0].descriptor_revision == before_busy,
                "busy mismatched slot descriptor changed");

    // Without a required resample binding, the existing external slot remains
    // eligible regardless of the retained Motion pointer.
    ok &= Check(Select(slots, 8, 1, 2, 0, false) == &slots[0],
                "non-resampled binding did not reuse the external slot");

    if (!ok) return 1;
    std::puts("MOTION_SLOT_SELECTION_PASS: 5 scenarios passed");
    return 0;
}
