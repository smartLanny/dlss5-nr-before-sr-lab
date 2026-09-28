#pragma once
#include <cstddef>

// Residency decisions are NOT GPU completion proofs. The caller holds the
// existing Core lock; its unchanged fence/recording/binding checks authorize
// mutations and its collector performs actual destruction outside the lock.
namespace nr_residency {
template <typename Slot, typename Eligible>
Slot* OldestReusable(Slot* slots, std::size_t count, Eligible eligible) noexcept {
    Slot* result=nullptr;
    if (!slots) return nullptr;
    for (std::size_t i=0;i<count;++i)
        if (eligible(slots[i]) &&
            (!result || slots[i].last_used_serial < result->last_used_serial))
            result=&slots[i];
    return result;
}

template <typename Context>
unsigned RetireForCompaction(Context* contexts, std::size_t count,
                            unsigned previous_active, unsigned requested_active,
                            bool explicit_route_change=false) noexcept {
    // Some signed runtimes retain their allocator high-water until the last
    // NR handle is released. Explicit downsizing therefore drains our whole
    // old closure, not just the removed children. Ordinary F6 keeps it warm.
    if (!contexts || requested_active==0 || requested_active>5 ||
        (!explicit_route_change && requested_active>=previous_active)) return 0;
    unsigned marked=0;
    for (std::size_t i=0;i<count;++i) {
        auto& context=contexts[i];
        if (context.sr_handle) {
            context.retired=true;
            ++marked;
        }
    }
    return marked;
}
} // namespace nr_residency
