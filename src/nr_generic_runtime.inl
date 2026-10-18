// A coherent receipt for native and Present final composition. It describes
// recorded constants only; it is not GPU completion or observed pixel proof.
struct NrGenericPolicySnapshot {
    nr_generic::Selection selection{};
    unsigned config_generation = 0;
    unsigned long long tick = 0;
    bool present = false;
    bool current = false;
};
NrGenericPolicySnapshot g_recorded_generic_policy{};
void RecordGenericPolicy(nr_generic::Selection selection, bool present) {
    EnterCriticalSection(&g_chain_cs);
    g_recorded_generic_policy = {selection,
        g_config_generation.load(std::memory_order_relaxed), GetTickCount64(), present, true};
    LeaveCriticalSection(&g_chain_cs);
}
NrGenericPolicySnapshot SnapshotGenericPolicy() {
    EnterCriticalSection(&g_chain_cs);
    auto result = g_recorded_generic_policy;
    const auto now = GetTickCount64();
    result.current = result.current && g_config.enabled && g_config.generic_residual &&
        result.config_generation == g_config_generation.load(std::memory_order_relaxed) &&
        now >= result.tick && now - result.tick <= 2000;
    LeaveCriticalSection(&g_chain_cs);
    return result;
}
