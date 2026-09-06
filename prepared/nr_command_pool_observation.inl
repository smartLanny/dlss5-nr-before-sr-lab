// Included inside command-state implementation. All functions require its lock.
// Only an actual carrier's verified device/vtable family may be reused. This
// starts future observation; it does not share bindings or invent a baseline.
struct ObservedCommandFamily {
    UINT64 device_identity = 0;
    const ID3D12GraphicsCommandListVtbl *vtable = nullptr;
    bool null_guard = false;
};
ObservedCommandFamily g_observed_command_families[16] = {};
constexpr unsigned kMaxPrimedCommandStates = 128; // bounded snapshots and CPU-only descriptor caches
unsigned g_primed_command_state_count = 0;
UINT64 g_command_prime_serial = 0;

const ObservedCommandFamily *FindObservedCommandFamilyLocked(
        const CommandStateRecord &record, ID3D12GraphicsCommandList *native) {
    if (!native || !record.meta_contract.device_identity || !record.meta_contract.known ||
        ((record.meta_contract.initialization_dirty | record.meta_contract.execution_dirty) &
         ~kCommandStateCoveredDomains) != 0) return nullptr;
    for (const auto &family : g_observed_command_families)
        if (family.device_identity == record.meta_contract.device_identity &&
            family.vtable == native->lpVtbl) return &family;
    return nullptr;
}
void RememberObservedCommandFamilyLocked(const CommandStateRecord &record,
        ID3D12GraphicsCommandList *native, bool null_guard) {
    if (!native || !record.meta_contract.device_identity || !record.meta_contract.known ||
        ((record.meta_contract.initialization_dirty | record.meta_contract.execution_dirty) &
         ~kCommandStateCoveredDomains) != 0) return;
    ObservedCommandFamily *empty = nullptr;
    for (auto &family : g_observed_command_families) {
        if (family.device_identity == record.meta_contract.device_identity && family.vtable == native->lpVtbl) {
            family.null_guard = family.null_guard || null_guard;
            return;
        }
        if (!family.device_identity && !empty) empty = &family;
    }
    if (empty) *empty = {record.meta_contract.device_identity, native->lpVtbl, null_guard};
    // Exhaustion means a missed optimization, never permission to trust a family.
}
void PrimeCommandListBeforeResetLocked(CommandStateRecord &record,
        ID3D12GraphicsCommandList *native) {
    const auto *family = FindObservedCommandFamilyLocked(record, native);
    if (!family || record.native != native) return;
    if (record.state == nullptr) {
        // Evict only observer states NEVER used by an NR carrier. No plugin
        // submission or fence can depend on these private CPU descriptors.
        if (g_primed_command_state_count >= kMaxPrimedCommandStates) {
            CommandStateRecord *oldest = nullptr;
            for (auto &candidate : g_command_states)
                if (candidate.prime_serial && candidate.state &&
                    (!oldest || candidate.prime_serial < oldest->prime_serial)) oldest = &candidate;
            if (!oldest) return;
            ReleaseNativeCommandState(*oldest->state); delete oldest->state;
            oldest->state = nullptr; oldest->prime_serial = 0;
            oldest->hooks_ready = false; oldest->null_guard_requested = false;
            oldest->next_hook_attempt = 0;
            --g_primed_command_state_count;
            g_command_state_epoch.fetch_add(1, std::memory_order_release);
        }
        record.state = new (std::nothrow) NativeCommandState;
        if (!record.state) return;
        record.state->meta_initialization_dirty = record.meta_contract.initialization_dirty;
        record.state->meta_execution_dirty = record.meta_contract.execution_dirty;
        record.prime_serial = ++g_command_prime_serial;
        ++g_primed_command_state_count;
        record.hooks_ready = true;
        record.next_hook_attempt = 0;
        g_command_state_epoch.fetch_add(1, std::memory_order_release);
    }
    // Coverage upgrades apply before a future successful Reset, not retroactively.
    if (record.hooks_ready && family->null_guard && !record.state->null_guard_covered) {
        record.null_guard_requested = true;
        record.state->null_guard_covered = true;
        record.state->null_guard_reset_observed = false;
        record.state->known = false;
        record.state->unknown_reason = "null-guard-awaiting-reset";
    }
}
