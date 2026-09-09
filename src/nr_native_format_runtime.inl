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
bool EnsureNrNativeFormatHooks(ID3D12Device* device);
void OnNrFormatInitDevice(reshade::api::device* device);

nr_native_format::Key NrFormatKey(ID3D12Device* device, ID3D12Resource* resource,
                                  unsigned long long generation, bool post_sr) {
    const auto receipt = NrFormatGetDevice(device);
    if (!receipt.hooks) return {};
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
template <class NativeEvaluate>
NgxResult NrCallNativeWithFormatObservation(NativeEvaluate original,
        ID3D12GraphicsCommandList* list, const NgxHandle* handle,
        const NgxParams* params, void* callback, unsigned long long generation) {
    NrNativeFormatScope scope(list, params, generation);
    const NgxResult result = original(list, handle, params, callback);
    scope.Finish(result == kNgxSuccess);
    return result;
}
void NrPrepareFormatObservation(ID3D12GraphicsCommandList* list) {
    ID3D12Device* device = nullptr;
    if (list && SUCCEEDED(ID3D12GraphicsCommandList_GetDevice(list, IID_ID3D12Device,
            reinterpret_cast<void**>(&device))) && device) {
        EnsureNrNativeFormatHooks(device);
        ID3D12Device_Release(device);
    }
}

struct NrResolvedColorView {
    D3D12_RESOURCE_DESC typed{};
    nr_native_format::Key key{};
    nr_native_format::Evidence evidence{};
    nr_format_access::Plan plan{};
    bool native_bounded_output = false;
    explicit operator bool() const noexcept { return static_cast<bool>(plan); }
};
struct NrFormatDiagnostic {
    unsigned physical = 0, srv = 0, uav = 0, config = 0;
    unsigned long long feature = 0, device = 0, resource = 0, observed_call = 0;
    nr_format_access::Status status = nr_format_access::Status::AmbiguousType;
    nr_format_access::Provenance provenance = nr_format_access::Provenance::None;
    bool available = false, coverage = false, conflict = false, native_failed = false;
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
NrResolvedColorView NrResolveColorView(ID3D12Device* device, ID3D12Resource* resource,
        const D3D12_RESOURCE_DESC& desc, unsigned long long generation, bool post_sr,
        int feature_create_flags) {
    NrResolvedColorView result{};
    result.typed = desc;
    nr_format_access::Request request{};
    request.resource = static_cast<unsigned>(desc.Format);
    // Existing FLOAT storage contract is retained. The R8 family needs native
    // Feature 1 evidence; format family membership alone proves no value domain.
    request.permit_float_interpretation = !post_sr;
    // The established typed-FP16 post path uses CopyResource into our owned
    // readable snapshot; it never creates a read view of a denied game Output.
    const bool exact_fp16_copy = post_sr && desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
    request.deny_shader_resource = !exact_fp16_copy &&
        (desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) != 0;
    request.allow_unordered_access = (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0;
    request.needs_inplace_store = post_sr;
    if (nr_format_access::IsTypeless(request.resource) ||
        request.resource == nr_format_access::Rgba8Unorm) {
        result.key = NrFormatKey(device, resource, generation, post_sr);
        result.evidence = NrFormatEvidence(result.key);
        if (result.evidence.failed || result.evidence.view.conflict) return result;
        request.observed_view = result.evidence.Resolved();
    }
    if (!device || !desc.Width || !desc.Height ||
        desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        desc.DepthOrArraySize != 1 || desc.MipLevels != 1 || desc.SampleDesc.Count != 1) return result;
    const unsigned interpretation = request.observed_view ? request.observed_view : request.resource;
    if (nr_format_access::GetFamily(interpretation) == nr_format_access::Family::Rgba8 &&
        interpretation != nr_format_access::Rgba8Unorm &&
        interpretation != nr_format_access::Rgba8Typeless) {
        result.plan.status = nr_format_access::Status::UnknownLayout;
        return result; // No signed/integer/sRGB codec contract is implemented here.
    }
    result.plan = nr_format_access::Resolve(request, [device](unsigned f, nr_format_access::Usage usage) {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
        support.Format = static_cast<DXGI_FORMAT>(f);
        if (FAILED(ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_FORMAT_SUPPORT,
                &support, sizeof(support)))) return false;
        if (usage == nr_format_access::Usage::ShaderLoad)
            return (support.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D) != 0 &&
                   (support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD) != 0;
        return (support.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) != 0 &&
               (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) != 0;
    });
    result.typed.Format = static_cast<DXGI_FORMAT>(result.plan.srv);
    result.native_bounded_output = post_sr && (feature_create_flags & 1) == 0 &&
        request.observed_view == nr_format_access::Rgba8Unorm &&
        result.plan.provenance == nr_format_access::Provenance::ObservedView;
    return result;
}
NrResolvedColorView NrResolveColorViewFromList(ID3D12GraphicsCommandList* list,
        ID3D12Resource* resource, const D3D12_RESOURCE_DESC& desc,
        unsigned long long generation, bool post_sr, int flags) {
    ID3D12Device* device = nullptr;
    if (!list || FAILED(ID3D12GraphicsCommandList_GetDevice(list, IID_ID3D12Device,
            reinterpret_cast<void**>(&device))) || !device) return {};
    const auto view = NrResolveColorView(device, resource, desc, generation, post_sr, flags);
    NrFormatDiagnostic diagnostic{};
    diagnostic.available = true;
    diagnostic.physical = static_cast<unsigned>(desc.Format);
    diagnostic.srv = view.plan.srv;
    diagnostic.uav = view.plan.uav;
    diagnostic.config = g_config_generation.load(std::memory_order_relaxed);
    diagnostic.feature = generation;
    diagnostic.device = view.key.device;
    diagnostic.resource = view.key.resource;
    diagnostic.observed_call = view.evidence.last_call;
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
