// Included after the native resource observer. Locks below protect values only;
// resource queries and recovery reads are outside the route lock.
SRWLOCK g_nr_route_lock = SRWLOCK_INIT;
nr_route::Policy g_nr_route;
struct NrRouteCarrierScope;
thread_local NrRouteCarrierScope* g_nr_route_carrier = nullptr;

nr_route::Snapshot NrRouteRead() {
    AcquireSRWLockShared(&g_nr_route_lock);
    const auto value = g_nr_route.Read();
    ReleaseSRWLockShared(&g_nr_route_lock);
    return value;
}
void NrRouteInvalidate(const char* reason) {
    AcquireSRWLockExclusive(&g_nr_route_lock);
    g_nr_route.Invalidate(reason);
    ReleaseSRWLockExclusive(&g_nr_route_lock);
}
void NrRouteRetireFeature(unsigned long long generation) {
    AcquireSRWLockExclusive(&g_nr_route_lock);
    if (g_nr_route.Read().scope.feature == generation) g_nr_route.Invalidate("feature-retired");
    ReleaseSRWLockExclusive(&g_nr_route_lock);
}
void NrRouteRefreshProof(const nr_recovery::Token& token) {
    if (!token || token.key.scope.mode != 1 || token.key.role != 1) return;
    AcquireSRWLockShared(&g_nr_recovery_lock);
    const auto proof = g_nr_recovery.Read(token.key, token.signature);
    ReleaseSRWLockShared(&g_nr_recovery_lock);
    AcquireSRWLockExclusive(&g_nr_route_lock);
    g_nr_route.Refresh(token, proof);
    ReleaseSRWLockExclusive(&g_nr_route_lock);
}
void NrRouteRecoveryStage(const nr_recovery::Token& token, nr_recovery::Stage stage, bool success) {
    AcquireSRWLockExclusive(&g_nr_route_lock);
    g_nr_route.Stage(token, stage, success);
    ReleaseSRWLockExclusive(&g_nr_route_lock);
    NrRouteRefreshProof(token);
}
nr_route::Snapshot SnapshotNrRoute() {
    auto value = NrRouteRead();
    if (value.current && (value.scope.config != g_config_generation.load(std::memory_order_acquire) ||
        value.scope.requested != static_cast<unsigned>(g_requested_mode_fast.load(std::memory_order_acquire)) ||
        !g_enabled_fast.load(std::memory_order_acquire) ||
        g_feature_tracking_uncertain.load(std::memory_order_acquire) ||
        g_lifecycle_tracking_uncertain.load(std::memory_order_acquire) || IsRayReconstructionRouteObserved())) {
        NrRouteInvalidate("current-scope-unavailable");
        return NrRouteRead();
    }
    NrRouteRefreshProof(value.proof_token);
    return NrRouteRead();
}
bool NrRoutePostLocked() {
    const auto value = SnapshotNrRoute();
    return value.current && value.locked;
}

nr_route::Scope NrRouteScope(const NgxParams* params, const FeatureRecord& feature) {
    nr_route::Scope key{};
    if (!params || feature.feature_id != 1 || !g_nr_recovery_carrier) return key;
    key.device = g_nr_recovery_carrier->scopes[1].device;
    key.feature = feature.generation;
    key.config = g_config_generation.load(std::memory_order_acquire);
    key.requested = static_cast<unsigned>(g_requested_mode_fast.load(std::memory_order_acquire));
    ID3D12Resource* resources[2]{};
    GetResource(params, "Color", &resources[0]); GetResource(params, "Output", &resources[1]);
    key.color = NrFormatResourceIdentity(resources[0]); key.output = NrFormatResourceIdentity(resources[1]);
    for (unsigned role = 0; role != 2; ++role) {
        if (!resources[role]) continue;
        const auto desc = ID3D12Resource_GetDesc(resources[role]);
        const auto evidence = NrFormatEvidence({key.device, role ? key.output : key.color, key.feature,
            role ? nr_native_format::Role::Output : nr_native_format::Role::Color});
        const unsigned at = role * 12;
        key.contract[at] = desc.Format; key.contract[at + 1] = desc.Flags;
        key.contract[at + 2] = desc.Dimension; key.contract[at + 3] = desc.Width;
        key.contract[at + 4] = desc.Height; key.contract[at + 5] = desc.DepthOrArraySize;
        key.contract[at + 6] = desc.MipLevels; key.contract[at + 7] = desc.SampleDesc.Count;
        key.contract[at + 8] = desc.SampleDesc.Quality; key.contract[at + 9] = desc.Layout;
        key.contract[at + 10] = evidence.Resolved();
        key.contract[at + 11] = unsigned(evidence.view.conflict) | (unsigned(evidence.failed) << 1);
    }
    key.contract[24] = static_cast<unsigned>(feature.create_flags);
    key.contract[25] = feature.create_flags_known;
    const char* regions[] = {"DLSS.Input.Color.Subrect.Base.X", "DLSS.Input.Color.Subrect.Base.Y",
        "DLSS.Output.Subrect.Base.X", "DLSS.Output.Subrect.Base.Y",
        "DLSS.Render.Subrect.Dimensions.Width", "DLSS.Render.Subrect.Dimensions.Height",
        "DLSS.Input.Depth.Subrect.Base.X", "DLSS.Input.Depth.Subrect.Base.Y",
        "DLSS.Input.MV.Subrect.Base.X", "DLSS.Input.MV.Subrect.Base.Y"};
    for (unsigned i = 0; i != 10; ++i) {
        unsigned value = 0;
        if (GetUInt(params, regions[i], &value)) key.contract[26 + i] = (1ull << 32) | value;
    }
    const char* guides[] = {"Depth", "MotionVectors", "ExposureTexture"};
    for (unsigned i = 0; i != 3; ++i) {
        ID3D12Resource* resource = nullptr;
        if (GetResource(params, guides[i], &resource)) key.contract[36 + i] = NrFormatResourceIdentity(resource);
    }
    const char* scalars[] = {"DLSS.Pre.Exposure", "DLSS.Exposure.Scale", "MV.Scale.X", "MV.Scale.Y"};
    for (unsigned i = 0; i != 4; ++i) {
        float value = 0; unsigned bits = 0;
        if (GetFloat(params, scalars[i], &value)) {
            std::memcpy(&bits, &value, sizeof(bits)); key.contract[39 + i] = (1ull << 32) | bits;
        }
    }
    return key;
}

struct NrRouteCarrierScope {
    NrRouteCarrierScope* parent = nullptr;
    const NgxParams* params = nullptr;
    FeatureRecord feature{};
    std::uint64_t frame = 0;
    bool begun = false;
    bool pre_semantic_seen = false, post_semantic_seen = false;
    nr_resource_semantics::Decision pre_semantic{}, post_semantic{};
    NrRouteCarrierScope(const NgxParams* p, const FeatureRecord& f) : params(p), feature(f) {
        parent = g_nr_route_carrier; g_nr_route_carrier = this;
        frame = g_nr_recovery_carrier ? g_nr_recovery_carrier->snapshot.frame : 0;
        const auto key = NrRouteScope(params, feature);
        AcquireSRWLockExclusive(&g_nr_route_lock);
        if (!(g_nr_route.Read().scope == key)) g_nr_route.Invalidate("scope-changed");
        ReleaseSRWLockExclusive(&g_nr_route_lock);
    }
    void Reconcile() {
        const auto key = NrRouteScope(params, feature);
        AcquireSRWLockExclusive(&g_nr_route_lock);
        g_nr_route.Begin(key, frame);
        begun = true;
        const auto token = g_nr_route.Read().proof_token;
        ReleaseSRWLockExclusive(&g_nr_route_lock);
        NrRouteRefreshProof(token);
    }
    ~NrRouteCarrierScope() {
        AcquireSRWLockExclusive(&g_nr_route_lock);
        if (begun && g_nr_route.Read().frame == frame) g_nr_route.End();
        else if (!begun) g_nr_route.Invalidate("carrier-bypassed");
        ReleaseSRWLockExclusive(&g_nr_route_lock);
        g_nr_route_carrier = parent;
    }
};
void NrRouteSemanticObserved(const nr_resource_semantics::Decision& decision, bool post) {
    if (auto* frame = g_nr_route_carrier) {
        if (post) { frame->post_semantic = decision; frame->post_semantic_seen = true; }
        else { frame->pre_semantic = decision; frame->pre_semantic_seen = true; }
    }
}
void NrRoutePreRejected(bool definitive, const char* reason) {
    if (!g_nr_route_carrier) return;
    AcquireSRWLockExclusive(&g_nr_route_lock);
    if (g_nr_route.Read().frame == g_nr_route_carrier->frame) g_nr_route.PreRejected(definitive, reason);
    ReleaseSRWLockExclusive(&g_nr_route_lock);
}
void NrRoutePreAdmitted() {
    if (!g_nr_route_carrier) return;
    AcquireSRWLockExclusive(&g_nr_route_lock);
    if (g_nr_route.Read().frame == g_nr_route_carrier->frame) g_nr_route.PreAdmitted();
    ReleaseSRWLockExclusive(&g_nr_route_lock);
}
void NrRouteAttemptPost() {
    if (!g_nr_route_carrier) return;
    AcquireSRWLockExclusive(&g_nr_route_lock);
    if (g_nr_route.Read().frame == g_nr_route_carrier->frame) g_nr_route.AttemptPost();
    ReleaseSRWLockExclusive(&g_nr_route_lock);
}
void NrRoutePostPrepared(bool admitted, const nr_recovery::Token& token) {
    if (!g_nr_route_carrier) return;
    const auto& frame = *g_nr_route_carrier;
    AcquireSRWLockExclusive(&g_nr_route_lock);
    if (g_nr_route.Read().frame == frame.frame) {
        if (admitted) g_nr_route.AdmitPost(token);
        else g_nr_route.BlockPost(frame.post_semantic_seen && !frame.post_semantic ?
            frame.post_semantic.reason : "post-not-admitted");
    }
    ReleaseSRWLockExclusive(&g_nr_route_lock);
}
void NrRoutePostFinished(bool recorded) {
    if (!g_nr_route_carrier) return;
    AcquireSRWLockExclusive(&g_nr_route_lock);
    const auto prior = g_nr_route.Read();
    if (prior.frame == g_nr_route_carrier->frame) g_nr_route.RecordedPost(recorded, prior.post_reason);
    ReleaseSRWLockExclusive(&g_nr_route_lock);
}
void NrRouteBypass(BypassReason reason) {
    if (reason == BypassReason::None) return;
    if (auto* frame = g_nr_route_carrier) {
        AcquireSRWLockExclusive(&g_nr_route_lock);
        const auto value = g_nr_route.Read();
        if (value.frame != frame->frame) g_nr_route.Invalidate("concurrent-carrier-bypassed");
        else if (value.post_attempted) {
            const bool format_reason = reason == BypassReason::UnsupportedOutputFormat ||
                reason == BypassReason::OutputColorContractUnverified;
            g_nr_route.BlockPost(format_reason && frame->post_semantic_seen && !frame->post_semantic ?
                frame->post_semantic.reason : BypassReasonName(reason));
        }
        ReleaseSRWLockExclusive(&g_nr_route_lock);
    } else NrRouteInvalidate(BypassReasonName(reason));
}
