// Included after the Core lifecycle implementation. This preflight never grants
// NR ownership and must be repeated before each injected SR Create lifecycle.
inline NrExternalStatusV1 QueryNativeObserverStartup() {
    NrExternalStatusV1 status{};
    // 0.4.7beta predates the later worker stop/startup latches.  Its existing
    // hook-ready and module-fault state provides the same fail-closed query
    // without importing a second lifecycle implementation into Core.
    if (!g_evaluate_hooks_ready.load(std::memory_order_acquire)) {
        status.reason = NrExternalProviderReason::Starting;
        return status;
    }
    // Sample publication before polling, so publication racing this call cannot
    // turn an early-return module poll into a successful native preflight.
    PollNgxLifecycleModules(true);
    const auto epoch = g_module_observer_epoch.load(std::memory_order_acquire);
    status.observer_epoch = epoch;
    if ((epoch & 1u) ||
        g_module_rescan_in_progress.load(std::memory_order_acquire) ||
        g_module_observer_gap_detected.load(std::memory_order_acquire) ||
        g_feature_tracking_uncertain.load(std::memory_order_acquire))
        status.reason = NrExternalProviderReason::ObserverUncertain;
    else if (g_module_observer_fault.load(std::memory_order_acquire) != 0)
        status.reason = NrExternalProviderReason::ObserverFailed;
    else
        status.reason = NrExternalProviderReason::Ready;
    if (epoch != g_module_observer_epoch.load(std::memory_order_acquire))
        status.reason = NrExternalProviderReason::Busy;
    return status;
}
