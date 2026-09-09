// Included after NrSlot. All journal locks protect values only. COM, NGX and
// command recording remain in their existing callers, outside this lock.
#define NR_HAS_RESOURCE_RECOVERY 1
namespace nr_recovery = nr_resource_recovery;
SRWLOCK g_nr_recovery_lock = SRWLOCK_INIT;
nr_recovery::Ledger<512, 512, 64> g_nr_recovery;
nr_recovery::Scope g_nr_recovery_scopes[64]{};
nr_recovery::Key g_nr_recovery_last_resources[64]{};
std::atomic<unsigned long long> g_nr_recovery_frame{0};
unsigned long long NrFormatDeviceIdentity(ID3D12Device*);
unsigned long long NrFormatResourceIdentity(ID3D12Resource*);
const char* NrRecoveryFailureReason(const nr_recovery::Failure&);

struct NrRecoverySnapshot {
    nr_compatibility_verdict::Input input{};
    nr_recovery::Key key{};
    nr_recovery::ViewSignature signature{};
    unsigned long long frame = 0;
    bool receipt_overflow = false;
};
NrRecoverySnapshot g_nr_recovery_snapshot;
struct NrRecoveryDetailSnapshot {
    NrRecoverySnapshot current{};
    nr_recovery::RecoveryEvidence proof{};
};
NrRecoveryDetailSnapshot SnapshotNrRecoveryDetails() {
    AcquireSRWLockShared(&g_nr_recovery_lock);
    const auto current = g_nr_recovery_snapshot;
    const auto proof = g_nr_recovery.Read(current.key, current.signature);
    ReleaseSRWLockShared(&g_nr_recovery_lock);
    return {current, proof};
}
struct NrRecoveryCarrierScope {
#if defined(NR_HAS_TIMING)
    NrTimingCarrierScope timing;
#endif
    NrRecoveryCarrierScope* parent = nullptr;
    nr_recovery::Scope scopes[2]{};
    nr_recovery::Carrier carriers[2]{};
    nr_recovery::Token tokens[2]{};
    bool transferred[2]{};
    NrRecoverySnapshot snapshot{};
    unsigned selected_role = 0;
    bool command_observed = false;
    NrRecoveryCarrierScope(ID3D12GraphicsCommandList*, unsigned long long);
    void ReconcileConfig();
    ~NrRecoveryCarrierScope();
};
thread_local NrRecoveryCarrierScope* g_nr_recovery_carrier = nullptr;

void NrRecoveryRememberScopeLocked(const nr_recovery::Scope& scope) {
    for (auto& key : g_nr_recovery_last_resources)
        if (key.resource && key.scope.config_generation != scope.config_generation) key = {};
    for (auto& old : g_nr_recovery_scopes) {
        if (old.device && old.config_generation != scope.config_generation) {
            g_nr_recovery.RetireScope(old); old = {};
        }
    }
    for (const auto& old : g_nr_recovery_scopes) if (old == scope) return;
    for (auto& old : g_nr_recovery_scopes) if (!old.device) { old = scope; return; }
}
void NrRecoveryForgetPreviousWaitingLocked(const nr_recovery::Key& key) {
    nr_recovery::Key* vacant = nullptr;
    for (auto& prior : g_nr_recovery_last_resources) {
        if (!prior.resource) { if (!vacant) vacant = &prior; continue; }
        if (prior.scope == key.scope && prior.role == key.role) {
            if (!(prior == key)) g_nr_recovery.ForgetUnattempted(prior);
            prior = key; return;
        }
    }
    if (vacant) *vacant = key;
}
void NrRecoveryRetireFeature(unsigned long long generation) {
    AcquireSRWLockExclusive(&g_nr_recovery_lock);
    for (auto& scope : g_nr_recovery_scopes) if (scope.device && scope.feature_generation == generation) {
        g_nr_recovery.RetireScope(scope); scope = {};
    }
    for (auto& key : g_nr_recovery_last_resources)
        if (key.resource && key.scope.feature_generation == generation) key = {};
    ReleaseSRWLockExclusive(&g_nr_recovery_lock);
}
NrRecoveryCarrierScope::NrRecoveryCarrierScope(ID3D12GraphicsCommandList* list,
        unsigned long long generation) {
    parent = g_nr_recovery_carrier;
    snapshot.frame = g_nr_recovery_frame.fetch_add(1, std::memory_order_relaxed) + 1;
    ID3D12Device* device = nullptr;
    unsigned long long identity = 0;
    if (list && SUCCEEDED(ID3D12GraphicsCommandList_GetDevice(list, IID_ID3D12Device,
            reinterpret_cast<void**>(&device))) && device) {
        identity = NrFormatDeviceIdentity(device);
        ID3D12Device_Release(device);
    }
    const auto config = g_config_generation.load(std::memory_order_acquire);
    const auto requested = static_cast<unsigned>(g_requested_mode_fast.load(std::memory_order_acquire));
    selected_role = requested == 1 ? 1u : 0u;
    snapshot.input.current = {true, config, generation, requested, requested};
#if defined(NR_HAS_TIMING)
    // Requested PRE may decline into POST. Route becomes known at the existing
    // pipeline admission point, not while observing the carrier or its views.
    timing.Begin(snapshot.frame, generation, config, 0, identity);
#endif
    snapshot.input.observer = {nr_compatibility_verdict::FactState::Complete,
        snapshot.input.current, "classified-live-Feature1-carrier", true};
    AcquireSRWLockExclusive(&g_nr_recovery_lock);
    for (unsigned role = 0; role != 2; ++role) {
        // Observe both routes when PRE may fall back to POST. A bypass breaks
        // its route's continuity even if the other route completed this call.
        scopes[role] = {identity, identity, 1, generation, config, role ? 1u : 2u};
        if (requested == 1 && role == 0) continue;
        NrRecoveryRememberScopeLocked(scopes[role]);
        carriers[role] = g_nr_recovery.ObserveCarrier(scopes[role], role, snapshot.frame);
    }
    ReleaseSRWLockExclusive(&g_nr_recovery_lock);
    g_nr_recovery_carrier = this;
}
void NrRecoveryCarrierScope::ReconcileConfig() {
    const auto config = g_config_generation.load(std::memory_order_acquire);
    const auto requested = static_cast<unsigned>(g_requested_mode_fast.load(std::memory_order_acquire));
    if (snapshot.input.current.config == config && snapshot.input.current.requested_mode == requested) return;
    AcquireSRWLockExclusive(&g_nr_recovery_lock);
    for (unsigned role = 0; role != 2; ++role) {
        g_nr_recovery.EndCarrier(scopes[role], role, carriers[role], false);
        scopes[role].config_generation = config;
        NrRecoveryRememberScopeLocked(scopes[role]);
        carriers[role] = (requested == 1 && role == 0) ? nr_recovery::Carrier{} :
            g_nr_recovery.ObserveCarrier(scopes[role], role, snapshot.frame);
    }
    ReleaseSRWLockExclusive(&g_nr_recovery_lock);
    snapshot.input = {}; snapshot.key = {}; snapshot.signature = {};
    snapshot.input.current = {true, config, scopes[1].feature_generation, requested, requested};
    snapshot.input.observer = {nr_compatibility_verdict::FactState::Complete,
        snapshot.input.current, "classified-live-Feature1-carrier", true};
    selected_role = requested == 1 ? 1u : 0u;
}
NrRecoveryCarrierScope::~NrRecoveryCarrierScope() {
    AcquireSRWLockExclusive(&g_nr_recovery_lock);
    for (unsigned role = 0; role != 2; ++role)
        g_nr_recovery.EndCarrier(scopes[role], role, carriers[role], transferred[role]);
    if (snapshot.frame >= g_nr_recovery_snapshot.frame) {
        if (nr_compatibility_verdict::SameScope(snapshot.input.current, g_nr_recovery_snapshot.input.current))
            snapshot.receipt_overflow |= g_nr_recovery_snapshot.receipt_overflow;
        g_nr_recovery_snapshot = snapshot;
    }
    ReleaseSRWLockExclusive(&g_nr_recovery_lock);
    g_nr_recovery_carrier = parent;
}
void NrObserveCompatibilityBypass(BypassReason reason) {
#if defined(NR_HAS_TIMING)
    NrTimingBypass(static_cast<unsigned>(reason));
#endif
    using namespace nr_compatibility_verdict;
    if (auto* frame = g_nr_recovery_carrier) {
        auto& input = frame->snapshot.input;
        if (reason == BypassReason::None) { input.first_block = {}; return; }
        if (!input.first_block.unresolved)
            input.first_block = {true, true, input.current, BypassReasonName(reason),
                ClassifyReason(BypassReasonName(reason)).known};
        return;
    }
    // Early process guards have no classified resource identity. Export their
    // actual cause without borrowing a prior Feature's success evidence.
    if (reason == BypassReason::None) {
        AcquireSRWLockExclusive(&g_nr_recovery_lock);
        auto& value = g_nr_recovery_snapshot;
        // RR uses the existing separate route. Its successful prepare/finish
        // clears its unclassified block, but is not Feature1 recovery proof.
        // An unrelated unscoped notification cannot clear a classified failure.
        if (!value.input.current.feature_generation && !value.key.resource) {
            value = {};
            const auto mode = static_cast<unsigned>(g_requested_mode_fast.load(std::memory_order_acquire));
            value.input.current = {true, g_config_generation.load(std::memory_order_acquire), 0, mode, mode};
            value.input.first_block = {true, true, value.input.current, "waiting-feature1", true};
        }
        ReleaseSRWLockExclusive(&g_nr_recovery_lock);
        return;
    }
    AcquireSRWLockExclusive(&g_nr_recovery_lock);
    for (const auto& scope : g_nr_recovery_scopes)
        if (scope.device) g_nr_recovery.InterruptScope(scope, scope.mode == 1 ? 1u : 0u);
    auto& value = g_nr_recovery_snapshot;
    value = {};
    const auto mode = static_cast<unsigned>(g_requested_mode_fast.load(std::memory_order_acquire));
    value.input.current = {true, g_config_generation.load(std::memory_order_acquire), 0, mode, mode};
    value.input.first_block = {true, true, value.input.current, BypassReasonName(reason),
        ClassifyReason(BypassReasonName(reason)).known};
    ReleaseSRWLockExclusive(&g_nr_recovery_lock);
}
void NrRecoveryCommandObserved() {
#if defined(NR_HAS_TIMING)
    NrTimingCommandObserved();
#endif
    if (auto* frame = g_nr_recovery_carrier) {
        frame->command_observed = true;
        frame->snapshot.input.command = {nr_compatibility_verdict::FactState::Complete,
            frame->snapshot.input.current, "actual-native-command-state-captured", true};
    }
}
void NrRecoveryRecord(const nr_recovery::Token& token, nr_recovery::Stage stage,
                      bool success, NgxResult result = 0) {
    AcquireSRWLockExclusive(&g_nr_recovery_lock);
    g_nr_recovery.Record(token, stage, success, static_cast<std::int32_t>(result));
    ReleaseSRWLockExclusive(&g_nr_recovery_lock);
}
void NrRecoveryDescriptorProgrammed(const NrSlot& slot, ID3D12DescriptorHeap* heap) {
    if (heap != slot.heap) return;
    slot.descriptor_receipt = {heap, slot.source, slot.result, slot.source_srv_format, slot.result_uav_format};
}
void NrRecoveryNativeResult(NgxResult result) {
    if (auto* frame = g_nr_recovery_carrier)
        for (const auto& token : frame->tokens)
            NrRecoveryRecord(token, nr_recovery::Stage::NativeSrEvaluate, result == kNgxSuccess, result);
}
nr_recovery::Token NrRecoveryTransferRecorded(unsigned mode) {
    auto* frame = g_nr_recovery_carrier;
    if (!frame || (mode != 1 && mode != 2)) return {};
    const unsigned role = mode == 1 ? 1u : 0u;
    const auto token = frame->tokens[role];
    NrRecoveryRecord(token, nr_recovery::Stage::TransferRecorded, true);
    frame->transferred[role] = static_cast<bool>(token);
    return token;
}
void NrRecoveryReceiptOverflow(const nr_recovery::Token& token) {
    if (auto* frame = g_nr_recovery_carrier) frame->snapshot.receipt_overflow = true;
    AcquireSRWLockExclusive(&g_nr_recovery_lock);
    // The missing receipt interrupts this exact stream now. Historical loss
    // remains visible, but three later complete carriers and a new stored read
    // may prove recovery without forcing an unrelated setting change.
    g_nr_recovery.InterruptScope(token.key.scope, token.key.role);
    g_nr_recovery_snapshot.receipt_overflow = true;
    ReleaseSRWLockExclusive(&g_nr_recovery_lock);
}
void NrRecoveryRetireSlot(NrSlot& slot, bool safe) {
    if (!safe || slot.delta_stats_pending || slot.recording_command_lists || slot.awaiting_fence_signal) return;
    AcquireSRWLockExclusive(&g_nr_recovery_lock);
    g_nr_recovery.RetireInactiveResource(slot.recovery_token.key, {true, true, true});
    ReleaseSRWLockExclusive(&g_nr_recovery_lock);
}
bool NrRecoveryNeedsProbe(const NrSlot& slot) {
    if (!slot.recovery_token) return false;
    AcquireSRWLockShared(&g_nr_recovery_lock);
    const auto proof = g_nr_recovery.Read(slot.recovery_token.key, slot.recovery_token.signature);
    ReleaseSRWLockShared(&g_nr_recovery_lock);
    // At most four extra sparse probes for this slot/interpretation; no extra
    // model calls. The regular periodic sample remains the fallback.
    return !proof.writeback_verified && proof.state != nr_recovery::State::Latched && slot.recovery_probe_count < 4;
}
bool NrRecoveryMayRecord(const NrSlot& slot) {
    if (!slot.recovery_token) return true;
    AcquireSRWLockShared(&g_nr_recovery_lock);
    const auto proof = g_nr_recovery.Read(slot.recovery_token.key, slot.recovery_token.signature);
    ReleaseSRWLockShared(&g_nr_recovery_lock);
    if (proof.state != nr_recovery::State::Latched) return true;
    if (auto* frame = g_nr_recovery_carrier) {
        auto& input = frame->snapshot.input;
        if (!input.first_block.unresolved)
            input.first_block = {true, true, input.current, NrRecoveryFailureReason(proof.first_failure), true};
    }
    SetBypassReason(BypassReason::RuntimeRejected);
    return false;
}
