// Exposure is an existing role-specific adapter. Publish its actual result in
// the same semantic schema; do not re-run a second, divergent format policy.
#define NR_HAS_EXPOSURE_SEMANTIC_EVIDENCE 1
unsigned long long NrFormatResourceIdentity(ID3D12Resource*);
unsigned long long NrFormatDeviceIdentity(ID3D12Device*);
struct NrExposureSemanticEvidence {
    bool available = false, present = false, required = false;
    unsigned physical = 0, flags = 0, width = 0, height = 0, mips = 0, samples = 0;
    unsigned config = 0;
    int mode = 0;
    unsigned long long device = 0, resource = 0, feature = 0;
    NrExposureAccess access{};
    nr_resource_semantics::Decision semantic{};
};
SRWLOCK g_exposure_semantic_lock = SRWLOCK_INIT;
NrExposureSemanticEvidence g_exposure_semantic_evidence;
void NrRecordExposureSemantic(ID3D12Device* device, ID3D12Resource* resource,
        const D3D12_RESOURCE_DESC& desc, const FeatureRecord& feature,
        int mode, bool required, const NrExposureAccess& access) {
    using namespace nr_resource_semantics;
    NrExposureSemanticEvidence e;
    e.available = true; e.present = resource != nullptr; e.required = required;
    e.physical = static_cast<unsigned>(desc.Format); e.flags = static_cast<unsigned>(desc.Flags);
    e.width = static_cast<unsigned>(desc.Width); e.height = desc.Height;
    e.mips = desc.MipLevels; e.samples = desc.SampleDesc.Count;
    e.config = g_config_generation.load(std::memory_order_relaxed);
    e.mode = mode; e.feature = feature.generation;
    e.device = NrFormatDeviceIdentity(device); e.resource = NrFormatResourceIdentity(resource);
    e.access = access;
    auto& d = e.semantic;
    d.contract = Contract::ExposureScale;
    if (!required) {
        d.verdict = Verdict::Admit; d.definitive = true; d.access.status = nr_format_access::Status::Ready;
        d.reason = resource ? "HDR-exposure-texture-unused-by-declared-LDR" : "no-exposure-texture-supplied";
        d.missing_evidence = "none"; d.next_action = "none";
    } else {
        d.source = access.view == desc.Format ? Source::TypedResource :
                   access.view != DXGI_FORMAT_UNKNOWN ? Source::UniqueContract : Source::None;
        d.access.srv = static_cast<unsigned>(access.view);
        if (access.view != DXGI_FORMAT_UNKNOWN) {
            d.candidate_count = 1; d.remaining_count = 1;
            auto& c = d.candidates[0]; c.view = static_cast<unsigned>(access.view);
            c.layout_legal = true; c.codec_implemented = true;
            c.capability.queried = access.query_attempted;
            c.capability.result = static_cast<std::int32_t>(access.query_result);
            c.capability.texture2d = (access.support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D) != 0;
            c.capability.load = (access.support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD) != 0;
        }
        if (access.allowed()) {
            d.verdict = Verdict::Admit; d.definitive = true; d.access.status = nr_format_access::Status::Ready;
            d.reason = "final-exposure-scale-read-resolved";
            d.missing_evidence = "none"; d.next_action = "none";
        } else {
            const bool incomplete = access.reason == NrExposureAccessReason::MissingDevice ||
                (access.reason == NrExposureAccessReason::Capability &&
                 (!access.query_attempted || FAILED(access.query_result)));
            Reject(d, incomplete ? Verdict::CapabilityUnknown : Verdict::Unsupported,
                access.reason == NrExposureAccessReason::Capability && FAILED(access.query_result) && access.query_attempted
                    ? "exposure-device-query-failed" : NrExposureAccessReasonName(access.reason),
                incomplete ? "successful-exposure-device-capability-query" : "none", !incomplete);
        }
    }
    AcquireSRWLockExclusive(&g_exposure_semantic_lock);
    g_exposure_semantic_evidence = e;
    ReleaseSRWLockExclusive(&g_exposure_semantic_lock);
}
NrExposureSemanticEvidence SnapshotNrExposureSemantic() {
    AcquireSRWLockShared(&g_exposure_semantic_lock);
    const auto e = g_exposure_semantic_evidence;
    ReleaseSRWLockShared(&g_exposure_semantic_lock);
    return e;
}
