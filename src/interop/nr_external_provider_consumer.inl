// Included after the pinned core TU exports (MC1 external provider consumer).
// Same NR owner as the Feature1 path: Feeder claims this export set and must
// disable its private NGX Feature1 evaluate. No fake Feature1 is created here.
#include "nr_external_provider_d3d12.h"
#include "nr_external_provider_feedback.h"
#include "nr_external_provider_core.inl"

namespace nr_external_provider_runtime {
inline SRWLOCK lock = SRWLOCK_INIT;
inline nr_external_provider::Session session;
inline SRWLOCK operation_lock = SRWLOCK_INIT;
// One producer operation spans CPU admission, command recording and retirement.
// Never let Release retire resources while Submit records against them.
struct OperationGuard {
    bool acquired;
    explicit OperationGuard(bool wait) : acquired(false) {
        if (wait) { AcquireSRWLockExclusive(&operation_lock); acquired = true; }
        else acquired = TryAcquireSRWLockExclusive(&operation_lock) != FALSE;
    }
    ~OperationGuard() { if (acquired) ReleaseSRWLockExclusive(&operation_lock); }
};

inline nr_external_provider_d3d12::CoreEvaluateResult BridgeEvaluate(
    const NrExternalFrameV1 &frame, bool history_reset) {
    const auto ev =
        nr_external_provider_core::EvaluateExternalFrame(frame, history_reset);
    nr_external_provider_d3d12::CoreEvaluateResult out{};
    out.kind = static_cast<std::uint32_t>(ev.kind);
    out.reason = ev.reason;
    return out;
}

inline void EnsureCoreHook() {
    static const bool installed = [] {
        nr_external_provider_d3d12::g_core_evaluate = &BridgeEvaluate;
        return true;
    }();
    (void)installed;
}

inline nr_external_provider::ConfigSnapshot Snapshot() {
    nr_external_provider::ConfigSnapshot cfg;
    // External Feeder path must not treat missing native Feature1 evaluate
    // hooks (nvngx_dlss / g_evaluate_hooks_ready) as observer uncertainty.
    const auto epoch = g_module_observer_epoch.load(std::memory_order_acquire);
    cfg.observer_epoch = epoch;
    if ((epoch & 1u) ||
        g_module_rescan_in_progress.load(std::memory_order_acquire) ||
        g_module_observer_gap_detected.load(std::memory_order_acquire) ||
        g_feature_tracking_uncertain.load(std::memory_order_acquire)) {
        cfg.observer_uncertain = true;
    }
    if (LatchGenericRenoDxConflictIfLoaded() ||
        g_shortfuse_nr_enabled.load(std::memory_order_acquire)) {
        cfg.competing_nr = true;
    }
    const auto config = SnapshotConfig();
    cfg.master_enabled = config.enabled;
    cfg.transfer_strength = ActiveTransferStrength(config);
    return cfg;
}

inline void PublishBypass(NrExternalProviderReason reason) {
    switch (reason) {
    case NrExternalProviderReason::Disabled:
        SetBypassReason(BypassReason::MasterDisabled);
        break;
    case NrExternalProviderReason::CompetingNr:
        SetBypassReason(OtherNrOwnerBypassReason());
        break;
    case NrExternalProviderReason::MissingRequired:
    case NrExternalProviderReason::UnsupportedContract:
        SetBypassReason(BypassReason::UnsupportedInputFormat);
        break;
    case NrExternalProviderReason::LeaseInvalid:
    case NrExternalProviderReason::TransportFailed:
        SetBypassReason(BypassReason::QueueAffinityUnverified);
        break;
    case NrExternalProviderReason::RuntimeUnavailable:
        SetBypassReason(BypassReason::RuntimeUnavailable);
        break;
    case NrExternalProviderReason::ObserverUncertain:
        SetBypassReason(BypassReason::ModuleObserverUncertain);
        break;
    case NrExternalProviderReason::IdentityPassthrough:
    case NrExternalProviderReason::Ready:
        SetBypassReason(BypassReason::None);
        break;
    default:
        break;
    }
}
} // namespace nr_external_provider_runtime

extern "C" __declspec(dllexport) std::int32_t WINAPI NRExternalProvider_QueryV1(
    std::uint32_t abi, std::uint32_t size, std::uint32_t flags,
    NrExternalStatusV1 *output) {
    if (abi != kNrExternalProviderAbi || size != sizeof(*output) || !output ||
        (flags & ~1u))
        return 0;
    nr_external_provider_runtime::EnsureCoreHook();
    if (flags & 1u) PollNgxLifecycleModules(true);
    AcquireSRWLockExclusive(&nr_external_provider_runtime::lock);
    auto cfg = nr_external_provider_runtime::Snapshot();
    NrExternalStatusV1 status{};
    nr_external_provider::FillStatus(nr_external_provider_runtime::session, cfg, status);
    nr_external_provider_feedback::Publish(status);
    ReleaseSRWLockExclusive(&nr_external_provider_runtime::lock);
    *output = status;
    return 1;
}

extern "C" __declspec(dllexport) std::int32_t WINAPI
NRExternalProvider_ClaimOwnershipV1(std::uint32_t abi, std::uint32_t size,
                                    std::uint64_t provider_id,
                                    NrExternalStatusV1 *output) {
    if (abi != kNrExternalProviderAbi || size != sizeof(*output) || !output ||
        !provider_id)
        return 0;
    nr_external_provider_runtime::EnsureCoreHook();
    nr_external_provider_runtime::OperationGuard operation(true);
    EnterCriticalSection(&g_chain_cs);
    AcquireSRWLockExclusive(&nr_external_provider_runtime::lock);
    auto cfg = nr_external_provider_runtime::Snapshot();
    AcquireSRWLockShared(&g_feature_lock);
    for (const auto &feature : g_features)
        if (feature.handle && (feature.feature_id == 1 || feature.feature_id == 13))
            cfg.competing_nr = true;
    ReleaseSRWLockShared(&g_feature_lock);
    const bool ok = nr_external_provider::ClaimOwnership(
        nr_external_provider_runtime::session, cfg, provider_id);
    if (ok)
        nr_external_provider_feedback::affinity_train.store(
            true, std::memory_order_release);
    NrExternalStatusV1 status{};
    nr_external_provider::FillStatus(nr_external_provider_runtime::session, cfg, status);
    nr_external_provider_feedback::Publish(status);
    ReleaseSRWLockExclusive(&nr_external_provider_runtime::lock);
    LeaveCriticalSection(&g_chain_cs);
    *output = status;
    return ok ? 1 : 0;
}

extern "C" __declspec(dllexport) std::int32_t WINAPI
NRExternalProvider_ReleaseOwnershipV1(std::uint32_t abi, std::uint64_t provider_id) {
    if (abi != kNrExternalProviderAbi || !provider_id) return 0;
    nr_external_provider_runtime::OperationGuard operation(true);
    AcquireSRWLockShared(&nr_external_provider_runtime::lock);
    const bool owned = nr_external_provider_runtime::session.owned &&
        nr_external_provider_runtime::session.owner_provider_id == provider_id;
    ReleaseSRWLockShared(&nr_external_provider_runtime::lock);
    if (!owned) return 0;
    nr_external_provider_core::RetireProviderContexts(provider_id);
    nr_external_provider_feedback::affinity_train.store(
        false, std::memory_order_release);
    AcquireSRWLockExclusive(&nr_external_provider_runtime::lock);
    nr_external_provider::ReleaseOwnership(
        nr_external_provider_runtime::session, provider_id);
    NrExternalStatusV1 status{};
    nr_external_provider::FillStatus(nr_external_provider_runtime::session,
        nr_external_provider_runtime::Snapshot(), status);
    nr_external_provider_feedback::Publish(status);
    ReleaseSRWLockExclusive(&nr_external_provider_runtime::lock);
    return 1;
}

extern "C" __declspec(dllexport) std::int32_t WINAPI
NRExternalProvider_SubmitFrameV1(std::uint32_t abi, const NrExternalFrameV1 *frame,
                                 NrExternalSubmitResultV1 *result) {
    if (abi != kNrExternalProviderAbi || !frame || !result) return 0;
    if (frame->size != sizeof(*frame) || frame->abi != kNrExternalProviderAbi)
        return 0;
    nr_external_provider_runtime::EnsureCoreHook();

    nr_external_provider_runtime::OperationGuard operation(false);
    if (!operation.acquired) {
        *result = NrExternalSubmitResultV1{};
        result->reason = NrExternalProviderReason::Busy;
        return 0;
    }
    NrExternalFrameV1 local = *frame;
    nr_external_provider::AdmitDecision admit{};
    nr_external_provider::ConfigSnapshot cfg{};
    bool do_nr = false;
    nr_external_provider::Session before{};
    AcquireSRWLockExclusive(&nr_external_provider_runtime::lock);
    cfg = nr_external_provider_runtime::Snapshot();
    if (nr_external_provider::StrengthAllowsWork(cfg) &&
        nr_external_provider_d3d12::g_core_evaluate == nullptr) {
        admit.reason = NrExternalProviderReason::RuntimeUnavailable;
        admit.admit = false;
        ++nr_external_provider_runtime::session.rejected_frames;
        nr_external_provider_runtime::session.last_bypass = admit.reason;
        NrExternalSubmitResultV1 out{};
        nr_external_provider::FillSubmitResult(admit, local, false, out);
        NrExternalStatusV1 status{};
        nr_external_provider::FillStatus(nr_external_provider_runtime::session, cfg,
                                         status);
        nr_external_provider_feedback::Publish(status);
        nr_external_provider_runtime::PublishBypass(admit.reason);
        ReleaseSRWLockExclusive(&nr_external_provider_runtime::lock);
        *result = out;
        return 0;
    }
    before = nr_external_provider_runtime::session;
    admit = nr_external_provider::Admit(
        nr_external_provider_runtime::session, cfg, local);
    do_nr = admit.admit && admit.run_nr;
    const bool history_reset = admit.history_reset;
    if (!admit.admit || !do_nr) {
        NrExternalSubmitResultV1 out{};
        nr_external_provider::FillSubmitResult(admit, local, false, out);
        if (admit.admit)
            nr_external_provider_feedback::external_submits.fetch_add(
                1, std::memory_order_relaxed);
        NrExternalStatusV1 status{};
        nr_external_provider::FillStatus(nr_external_provider_runtime::session, cfg,
                                         status);
        nr_external_provider_feedback::Publish(status);
        nr_external_provider_runtime::PublishBypass(admit.reason);
        ReleaseSRWLockExclusive(&nr_external_provider_runtime::lock);
        *result = out;
        return admit.admit ? 1 : 0;
    }
    ReleaseSRWLockExclusive(&nr_external_provider_runtime::lock);

    const auto ev =
        nr_external_provider_d3d12::g_core_evaluate(local, history_reset);

    AcquireSRWLockExclusive(&nr_external_provider_runtime::lock);
    bool writeback = false;
    if (ev.kind == 2u) {
        writeback = true;
        nr_external_provider::NoteWriteback(
            nr_external_provider_runtime::session, true);
        admit.reason = NrExternalProviderReason::Ready;
        nr_external_provider_feedback::nr_backend.store(
            3, std::memory_order_relaxed);
    } else if (ev.kind == 1u) {
        admit.reason = NrExternalProviderReason::IdentityPassthrough;
        nr_external_provider_runtime::session.last_bypass = admit.reason;
        ++nr_external_provider_runtime::session.identity_frames;
    } else {
        admit.reason = ev.reason;
        admit.admit = false;
        admit.run_nr = false;
        nr_external_provider::RollbackAdmit(
            nr_external_provider_runtime::session, before, admit.reason);
    }
    NrExternalSubmitResultV1 out{};
    nr_external_provider::FillSubmitResult(admit, local, writeback, out);
    nr_external_provider_feedback::external_submits.fetch_add(
        1, std::memory_order_relaxed);
    NrExternalStatusV1 status{};
    nr_external_provider::FillStatus(nr_external_provider_runtime::session, cfg,
                                     status);
    nr_external_provider_feedback::Publish(status);
    nr_external_provider_runtime::PublishBypass(admit.reason);
    ReleaseSRWLockExclusive(&nr_external_provider_runtime::lock);
    *result = out;
    // Identity-retained still returns 1 (admitted) with writeback_performed=0.
    return (ev.kind != 0u) ? 1 : 0;
}

void NrExternalNoteNativeFeature1(std::uint64_t view_id,
                                         std::uint64_t frame_id, bool nr_ran) {
    if (!TryAcquireSRWLockExclusive(&nr_external_provider_runtime::lock))
        return;
    nr_external_provider::NoteNativeFeature1Frame(
        nr_external_provider_runtime::session, view_id, frame_id, nr_ran);
    nr_external_provider_feedback::native_feature1_notes.fetch_add(
        1, std::memory_order_relaxed);
    if (nr_ran)
        nr_external_provider_feedback::nr_backend.store(2, std::memory_order_relaxed);
    auto cfg = nr_external_provider_runtime::Snapshot();
    NrExternalStatusV1 status{};
    nr_external_provider::FillStatus(nr_external_provider_runtime::session, cfg,
                                     status);
    nr_external_provider_feedback::Publish(status);
    ReleaseSRWLockExclusive(&nr_external_provider_runtime::lock);
}
