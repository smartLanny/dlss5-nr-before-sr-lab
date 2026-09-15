// Included beside FeatureRecord and the existing ownership gate. Lock order:
// ownership gate -> chain -> NR API -> feature registry. The registry never
// calls NGX/COM, acquires another lock, or retires GPU resources while held.
#define NR_HAS_NGX_LIFECYCLE_EVIDENCE 1
extern std::atomic<unsigned long long> g_module_observer_epoch;
extern std::atomic<unsigned int> g_history_generation;
void RememberFeature(const NgxHandle *, int, const NgxParams *);
void MarkLifecycleUncertain(const char *);

std::atomic<unsigned int> g_external_nr_handles{0};
std::atomic<unsigned int> g_external_nr_creates{0};
std::atomic<unsigned long long> g_external_nr_evaluations{0};
std::atomic<unsigned int> g_ngx_pending_retirement_count{0};
std::atomic<unsigned long long> g_ngx_operation_sequence{0};
std::atomic<unsigned long long> g_feature_live_identity_conflicts{0};
std::atomic<unsigned long long> g_feature_registry_capacity_conflicts{0};
thread_local NgxLayer g_ngx_layer = NgxLayer::Unspecified;
thread_local unsigned int g_private_nr_call_depth = 0;

struct ScopedNgxLayer {
    NgxLayer previous = g_ngx_layer;
    explicit ScopedNgxLayer(NgxLayer layer) { g_ngx_layer = layer; }
    ~ScopedNgxLayer() { g_ngx_layer = previous; }
    ScopedNgxLayer(const ScopedNgxLayer &) = delete;
    ScopedNgxLayer &operator=(const ScopedNgxLayer &) = delete;
};

enum class PrivateNrOperation { Create, Evaluate, Release };
struct ScopedPrivateNrCall;
thread_local ScopedPrivateNrCall *g_private_nr_call = nullptr;
struct ScopedPrivateNrCall {
    ScopedPrivateNrCall *previous = g_private_nr_call;
    PrivateNrOperation operation;
    const NgxHandle *handle;
    ScopedPrivateNrCall(PrivateNrOperation op, const void *value = nullptr)
        : operation(op), handle(static_cast<const NgxHandle *>(value)) {
        g_private_nr_call = this;
        ++g_private_nr_call_depth;
    }
    ~ScopedPrivateNrCall() {
        --g_private_nr_call_depth;
        g_private_nr_call = previous;
    }
    ScopedPrivateNrCall(const ScopedPrivateNrCall &) = delete;
    ScopedPrivateNrCall &operator=(const ScopedPrivateNrCall &) = delete;
};

bool HasPrivateBridgeOrigin() {
    if (!g_private_nr_call || !g_private_nr_call->handle) return false;
    bool known = false;
    AcquireSRWLockShared(&g_feature_lock);
    for (const auto &r : g_features)
        if (r.handle == g_private_nr_call->handle && r.layer == NgxLayer::PrivateBridge &&
            r.owner == NgxOwner::PrivateNr && !r.pending && !r.retiring) known = true;
    ReleaseSRWLockShared(&g_feature_lock);
    return known;
}

struct NgxLifecycleEvent {
    unsigned long long sequence = 0, call = 0, parent = 0;
    unsigned long long handle_token = 0, generation = 0, epoch = 0;
    unsigned int layer = 0, owner = 0, operation = 0;
    int feature = -1;
    NgxResult result = 0;
};
struct NgxLifecycleEvidence {
    NgxLifecycleEvent recent[16] = {}, first_fault[16] = {};
    unsigned int recent_count = 0, fault_count = 0;
    unsigned long long events = 0;
    bool frozen = false;
};
NgxLifecycleEvidence g_ngx_lifecycle_evidence;
struct UnknownNgxToken { const NgxHandle *handle = nullptr; unsigned long long token = 0; };
UnknownNgxToken g_unknown_ngx_tokens[8] = {};
struct NgxContextRetirement {
    const NgxHandle *handle = nullptr;
    unsigned long long generation = 0;
};
NgxContextRetirement g_ngx_context_retirements[64] = {};

void QueueNgxContextRetirement(const NgxHandle *handle, unsigned long long generation) {
    if (!handle || !generation) return;
    bool queued = false;
    AcquireSRWLockExclusive(&g_feature_lock);
    NgxContextRetirement *free_slot = nullptr;
    for (auto &r : g_ngx_context_retirements) {
        if (r.handle == handle && r.generation == generation) { queued = true; break; }
        if (!r.handle && !free_slot) free_slot = &r;
    }
    if (!queued && free_slot) {
        *free_slot = {handle, generation};
        ++g_ngx_pending_retirement_count;
        queued = true;
    }
    ReleaseSRWLockExclusive(&g_feature_lock);
    if (!queued) MarkLifecycleUncertain("observed NGX retirement queue exhausted");
}

bool TakeNgxContextRetirement(NgxContextRetirement &result) {
    result = {};
    AcquireSRWLockExclusive(&g_feature_lock);
    for (auto &r : g_ngx_context_retirements) {
        if (!r.handle) continue;
        result = r;
        r = {};
        --g_ngx_pending_retirement_count;
        break;
    }
    ReleaseSRWLockExclusive(&g_feature_lock);
    return result.handle != nullptr;
}

unsigned long long UnknownNgxTokenLocked(const NgxHandle *handle) {
    if (!handle) return 0;
    for (const auto &r : g_unknown_ngx_tokens)
        if (r.handle == handle) return r.token;
    for (auto &r : g_unknown_ngx_tokens)
        if (!r.handle) { r = {handle, ++g_ngx_operation_sequence}; return r.token; }
    return 0; // Capacity is explicit; no raw pointer/hash fallback.
}
// Uses g_feature_lock, so evidence and identity are one coherent bounded view.
// Tokens are Create operation identities, never hashes/encodings of addresses.
void RecordNgxLifecycleLocked(unsigned int operation, const FeatureRecord &r,
                              NgxResult result = 0) {
    auto &e = g_ngx_lifecycle_evidence;
    NgxLifecycleEvent event{};
    event.sequence = ++e.events;
    event.call = r.create_call;
    event.parent = r.create_parent;
    event.handle_token = r.create_call;
    event.generation = r.generation;
    event.epoch = g_module_observer_epoch.load(std::memory_order_acquire);
    event.layer = static_cast<unsigned int>(g_ngx_layer);
    event.owner = static_cast<unsigned int>(r.owner);
    event.operation = operation;
    event.feature = r.feature_id;
    event.result = result;
    if (e.recent_count == std::size(e.recent)) {
        for (std::size_t i = 1; i < std::size(e.recent); ++i)
            e.recent[i - 1] = e.recent[i];
        --e.recent_count;
    }
    e.recent[e.recent_count++] = event;
}

void FreezeNgxLifecycleFault() {
    AcquireSRWLockExclusive(&g_feature_lock);
    auto &e = g_ngx_lifecycle_evidence;
    if (!e.frozen) {
        e.frozen = true;
        e.fault_count = e.recent_count;
        for (unsigned int i = 0; i < e.fault_count; ++i)
            e.first_fault[i] = e.recent[i];
    }
    ReleaseSRWLockExclusive(&g_feature_lock);
}

NgxLifecycleEvidence SnapshotNgxLifecycleEvidence() {
    AcquireSRWLockShared(&g_feature_lock);
    const auto result = g_ngx_lifecycle_evidence;
    ReleaseSRWLockShared(&g_feature_lock);
    return result;
}

void RecountNgxOwnersLocked() {
    unsigned int rr = 0, external = 0, fg = 0, reconstruction = 0;
    for (std::size_t i = 0; i < std::size(g_features); ++i) {
        const auto &r = g_features[i];
        if (!r.handle) continue;
        bool first = true;
        for (std::size_t j = 0; j < i; ++j)
            if (g_features[j].handle && g_features[j].generation == r.generation)
                first = false;
        if (!first) continue;
        if (r.feature_id == 13) ++rr;
        if (r.owner == NgxOwner::Game && r.feature_id == 11) ++fg;
        // Generated product cores connect Feature13 to ProcessRrPost; both
        // supported reconstruction carriers create CarrierScope only where a
        // real native After operation can be attempted.
        if (r.owner == NgxOwner::Game && (r.feature_id == 1 || r.feature_id == 13)) ++reconstruction;
        if (r.owner == NgxOwner::ExternalNr) ++external;
    }
    g_live_rr_handles.store(rr, std::memory_order_release);
    g_fg_presentation_policy.Inventory(fg,reconstruction);
    g_external_nr_handles.store(external, std::memory_order_release);
}

struct ScopedNgxCreate;
thread_local ScopedNgxCreate *g_ngx_create = nullptr;
struct ScopedNgxCreate {
    ScopedNgxCreate *parent = g_ngx_create;
    ScopedNgxLayer layer;
    unsigned long long call = ++g_ngx_operation_sequence;
    unsigned long long root = parent ? parent->root : call;
    unsigned long long epoch = g_module_observer_epoch.load(std::memory_order_acquire);
    ID3D12GraphicsCommandList *list;
    const NgxParams *params_identity;
    int feature;
    unsigned int depth = parent ? parent->depth + 1 : 1;
    bool private_nr;
    bool private_origin_uncertain;
    bool finished = false;
    nr_fg_presentation::Creating fg_creation{g_fg_presentation_policy,feature==11};
    ScopedNgxCreate(NgxLayer entry, ID3D12GraphicsCommandList *command_list, int id,
                    const NgxParams *params)
        : layer(entry), list(command_list), params_identity(params), feature(id),
          private_nr(id == 18 && g_private_nr_call &&
                     ((g_private_nr_call->operation == PrivateNrOperation::Create &&
                       (entry == NgxLayer::PrivateBridge || (parent && parent->private_nr))) ||
                      HasPrivateBridgeOrigin())),
          private_origin_uncertain(id == 18 && g_private_nr_call && !private_nr) {
        g_ngx_create = this;
        FeatureRecord entry_record{};
        entry_record.create_call = call;
        entry_record.create_parent = parent ? parent->call : 0;
        entry_record.feature_id = feature;
        entry_record.owner = feature == 18
            ? (private_nr ? NgxOwner::PrivateNr :
                (private_origin_uncertain ? NgxOwner::Uncertain : NgxOwner::ExternalNr))
            : NgxOwner::Game;
        AcquireSRWLockExclusive(&g_feature_lock);
        RecordNgxLifecycleLocked(7, entry_record);
        ReleaseSRWLockExclusive(&g_feature_lock);
        if (depth > 8)
            LatchRrOwnershipUncertain("NGX Create observation", "nested Create capacity exceeded");
        if (private_origin_uncertain)
            LatchRrOwnershipUncertain("private NGX Create observation", "nested Feature18 had no proven private bridge call origin");
    }
    ~ScopedNgxCreate() { g_ngx_create = parent; }
    ScopedNgxCreate(const ScopedNgxCreate &) = delete;
    ScopedNgxCreate &operator=(const ScopedNgxCreate &) = delete;
    void Finish(NgxResult result, const NgxHandle *handle, const NgxParams *params);
};

void PublishObservedFeature(FeatureRecord record) {
    const auto *call = g_ngx_create;
    record.layer = g_ngx_layer;
    record.owner = record.feature_id == 18
        ? (call && call->private_nr ? NgxOwner::PrivateNr :
            (call && call->private_origin_uncertain ? NgxOwner::Uncertain : NgxOwner::ExternalNr))
        : NgxOwner::Game;
    record.create_call = call ? call->call : ++g_ngx_operation_sequence;
    record.create_parent = call && call->parent ? call->parent->call : 0;
    record.create_root = call ? call->root : record.create_call;
    record.observer_epoch = call ? call->epoch : g_module_observer_epoch.load();
    record.create_list = call ? call->list : nullptr;
    record.pending = call != nullptr;
    record.context_handle = record.handle;
    if (!record.generation) {
        record.generation = g_feature_generation.fetch_add(1, std::memory_order_relaxed);
        if (!record.generation)
            record.generation = g_feature_generation.fetch_add(1, std::memory_order_relaxed);
    }
    bool live_identity_conflict = false;
    unsigned int active_slots = 0;
    AcquireSRWLockExclusive(&g_feature_lock);
    FeatureRecord *slot = nullptr;
    for (auto &r : g_features) {
        if (r.handle) ++active_slots;
        if (!r.handle && !slot) slot = &r;
        if (r.handle == record.handle && r.layer == record.layer) {
            // No successful Release: do not overwrite a live generation, even
            // for the same feature. A later Release snapshot must not erase it.
            live_identity_conflict = true;
        }
    }
    const bool registry_capacity_conflict = slot == nullptr;
    const bool nested_capacity_conflict = call != nullptr && call->depth > 8;
    const bool capacity_conflict = registry_capacity_conflict || nested_capacity_conflict;
    const bool conflict = live_identity_conflict || capacity_conflict;
    if (!conflict) *slot = record;
    RecordNgxLifecycleLocked(1, record, kNgxSuccess);
    RecountNgxOwnersLocked();
    ReleaseSRWLockExclusive(&g_feature_lock);
    if (!conflict && record.owner == NgxOwner::ExternalNr)
        g_history_generation.fetch_add(1, std::memory_order_acq_rel);
    if (live_identity_conflict) {
        const auto count = g_feature_live_identity_conflicts.fetch_add(
            1, std::memory_order_relaxed) + 1;
        Log("[ngx-lifecycle] Feature registry conflict: kind=live-identity "
            "count=%llu active=%u capacity=%zu layer=%u feature=%d owner=%u.",
            count, active_slots, std::size(g_features),
            static_cast<unsigned int>(record.layer), record.feature_id,
            static_cast<unsigned int>(record.owner));
    }
    if (capacity_conflict) {
        const auto count = g_feature_registry_capacity_conflicts.fetch_add(
            1, std::memory_order_relaxed) + 1;
        Log("[ngx-lifecycle] Feature registry conflict: kind=capacity "
            "count=%llu active=%u capacity=%zu nestedDepth=%u layer=%u feature=%d owner=%u.",
            count, active_slots, std::size(g_features),
            nested_capacity_conflict ? (call != nullptr ? call->depth : 0u) : 0u,
            static_cast<unsigned int>(record.layer), record.feature_id,
            static_cast<unsigned int>(record.owner));
    }
    if (conflict) {
        FreezeNgxLifecycleFault();
        LatchRrOwnershipUncertain("NGX Create observation", "live identity collision or bounded registry exhausted");
    }
}

bool SameObservedCreateContract(const FeatureRecord &a, const FeatureRecord &b) {
    // Exact same non-null list on the still-live nested call stack proves the
    // same device without assuming proxy pointer equivalence or querying COM
    // while the registry is locked. Different lists are deliberately not joined.
    return a.create_contract_known && b.create_contract_known &&
        a.create_params && a.create_params == b.create_params &&
        a.create_list && a.create_list == b.create_list &&
        (a.feature_id == 1 || a.feature_id == 18) &&
        a.feature_id == b.feature_id && a.owner == b.owner &&
        a.width == b.width && a.height == b.height &&
        a.out_width == b.out_width && a.out_height == b.out_height &&
        a.create_flags == b.create_flags && a.perf_quality == b.perf_quality &&
        a.observer_epoch == b.observer_epoch && !(a.observer_epoch & 1u);
}

void ScopedNgxCreate::Finish(NgxResult result, const NgxHandle *handle,
                            const NgxParams *params) {
    if (finished) return;
    finished = true;
    const bool succeeded = result == kNgxSuccess && handle;
    if (succeeded) RememberFeature(handle, feature, params);
    bool orphaned = false;
    AcquireSRWLockExclusive(&g_feature_lock);
    FeatureRecord *current = nullptr, *child = nullptr;
    unsigned int children = 0;
    for (auto &r : g_features) {
        if (!r.handle) continue;
        if (r.create_call == call) current = &r;
        if (r.create_parent == call && r.feature_id == feature) {
            child = &r;
            ++children;
        }
    }
    const auto completion_epoch = g_module_observer_epoch.load();
    if (current && children == 1 && child &&
        SameObservedCreateContract(*current, *child) &&
        completion_epoch == epoch) {
        const auto from_generation = child->generation;
        for (auto &r : g_features) {
            if (r.handle && r.generation == from_generation) {
                r.generation = current->generation;
                r.context_handle = current->handle;
            }
        }
        RecordNgxLifecycleLocked(2, *current, result);
    }
    if (!parent) {
        for (auto &r : g_features) {
            if (!r.handle || r.create_root != root) continue;
            if (!succeeded || (epoch & 1u) || epoch != completion_epoch) orphaned = true;
            r.pending = !succeeded || (epoch & 1u) || epoch != completion_epoch;
            r.create_list = nullptr;
            r.create_params = nullptr;
        }
    }
    if (!succeeded) {
        FeatureRecord failed{};
        failed.create_call = call;
        failed.create_parent = parent ? parent->call : 0;
        failed.feature_id = feature;
        RecordNgxLifecycleLocked(3, failed, result);
    }
    RecountNgxOwnersLocked();
    ReleaseSRWLockExclusive(&g_feature_lock);
    if (orphaned) {
        FreezeNgxLifecycleFault();
        LatchRrOwnershipUncertain("NGX Create observation", "successful inner lifetime outlived a failed or uncovered outer Create");
    }
}

FeatureRecord FindObservedFeature(const NgxHandle *handle) {
    FeatureRecord result{};
    if (!handle) return result;
    AcquireSRWLockShared(&g_feature_lock);
    for (const auto &r : g_features) {
        if (r.handle != handle || r.layer != g_ngx_layer) continue;
        result = r;
        break;
    }
    ReleaseSRWLockShared(&g_feature_lock);
    return result;
}

void NrInvalidateFormatFeatureObservations(unsigned long long feature) noexcept;
void ForgetObservedFeature(const NgxHandle *handle, unsigned long long generation) {
    unsigned long long retired_generation = 0;
    AcquireSRWLockExclusive(&g_feature_lock);
    for (auto &r : g_features) {
        if (r.handle != handle || r.layer != g_ngx_layer ||
            (generation && r.generation != generation)) continue;
        retired_generation = r.generation;
        RecordNgxLifecycleLocked(5, r, kNgxSuccess);
        r = {};
        // One layer's Release does not prove every related wrapper disappeared.
        // Keep remaining aliases native-only until their own observed Release.
        for (auto &alias : g_features)
            if (alias.handle && alias.generation == retired_generation)
                alias.retiring = true;
        break;
    }
    RecountNgxOwnersLocked();
    ReleaseSRWLockExclusive(&g_feature_lock);
    if (retired_generation) NrInvalidateFormatFeatureObservations(retired_generation);
    nr_source_runtime::RetireFeature(handle,generation);
}

void ObserveNgxEvaluate(const NgxHandle *handle) {
    auto record = FindObservedFeature(handle);
    // Cold first-evaluate records only; do not turn the hot path into a log.
    if (record.handle) {
        if (record.owner == NgxOwner::ExternalNr)
            g_external_nr_evaluations.fetch_add(1, std::memory_order_relaxed);
        if (record.evaluate_observed) return;
    }
    AcquireSRWLockExclusive(&g_feature_lock);
    if (record.handle) {
        for (auto &r : g_features)
            if (r.create_call == record.create_call) {
                if (r.evaluate_observed) { ReleaseSRWLockExclusive(&g_feature_lock); return; }
                r.evaluate_observed = true;
            }
    } else record.create_call = UnknownNgxTokenLocked(handle);
    RecordNgxLifecycleLocked(4, record);
    ReleaseSRWLockExclusive(&g_feature_lock);
}

void ObserveNgxRelease(const NgxHandle *handle) {
    auto record = FindObservedFeature(handle);
    AcquireSRWLockExclusive(&g_feature_lock);
    if (!record.handle) record.create_call = UnknownNgxTokenLocked(handle);
    RecordNgxLifecycleLocked(6, record);
    ReleaseSRWLockExclusive(&g_feature_lock);
}

bool IsPrivateNrEntry(PrivateNrOperation operation, const NgxHandle *handle) {
    if (!g_private_nr_call || g_private_nr_call->operation != operation) return false;
    const auto record = FindObservedFeature(handle);
    // A bridge's public handle and its observed inner handle may differ. Only
    // a proven private record can suppress the lifecycle guard in a hook.
    return record.handle && record.owner == NgxOwner::PrivateNr;
}
