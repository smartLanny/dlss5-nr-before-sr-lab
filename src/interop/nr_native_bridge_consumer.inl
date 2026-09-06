// Included after the pinned beta0.4.2 TU, in an explicitly experimental build.
#include "nr_native_bridge_abi.h"

extern "C" __declspec(dllexport) std::int32_t WINAPI NRNativeBridge_QueryV1(
    std::uint32_t abi, std::uint32_t size, std::uint32_t flags,
    NrNativeBridgeStatusV1* output) {
    if (abi != kNrNativeBridgeAbi || size != sizeof(*output) ||
        !output || (flags & ~1u)) return 0;
    NrNativeBridgeStatusV1 status{};
    // The worker publishes readiness only after initialization. Do not enter
    // core critical sections before that publication.
    if (!g_evaluate_hooks_ready.load(std::memory_order_acquire)) {
        *output = status; return 1;
    }
    // Called between NGX calls, never from DllMain or a loader notification.
    // Reconcile at session admission, not on every frame. This closes the
    // missed-first-Create race without weakening late-module/RR guards.
    if (flags & 1u) PollNgxLifecycleModules(true);
    const auto epoch = g_module_observer_epoch.load(std::memory_order_acquire);
    status.observerEpoch = epoch;
    if ((epoch & 1u) || g_module_rescan_in_progress.load(std::memory_order_acquire) ||
        g_module_observer_gap_detected.load(std::memory_order_acquire) ||
        g_feature_tracking_uncertain.load(std::memory_order_acquire)) {
        status.reason = NrNativeBridgeReason::ObserverUncertain;
    } else if (LatchGenericRenoDxConflictIfLoaded() ||
               g_shortfuse_nr_enabled.load(std::memory_order_acquire)) {
        status.reason = NrNativeBridgeReason::CompetingNr;
    } else {
        // No settings writes; existing F6, ApplyConfig and F8 locks still own it.
        const auto config = SnapshotConfig();
        status.reason = config.enabled && ActiveTransferStrength(config) > 0.000001f
            ? NrNativeBridgeReason::Ready : NrNativeBridgeReason::Disabled;
    }
    if (epoch != g_module_observer_epoch.load(std::memory_order_acquire))
        status.reason = NrNativeBridgeReason::Busy;
    *output = status;
    return 1;
}
