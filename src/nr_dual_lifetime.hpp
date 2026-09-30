#pragma once
namespace nr_dual {
// Contexts are moved twice by the existing deferred-release queue. Rebase the
// internal owner pointer after each move; the vacated active entry can be reused.
template<class Context> void RebindSlotOwners(Context& context) {
    for(auto& slot:context.slots)if(slot.dual_spatial)slot.dual_owner=&context;
}
}
