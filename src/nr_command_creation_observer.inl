// Optional evidence observer only. A successful Create receipt does NOT set
// state.known or bypass the existing Reset/coverage/queue admission rules.
constexpr GUID kCommandCreationReceipt = {0x7a8e5c43,0x13bf,0x4f80,{0x9a,0x46,0x2e,0x82,0x62,0xd7,0xa1,0x93}};
std::atomic<UINT64> g_command_create_calls{0}, g_command_create_successes{0};
std::atomic<UINT64> g_command_create1_calls{0}, g_command_create1_successes{0};
std::atomic<UINT64> g_command_create_receipt_failures{0}, g_command_creation_serial{0};
std::atomic<UINT64> g_command_init_events{0}, g_command_destroy_events{0};
std::atomic<UINT64> g_command_native_reset_events{0}, g_command_native_reset_failures{0};
std::atomic<UINT64> g_command_reset_without_record{0}, g_command_reset_identity_mismatches{0};
std::atomic<UINT64> g_command_creation_publication_failures{0};
#define NR_HAS_COMMAND_CREATION_EVIDENCE 1

void ObserveCommandCreation(ID3D12Device *device, D3D12_COMMAND_LIST_TYPE type,
        UINT node_mask, unsigned method, UINT flags, bool initial_pso,
        HRESULT result, void **output) {
    if (FAILED(result) || !output || !*output || type != D3D12_COMMAND_LIST_TYPE_DIRECT) return;
    ID3D12GraphicsCommandList *list = nullptr;
    if (FAILED(IUnknown_QueryInterface(static_cast<IUnknown *>(*output), IID_ID3D12GraphicsCommandList,
            reinterpret_cast<void **>(&list))) || !list) {
        g_command_create_receipt_failures.fetch_add(1, std::memory_order_relaxed); return;
    }
    CommandCreationInfo info;
    info.method = method; info.node_mask = node_mask; info.flags = flags; info.initial_pso = initial_pso;
    info.serial = g_command_creation_serial.fetch_add(1, std::memory_order_relaxed) + 1;
    info.tick = GetTickCount64();
    CommandStateMetaContract meta;
    UINT bytes = sizeof(meta);
    if (SUCCEEDED(ID3D12Device_GetPrivateData(device, kCommandStateMetaContract, &bytes, &meta)) && bytes == sizeof(meta))
        info.device_identity = meta.device_identity;
    if (FAILED(ID3D12GraphicsCommandList_SetPrivateData(list, kCommandCreationReceipt, sizeof(info), &info)))
        g_command_create_receipt_failures.fetch_add(1, std::memory_order_relaxed);
    ID3D12GraphicsCommandList_Release(list);
}
CommandStateHookFamily g_state_CreateCommandList, g_state_CreateCommandList1;
template<unsigned I> HRESULT STDMETHODCALLTYPE StateHookCreateCommandList(ID3D12Device *device,
        UINT mask, D3D12_COMMAND_LIST_TYPE type, ID3D12CommandAllocator *allocator,
        ID3D12PipelineState *pipeline, REFIID iid, void **output) {
    g_command_create_calls.fetch_add(1, std::memory_order_relaxed);
    using Fn = decltype(ID3D12DeviceVtbl::CreateCommandList);
    const HRESULT result = reinterpret_cast<Fn>(g_state_CreateCommandList.originals[I])(
        device, mask, type, allocator, pipeline, iid, output);
    if (SUCCEEDED(result) && output && *output) g_command_create_successes.fetch_add(1, std::memory_order_relaxed);
    ObserveCommandCreation(device, type, mask, 1, 0, pipeline != nullptr, result, output);
    return result;
}
template<unsigned I> HRESULT STDMETHODCALLTYPE StateHookCreateCommandList1(ID3D12Device4 *device,
        UINT mask, D3D12_COMMAND_LIST_TYPE type, D3D12_COMMAND_LIST_FLAGS flags, REFIID iid, void **output) {
    g_command_create1_calls.fetch_add(1, std::memory_order_relaxed);
    using Fn = decltype(ID3D12Device4Vtbl::CreateCommandList1);
    const HRESULT result = reinterpret_cast<Fn>(g_state_CreateCommandList1.originals[I])(
        device, mask, type, flags, iid, output);
    if (SUCCEEDED(result) && output && *output) g_command_create1_successes.fetch_add(1, std::memory_order_relaxed);
    ObserveCommandCreation(reinterpret_cast<ID3D12Device *>(device), type, mask, 2,
        static_cast<UINT>(flags), false, result, output);
    return result;
}
void *const g_state_detours_CreateCommandList[4] = {
    reinterpret_cast<void *>(&StateHookCreateCommandList<0>), reinterpret_cast<void *>(&StateHookCreateCommandList<1>),
    reinterpret_cast<void *>(&StateHookCreateCommandList<2>), reinterpret_cast<void *>(&StateHookCreateCommandList<3>)};
void *const g_state_detours_CreateCommandList1[4] = {
    reinterpret_cast<void *>(&StateHookCreateCommandList1<0>), reinterpret_cast<void *>(&StateHookCreateCommandList1<1>),
    reinterpret_cast<void *>(&StateHookCreateCommandList1<2>), reinterpret_cast<void *>(&StateHookCreateCommandList1<3>)};
// Optional evidence may remain unavailable until restart. Reserve each target
// before publication and try it only once: persistent MinHook failures must not
// suspend game threads on every list creation. This lock is never taken by a
// detour, and is released before the existing global publication lock is used.
struct CommandCreationAttempts { void *targets[4] = {}; unsigned count = 0; };
SRWLOCK g_command_creation_attempt_lock = SRWLOCK_INIT;
CommandCreationAttempts g_creation_attempts, g_creation1_attempts;
void PublishCommandCreationOnce(CommandStateHookRequest &request, CommandCreationAttempts &attempts) {
    if (!request.target) return;
    AcquireSRWLockExclusive(&g_command_creation_attempt_lock);
    bool fresh = attempts.count < 4;
    for (unsigned i = 0; i < attempts.count; ++i)
        if (attempts.targets[i] == request.target) fresh = false;
    if (fresh) attempts.targets[attempts.count++] = request.target;
    ReleaseSRWLockExclusive(&g_command_creation_attempt_lock);
    if (fresh && !PublishCommandStateHooks(&request, 1))
        g_command_creation_publication_failures.fetch_add(1, std::memory_order_relaxed);
}
void EnsureCommandCreationObserver(ID3D12Device *device) {
    CommandStateHookRequest request{&g_state_CreateCommandList,
        reinterpret_cast<void *>(device->lpVtbl->CreateCommandList), g_state_detours_CreateCommandList};
    PublishCommandCreationOnce(request, g_creation_attempts);
    ID3D12Device4 *extended = nullptr;
    if (SUCCEEDED(ID3D12Device_QueryInterface(device, IID_ID3D12Device4, reinterpret_cast<void **>(&extended))) && extended) {
        CommandStateHookRequest next{&g_state_CreateCommandList1,
            reinterpret_cast<void *>(extended->lpVtbl->CreateCommandList1), g_state_detours_CreateCommandList1};
        PublishCommandCreationOnce(next, g_creation1_attempts);
        ID3D12Device4_Release(extended);
    }
}
