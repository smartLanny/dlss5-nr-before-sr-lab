#include "nr_native_clear_request.h"
// Included after the format adapter and before storage admission. The registry
// lock covers value records only; all D3D12 calls occur outside it.
const GUID kNrFormatLifetime = {0x61381092,0x6551,0x4f8e,{0xa1,0x35,0x63,0x71,0x20,0x5a,0x47,0x01}};
const GUID kNrFormatDevice = {0x61381093,0x6551,0x4f8e,{0xa1,0x35,0x63,0x71,0x20,0x5a,0x47,0x01}};
std::atomic<unsigned long long> g_nr_format_next_identity{1};
std::atomic<unsigned long long> g_nr_format_calls{0};
SRWLOCK g_nr_format_lock = SRWLOCK_INIT;
nr_native_format::Ledger<> g_nr_format_ledger;
struct NrFormatDeviceReceipt { unsigned long long identity = 0; bool hooks = false; };

unsigned long long NrFormatResourceIdentity(ID3D12Resource* resource) {
    if (!resource) return 0;
    unsigned long long value = 0;
    UINT bytes = sizeof(value);
    if (SUCCEEDED(ID3D12Resource_GetPrivateData(resource, kNrFormatLifetime, &bytes, &value)) &&
        bytes == sizeof(value) && value) return value;
    value = g_nr_format_next_identity.fetch_add(1, std::memory_order_relaxed);
    if (FAILED(ID3D12Resource_SetPrivateData(resource, kNrFormatLifetime, sizeof(value), &value))) return 0;
    // A racing observer may assign another token. Only the value actually held
    // by this COM lifetime is authoritative; displaced entries cannot be found.
    bytes = sizeof(value);
    return SUCCEEDED(ID3D12Resource_GetPrivateData(resource, kNrFormatLifetime, &bytes, &value)) &&
        bytes == sizeof(value) ? value : 0;
}
NrFormatDeviceReceipt NrFormatGetDevice(ID3D12Device* device) {
    NrFormatDeviceReceipt receipt{};
    UINT bytes = sizeof(receipt);
    if (!device || FAILED(ID3D12Device_GetPrivateData(device, kNrFormatDevice, &bytes, &receipt)) ||
        bytes != sizeof(receipt)) return {};
    return receipt;
}
unsigned long long NrFormatDeviceIdentity(ID3D12Device* device) {
    return NrFormatGetDevice(device).identity;
}
bool EnsureNrNativeFormatHooks(ID3D12Device* device);
void OnNrFormatInitDevice(reshade::api::device* device);

nr_native_format::Key NrFormatKey(ID3D12Device* device, ID3D12Resource* resource,
                                  unsigned long long generation, bool post_sr) {
    const auto receipt = NrFormatGetDevice(device);
    if (!receipt.identity) return {};
    return {receipt.identity, NrFormatResourceIdentity(resource), generation,
            post_sr ? nr_native_format::Role::Output : nr_native_format::Role::Color};
}
nr_native_format::Evidence NrFormatEvidence(const nr_native_format::Key& key) {
    AcquireSRWLockShared(&g_nr_format_lock);
    const auto value = g_nr_format_ledger.Find(key);
    ReleaseSRWLockShared(&g_nr_format_lock);
    return value;
}

struct NrNativeFormatScope {
    NrNativeFormatScope* parent = nullptr;
    ID3D12Device* device = nullptr;
    ID3D12Resource* resources[2] = {};
    nr_native_format::Key keys[2] = {};
    nr_native_format::View views[2] = {};
    unsigned long long call = 0;
    bool active = false;
    NrNativeFormatScope(ID3D12GraphicsCommandList*, const NgxParams*, unsigned long long);
    void Finish(bool success);
    ~NrNativeFormatScope();
};
thread_local NrNativeFormatScope* g_nr_native_format_scope = nullptr;
NrNativeFormatScope::NrNativeFormatScope(ID3D12GraphicsCommandList* list,
        const NgxParams* params, unsigned long long generation) {
    parent = g_nr_native_format_scope;
    g_nr_native_format_scope = nullptr; // Invalid/nested calls cannot teach the parent.
    if (!list || !params || !generation ||
        FAILED(ID3D12GraphicsCommandList_GetDevice(list, IID_ID3D12Device,
            reinterpret_cast<void**>(&device))) || !device) return;
    if (!NrFormatGetDevice(device).hooks) return;
    GetResource(params, "Color", &resources[0]);
    GetResource(params, "Output", &resources[1]);
    for (unsigned i = 0; i != 2; ++i)
        keys[i] = NrFormatKey(device, resources[i], generation, i != 0);
    call = g_nr_format_calls.fetch_add(1, std::memory_order_relaxed) + 1;
    active = true;
    g_nr_native_format_scope = this;
}
void NrNativeFormatScope::Finish(bool success) {
    if (!active) return;
    g_nr_native_format_scope = parent;
    AcquireSRWLockExclusive(&g_nr_format_lock);
    for (unsigned i = 0; i != 2; ++i)
        g_nr_format_ledger.Commit(keys[i], views[i], call, success);
    ReleaseSRWLockExclusive(&g_nr_format_lock);
    active = false;
}
NrNativeFormatScope::~NrNativeFormatScope() {
    Finish(false);
    g_nr_native_format_scope = parent;
    if (device) ID3D12Device_Release(device);
}
#if defined(NR_HAS_DESCRIPTOR_PROVENANCE)
#include "nr_descriptor_provenance_runtime.inl"
#endif
#include "nr_native_clear_request_runtime.inl"
template <class NativeEvaluate>
NgxResult NrCallNativeWithFormatObservation(NativeEvaluate original,
        ID3D12GraphicsCommandList* list, const NgxHandle* handle,
        const NgxParams* params, void* callback, unsigned long long generation,
        nr_native_clear::Invocation* clear_invocation = nullptr,
        const nr_native_clear::Receipt* prepared_clear = nullptr) {
    if(clear_invocation)*clear_invocation={};
#if !defined(NR_HAS_DESCRIPTOR_PROVENANCE)
    (void)prepared_clear;
#endif
#if defined(NR_HAS_DESCRIPTOR_PROVENANCE)
    NrPostBoundaryAliasScope list_alias(list);
#endif
    NrNativeFormatScope scope(list, params, generation);
#if defined(NR_HAS_DESCRIPTOR_PROVENANCE)
    NrDescriptorNativeUseScope descriptor_use(scope,list,generation);
    NrNativeClearScope clear_scope(scope,list,prepared_clear);
#endif
#if defined(NR_HAS_TIMING)
    NrTimingScope native_timing(nr_timing::Stage::NativeEvaluate);
#endif
    const NgxResult result = original(list, handle, params, callback);
#if defined(NR_HAS_DESCRIPTOR_PROVENANCE)
    descriptor_use.Finish(result==kNgxSuccess);
#if defined(NR_HAS_POST_BOUNDARY_TRACE)
    if(g_native_output_trace && g_native_output_trace->resource==
            reinterpret_cast<std::uint64_t>(scope.resources[1])) {
        const auto use=descriptor_use.call?SnapshotNrDescriptorUse(descriptor_use.call):nr_descriptor::NativeUse{};
        g_native_output_trace->evidence.AttachUse(use,scope.keys[1].device,
            scope.keys[1].feature,scope.keys[1].resource);
    }
#endif
    const auto clear_result=clear_scope.Finish(result==kNgxSuccess);
    if(clear_invocation)*clear_invocation=clear_result;
#endif
#if defined(NR_HAS_TIMING)
    NrTimingNativeResult(static_cast<std::int32_t>(result));
    native_timing.Stop();
#endif
    scope.Finish(result == kNgxSuccess);
#if defined(NR_HAS_RESOURCE_RECOVERY)
    NrRecoveryNativeResult(result);
#endif
    return result;
}
void NrPrepareFormatObservation(ID3D12GraphicsCommandList* list) {
    ID3D12Device* device = nullptr;
    if (list && SUCCEEDED(ID3D12GraphicsCommandList_GetDevice(list, IID_ID3D12Device,
            reinterpret_cast<void**>(&device))) && device) {
        EnsureNrNativeFormatHooks(device);
#if defined(NR_HAS_DESCRIPTOR_PROVENANCE)
        EnsureNrDescriptorHooks(device);
        EnsureNrDescriptorCommandHooks(list);
        const auto identity=ResolvePostBoundaryListForObservation(list);
        if(identity.known && identity.address!=reinterpret_cast<std::uint64_t>(list))
            EnsureNrDescriptorCommandHooks(reinterpret_cast<ID3D12GraphicsCommandList *>(identity.address));
#endif
        ID3D12Device_Release(device);
    }
}

struct NrResolvedColorView {
    D3D12_RESOURCE_DESC typed{};
    nr_native_format::Key key{};
    nr_native_format::Evidence evidence{};
    nr_native_clear::Receipt clear_request{};
    bool clear_source = false;
    nr_format_access::Plan plan{};
    nr_resource_semantics::Decision semantic{};
    PreSrStorageContract pre_storage = PreSrStorageContract::Unsupported;
    PostSrStorageContract post_storage = PostSrStorageContract::Unsupported;
    bool native_bounded_output = false;
    explicit operator bool() const noexcept { return static_cast<bool>(semantic); }
};
struct NrColorSemanticContract {
    bool flags_known = true;
    bool live_bounded_output = false;
    ColorContract exposure{};
};
struct NrFormatDiagnostic {
    unsigned physical = 0, srv = 0, uav = 0, config = 0;
    unsigned long long feature = 0, device = 0, resource = 0, observed_call = 0;
    nr_format_access::Status status = nr_format_access::Status::AmbiguousType;
    nr_format_access::Provenance provenance = nr_format_access::Provenance::None;
    bool available = false, coverage = false, conflict = false, native_failed = false, clear_source = false;
    unsigned flags = 0, width = 0, height = 0, arrays = 0, mips = 0, samples = 0;
    nr_native_format::View observed{};
    nr_resource_semantics::Decision semantic{};
};
struct NrFormatDiagnostics {
    NrFormatDiagnostic latest[2]{}, first_unknown[2]{};
};
NrFormatDiagnostics g_nr_format_diagnostics;
NrFormatDiagnostics SnapshotNrFormatDiagnostics() {
    AcquireSRWLockShared(&g_nr_format_lock);
    const auto copy = g_nr_format_diagnostics;
    ReleaseSRWLockShared(&g_nr_format_lock);
    return copy;
}
#if defined(NR_HAS_RESOURCE_RECOVERY)
#include "nr_resource_recovery_semantics.inl"
#else
NrResolvedColorView NrRecoveryResolve(NrResolvedColorView result, bool) { return result; }
#endif
NrResolvedColorView NrResolveColorView(ID3D12Device* device, ID3D12Resource* resource,
        const D3D12_RESOURCE_DESC& desc, unsigned long long generation, bool post_sr,
        int feature_create_flags, const NrColorSemanticContract* color = nullptr) {
    using namespace nr_resource_semantics;
    NrResolvedColorView result{};
    result.typed = desc;
    result.key = NrFormatKey(device, resource, generation, post_sr);
    result.evidence = NrFormatEvidence(result.key);
    const NrColorSemanticContract default_contract{};
    const auto& contract = color ? *color : default_contract;
    Request request{};
    request.role = post_sr ? Role::Output : Role::ColorInput;
    request.storage = static_cast<unsigned>(desc.Format);
    request.observed_view = result.evidence.Resolved();
    request.observation_conflict = result.evidence.view.conflict;
    request.observation_stale = result.evidence.failed;
    request.lifecycle_known = static_cast<bool>(result.key);
    request.shape_valid = desc.Width && desc.Height &&
        desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
        desc.DepthOrArraySize == 1 && desc.MipLevels == 1 && desc.SampleDesc.Count == 1;
    request.deny_read = (desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) != 0;
    request.allow_write = (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0;
    request.exact_copy_snapshot = post_sr && desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
    request.contract_known = contract.flags_known;
    request.contract = !contract.flags_known ? Contract::None :
        (feature_create_flags & 1) ? Contract::HdrDeclared : Contract::LdrEncoded;
    if (!post_sr && (request.storage == nr_format_access::Rgba16Typeless ||
                     request.storage == nr_format_access::Rgba32Typeless)) {
        request.contract = Contract::SceneFloat;
        request.contract_known = true; // Existing explicit FLOAT adapter contract.
    }
    const auto capabilities = [device](unsigned read, unsigned write) {
            Capability capability{};
            if (!device) return capability;
            D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
            support.Format = static_cast<DXGI_FORMAT>(read);
            capability.queried = true;
            capability.result = static_cast<std::int32_t>(ID3D12Device_CheckFeatureSupport(
                device, D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)));
            if (!capability.known()) return capability;
            capability.texture2d = (support.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D) != 0;
            capability.load = (support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD) != 0;
            if (write && write != read) {
                support = {}; support.Format = static_cast<DXGI_FORMAT>(write);
                capability.result = static_cast<std::int32_t>(ID3D12Device_CheckFeatureSupport(
                    device, D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)));
                if (!capability.known()) return capability;
            }
            capability.store = !write ||
                ((support.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) != 0 &&
                 (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) != 0);
            return capability;
        };
    result.semantic = ResourceSemanticResolver::Resolve(request,capabilities);
    if(post_sr) {
        result.clear_request=NrNativeClearCandidate(result.key,g_config_generation.load(std::memory_order_acquire));
        if(result.clear_request) {
            if(request.observed_view && request.observed_view!=result.clear_request.format) {
                request.observation_conflict=result.evidence.view.conflict=true;
                result.semantic=ResourceSemanticResolver::Resolve(request,capabilities);
            } else if(!request.observed_view && result.semantic.verdict==Verdict::Ambiguous &&
                      !request.observation_conflict && !request.observation_stale) {
                request.observed_view=result.clear_request.format;
                result.clear_source=true;
                result.semantic=ResourceSemanticResolver::Resolve(request,capabilities);
            }
        }
    }
    result.plan = result.semantic.access;
    result.typed.Format = static_cast<DXGI_FORMAT>(result.plan.srv);
    result.native_bounded_output = post_sr && contract.flags_known &&
        (feature_create_flags & 1) == 0 && result.plan.srv == nr_format_access::Rgba8Unorm;
    if (!result.semantic) return NrRecoveryResolve(result, post_sr);
    result.pre_storage = ClassifyPreSrStorage(result.typed.Format, feature_create_flags);
    result.post_storage = ClassifyPostSrStorage(result.typed.Format, feature_create_flags,
                                               result.native_bounded_output);
    const bool usable = post_sr ? IsUsablePostSrStorage(result.post_storage) :
                                  IsUsablePreSrStorage(result.pre_storage);
    const bool r8 = post_sr ? result.post_storage == PostSrStorageContract::R8NativeBounded :
                             result.pre_storage == PreSrStorageContract::R8BoundedCandidate;
    const bool pq = post_sr ? result.post_storage == PostSrStorageContract::R10PqHdr :
                             result.pre_storage == PreSrStorageContract::R10PqHdr;
    if (!usable) {
        Reject(result.semantic, Verdict::Unsupported, "selected-view-color-contract-unimplemented",
               "explicit-supported-color-domain-or-codec-adapter");
    } else if ((r8 || pq) && !contract.flags_known) {
        Reject(result.semantic, Verdict::MissingContract, "DLSS-color-create-flags-not-observed",
               "DLSS.Feature.Create.Flags-from-this-Feature-Create", false);
    } else if (r8 && !IsPreSrBoundedR8Contract(contract.exposure,
                   (feature_create_flags & 1) == 0, contract.live_bounded_output)) {
        Reject(result.semantic, Verdict::MissingContract, "bounded-R8-color-and-exposure-contract-conflict",
               (feature_create_flags & 1) ? "explicit-unity-scalars-and-bounded-live-output" :
                                            "consistent-LDR-exposure-scalars", false);
    } else if (pq && !IsPqDisplayExposureContract(contract.exposure)) {
        Reject(result.semantic, Verdict::MissingContract, "PQ-display-and-exposure-contract-conflict",
               "consistent-display-PQ-exposure-contract", false);
    }
    return NrRecoveryResolve(result, post_sr);
}
NrResolvedColorView NrResolveColorViewFromList(ID3D12GraphicsCommandList* list,
        ID3D12Resource* resource, const D3D12_RESOURCE_DESC& desc,
        unsigned long long generation, bool post_sr, int flags,
        const NrColorSemanticContract* color = nullptr) {
    ID3D12Device* device = nullptr;
    if (!list || FAILED(ID3D12GraphicsCommandList_GetDevice(list, IID_ID3D12Device,
            reinterpret_cast<void**>(&device))) || !device) return {};
    const auto view = NrResolveColorView(device, resource, desc, generation, post_sr, flags, color);
    NrFormatDiagnostic diagnostic{};
    diagnostic.available = true;
    diagnostic.physical = static_cast<unsigned>(desc.Format);
    diagnostic.flags = static_cast<unsigned>(desc.Flags);
    diagnostic.width = static_cast<unsigned>(desc.Width);
    diagnostic.height = desc.Height;
    diagnostic.arrays = desc.DepthOrArraySize; diagnostic.mips = desc.MipLevels;
    diagnostic.samples = desc.SampleDesc.Count;
    diagnostic.observed = view.evidence.view;
    if(view.clear_source)diagnostic.observed.Observe(view.clear_request.format);
    diagnostic.semantic = view.semantic;
    diagnostic.srv = view.plan.srv;
    diagnostic.uav = view.plan.uav;
    diagnostic.config = g_config_generation.load(std::memory_order_relaxed);
    diagnostic.feature = generation;
    diagnostic.device = view.key.device;
    diagnostic.resource = view.key.resource;
    diagnostic.observed_call = view.clear_source ? view.clear_request.call : view.evidence.last_call;
    diagnostic.clear_source = view.clear_source;
    diagnostic.status = view.plan.status;
    diagnostic.provenance = view.plan.provenance;
    diagnostic.coverage = NrFormatGetDevice(device).hooks;
    diagnostic.conflict = view.evidence.view.conflict;
    diagnostic.native_failed = view.evidence.failed;
    ID3D12Device_Release(device);
    AcquireSRWLockExclusive(&g_nr_format_lock);
    const unsigned role = post_sr ? 1 : 0;
    auto& first = g_nr_format_diagnostics.first_unknown[role];
    if (first.feature != generation || first.config != diagnostic.config) first = {};
    if (!view && !first.available) first = diagnostic;
    g_nr_format_diagnostics.latest[role] = diagnostic;
    ReleaseSRWLockExclusive(&g_nr_format_lock);
    return view;
}
