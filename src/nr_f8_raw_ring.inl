// Fixed immutable raw-slot ring for F8. GPU resources belong to the caller;
// this module only owns slot identity and exact-fence admission/reclamation.
#ifndef NR_F8_RAW_RING_INL
#define NR_F8_RAW_RING_INL

namespace nr_f8_raw_ring {

constexpr unsigned int kCapacity = 16;

struct Identity {
    unsigned long long frame = 0;
    unsigned long long instance = 0;
    unsigned int generation = 0;
    unsigned int width = 0;
    unsigned int height = 0;
};

struct Slot {
    unsigned long long request = 0;
    unsigned long long fence = 0;
    Identity identity = {};
    bool reserved = false;
    bool submitted = false;
    bool completed = false;
};

inline bool SameIdentity(const Identity &left, const Identity &right) {
    return left.frame == right.frame && left.instance == right.instance &&
        left.generation == right.generation && left.width == right.width &&
        left.height == right.height;
}

class Ring {
public:
    bool Reserve(unsigned long long request, unsigned int slot,
                 const Identity &identity) {
        if (request == 0 || slot >= kCapacity || slots_[slot].reserved) return false;
        slots_[slot] = {};
        slots_[slot].request = request;
        slots_[slot].identity = identity;
        slots_[slot].reserved = true;
        return true;
    }

    bool Submit(unsigned long long request, unsigned int slot,
                unsigned long long fence) {
        if (!Matches(request, slot) || fence == 0 || slots_[slot].submitted)
            return false;
        slots_[slot].fence = fence;
        slots_[slot].submitted = true;
        return true;
    }

    // Completion is accepted only for the immutable request, slot, fence and
    // identity. A stale callback therefore cannot attach pixels to a newer
    // frame even when the slot index has since been reused.
    bool Complete(unsigned long long request, unsigned int slot,
                  unsigned long long fence, const Identity &identity) {
        if (!Matches(request, slot) || !slots_[slot].submitted ||
            slots_[slot].completed || slots_[slot].fence != fence ||
            !SameIdentity(slots_[slot].identity, identity)) return false;
        slots_[slot].completed = true;
        return true;
    }

    bool Reclaim(unsigned long long request, unsigned int slot,
                 unsigned long long fence) {
        if (!Matches(request, slot) || !slots_[slot].completed ||
            slots_[slot].fence != fence) return false;
        slots_[slot] = {};
        return true;
    }

    bool IsReserved(unsigned int slot) const {
        return slot < kCapacity && slots_[slot].reserved;
    }
    bool IsCompleted(unsigned int slot) const {
        return slot < kCapacity && slots_[slot].completed;
    }
    const Slot &Get(unsigned int slot) const { return slots_[slot]; }

    unsigned int ReservedCount() const {
        unsigned int count = 0;
        for (const Slot &slot : slots_) count += slot.reserved ? 1u : 0u;
        return count;
    }
    unsigned int CompletedCount() const {
        unsigned int count = 0;
        for (const Slot &slot : slots_) count += slot.completed ? 1u : 0u;
        return count;
    }

private:
    bool Matches(unsigned long long request, unsigned int slot) const {
        return request != 0 && slot < kCapacity && slots_[slot].reserved &&
            slots_[slot].request == request;
    }

    Slot slots_[kCapacity] = {};
};

} // namespace nr_f8_raw_ring
#endif
