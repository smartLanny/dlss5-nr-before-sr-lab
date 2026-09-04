// Native D3D12 compute-state envelope. Public ReShade events do not expose
// SetDescriptorHeaps or every root GPU address, so setters are authoritative.
// The shadow starts UNKNOWN when instrumentation is installed into a recording;
// only an observed successful Reset/ClearState establishes a complete baseline.
// SDK semantics: same root signature preserves arguments; changed heaps
// invalidate both stages' tables; ExecuteIndirect clears its root arguments.

// BEGIN COMMAND_STATE_MODEL (extracted unchanged by the regression harness)
enum class CommandRootKind : unsigned char { Empty, Table, Constants, Cbv, Srv, Uav };
struct CommandRootArgument {
    CommandRootKind kind = CommandRootKind::Empty;
    UINT64 value = 0;
    UINT64 constant_mask = 0;
    UINT constants[64] = {};
};
struct NativeCommandState {
    bool known = false;
    const char *unknown_reason = "awaiting-native-reset";
    UINT unknown_domains = 0;
    UINT meta_initialization_dirty = 0;
    UINT meta_execution_dirty = 0;
    ID3D12PipelineState *pipeline = nullptr;
    ID3D12StateObject *raytracing_pipeline = nullptr;
    ID3D12RootSignature *roots[2] = {};
    ID3D12DescriptorHeap *heaps[2] = {}; // CBV/SRV/UAV, sampler
    CommandRootArgument arguments[2][64] = {};
};
template <typename T> void CommandStateAssign(T *&destination, T *source) {
    if (source == destination) return;
    if (source != nullptr) IUnknown_AddRef(reinterpret_cast<IUnknown *>(source));
    if (destination != nullptr)
        IUnknown_Release(reinterpret_cast<IUnknown *>(destination));
    destination = source;
}
void ReleaseNativeCommandState(NativeCommandState &state) {
    CommandStateAssign(state.pipeline, static_cast<ID3D12PipelineState *>(nullptr));
    CommandStateAssign(state.raytracing_pipeline, static_cast<ID3D12StateObject *>(nullptr));
    for (unsigned int stage = 0; stage < 2; ++stage) {
        CommandStateAssign(state.roots[stage], static_cast<ID3D12RootSignature *>(nullptr));
        CommandStateAssign(state.heaps[stage], static_cast<ID3D12DescriptorHeap *>(nullptr));
    }
    // Do not assign a whole aggregate here: Clang can materialize a ~36 KiB
    // temporary even at -O2, exceeding the available game worker stack.
    memset(state.arguments, 0, sizeof(state.arguments));
    state.known = false;
    state.unknown_reason = "awaiting-native-reset";
    state.unknown_domains = 0;
    state.meta_initialization_dirty = 0;
    state.meta_execution_dirty = 0;
}
void ResetNativeCommandState(NativeCommandState &state, ID3D12PipelineState *pipeline) {
    const UINT meta_initialization_dirty = state.meta_initialization_dirty;
    const UINT meta_execution_dirty = state.meta_execution_dirty;
    ReleaseNativeCommandState(state);
    state.meta_initialization_dirty = meta_initialization_dirty;
    state.meta_execution_dirty = meta_execution_dirty;
    CommandStateAssign(state.pipeline, pipeline);
    state.known = true;
    state.unknown_reason = "none";
}
void CopyNativeCommandState(NativeCommandState &destination, const NativeCommandState &source) {
    CommandStateAssign(destination.pipeline, source.pipeline);
    CommandStateAssign(destination.raytracing_pipeline, source.raytracing_pipeline);
    for (unsigned int stage = 0; stage < 2; ++stage) {
        CommandStateAssign(destination.roots[stage], source.roots[stage]);
        CommandStateAssign(destination.heaps[stage], source.heaps[stage]);
    }
    memcpy(destination.arguments, source.arguments, sizeof(source.arguments));
    destination.known = source.known;
    destination.unknown_reason = source.unknown_reason;
    destination.unknown_domains = source.unknown_domains;
    destination.meta_initialization_dirty = source.meta_initialization_dirty;
    destination.meta_execution_dirty = source.meta_execution_dirty;
}
void CommandStateSetRoot(NativeCommandState &state, unsigned int stage, ID3D12RootSignature *root) {
    const UINT domain = stage == 0 ? D3D12_GRAPHICS_STATE_COMPUTE_ROOT_SIGNATURE : D3D12_GRAPHICS_STATE_GRAPHICS_ROOT_SIGNATURE;
    if (state.roots[stage] == root && (state.unknown_domains & domain) == 0) return;
    state.unknown_domains &= ~domain;
    CommandStateAssign(state.roots[stage], root);
    for (auto &argument : state.arguments[stage]) argument = {};
}
void CommandStateSetHeaps(NativeCommandState &state, UINT count, ID3D12DescriptorHeap *const *heaps) {
    ID3D12DescriptorHeap *next[2] = {};
    if (count > 2 || (count != 0 && heaps == nullptr)) { state.known = false; return; }
    for (UINT i = 0; i < count; ++i) {
        if (heaps[i] == nullptr) { state.known = false; return; }
        const auto description = ID3D12DescriptorHeap_GetDesc(heaps[i]);
        const unsigned int index = static_cast<unsigned int>(description.Type);
        if (index > 1 || next[index] != nullptr) { state.known = false; return; }
        next[index] = heaps[i];
    }
    if (state.heaps[0] == next[0] && state.heaps[1] == next[1] &&
        (state.unknown_domains & D3D12_GRAPHICS_STATE_DESCRIPTOR_HEAP) == 0) return;
    state.unknown_domains &= ~D3D12_GRAPHICS_STATE_DESCRIPTOR_HEAP;
    for (unsigned int i = 0; i < 2; ++i) CommandStateAssign(state.heaps[i], next[i]);
    for (auto &stage : state.arguments)
        for (auto &argument : stage)
            if (argument.kind == CommandRootKind::Table) argument = {};
}
void CommandStateSetValue(NativeCommandState &state, unsigned int stage,
                          UINT index, CommandRootKind kind, UINT64 value) {
    if (index >= 64) { state.known = false; return; }
    auto &argument = state.arguments[stage][index];
    argument = {};
    argument.kind = kind;
    argument.value = value;
}
void CommandStateSetConstants(NativeCommandState &state, unsigned int stage,
                              UINT index, UINT count, const void *values, UINT offset) {
    if (index >= 64 || offset > 64 || count > 64 - offset ||
        (count != 0 && values == nullptr)) { state.known = false; return; }
    auto &argument = state.arguments[stage][index];
    if (argument.kind != CommandRootKind::Constants) argument = {};
    argument.kind = CommandRootKind::Constants;
    const auto *source = static_cast<const UINT *>(values);
    for (UINT i = 0; i < count; ++i) {
        argument.constants[offset + i] = source[i];
        argument.constant_mask |= UINT64{1} << (offset + i);
    }
}
bool CanRestoreNativeCommandState(const NativeCommandState &state) {
    // SetPipelineState(nullptr) is not a legal way to restore an unbound PSO.
    // Clearing all graphics state here would require a much wider envelope.
    return state.known && state.unknown_domains == 0 &&
        (state.pipeline != nullptr || state.raytracing_pipeline != nullptr);
}
void RestoreNativeCommandState(ID3D12GraphicsCommandList *command_list,
                               const NativeCommandState &state) {
    ID3D12DescriptorHeap *heaps[2] = {};
    UINT heap_count = 0;
    for (auto *heap : state.heaps) if (heap != nullptr) heaps[heap_count++] = heap;
    ID3D12GraphicsCommandList_SetDescriptorHeaps(command_list, heap_count, heaps);
    ID3D12GraphicsCommandList_SetComputeRootSignature(command_list, state.roots[0]);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(command_list, state.roots[1]);
    for (unsigned int stage = 0; stage < 2; ++stage) {
        for (UINT index = 0; index < 64; ++index) {
            const auto &argument = state.arguments[stage][index];
            switch (argument.kind) {
            case CommandRootKind::Empty: break;
            case CommandRootKind::Table: {
                D3D12_GPU_DESCRIPTOR_HANDLE handle{argument.value};
                if (stage == 0) ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(command_list, index, handle);
                else ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(command_list, index, handle);
                break;
            }
            case CommandRootKind::Constants:
                // Preserve partial writes, including holes that were never set.
                for (UINT begin = 0; begin < 64;) {
                    if ((argument.constant_mask & (UINT64{1} << begin)) == 0) { ++begin; continue; }
                    UINT end = begin + 1;
                    while (end < 64 && (argument.constant_mask & (UINT64{1} << end)) != 0) ++end;
                    if (stage == 0) ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(command_list, index, end - begin, argument.constants + begin, begin);
                    else ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(command_list, index, end - begin, argument.constants + begin, begin);
                    begin = end;
                }
                break;
            case CommandRootKind::Cbv:
                if (stage == 0) ID3D12GraphicsCommandList_SetComputeRootConstantBufferView(command_list, index, argument.value);
                else ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(command_list, index, argument.value);
                break;
            case CommandRootKind::Srv:
                if (stage == 0) ID3D12GraphicsCommandList_SetComputeRootShaderResourceView(command_list, index, argument.value);
                else ID3D12GraphicsCommandList_SetGraphicsRootShaderResourceView(command_list, index, argument.value);
                break;
            case CommandRootKind::Uav:
                if (stage == 0) ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(command_list, index, argument.value);
                else ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(command_list, index, argument.value);
                break;
            }
        }
    }
    if (state.pipeline != nullptr) ID3D12GraphicsCommandList_SetPipelineState(command_list, state.pipeline);
    else if (state.raytracing_pipeline != nullptr) {
        ID3D12GraphicsCommandList4 *extended = nullptr;
        if (SUCCEEDED(ID3D12GraphicsCommandList_QueryInterface(command_list, IID_ID3D12GraphicsCommandList4, reinterpret_cast<void **>(&extended)))) {
            ID3D12GraphicsCommandList4_SetPipelineState1(extended, state.raytracing_pipeline);
            ID3D12GraphicsCommandList4_Release(extended);
        }
    }
}
// END COMMAND_STATE_MODEL

constexpr GUID kCommandStateIdentity = {0xf2176d89, 0x1362, 0x4eec, {0x93,0x5f,0x9e,0xa8,0x63,0x90,0xd3,0x1f}};
constexpr GUID kCommandSignatureState = {0x4b2eddb8, 0x465c, 0x4251, {0x91,0x39,0x23,0x75,0xd8,0xe9,0x8c,0x07}};
constexpr GUID kCommandStateMetaContract = {0xc5c81d83, 0x9623, 0x4635, {0x80,0xa8,0x11,0xc4,0x13,0x1a,0x4f,0x88}};
constexpr UINT kCommandStateCoveredDomains = D3D12_GRAPHICS_STATE_DESCRIPTOR_HEAP |
    D3D12_GRAPHICS_STATE_COMPUTE_ROOT_SIGNATURE | D3D12_GRAPHICS_STATE_GRAPHICS_ROOT_SIGNATURE |
    D3D12_GRAPHICS_STATE_PIPELINE_STATE;
struct CommandStateMetaContract {
    bool known = false;
    UINT initialization_dirty = 0;
    UINT execution_dirty = 0;
};
struct CommandStateRecord {
    unsigned long long token = 0;
    ID3D12GraphicsCommandList *native = nullptr;
    reshade::api::command_list *owner = nullptr;
    NativeCommandState *state = nullptr; // Only NR carrier lists allocate this.
    bool hooks_ready = false;
    CommandStateMetaContract meta_contract;
};
CommandStateRecord g_command_states[2048] = {};
SRWLOCK g_command_state_lock = SRWLOCK_INIT;
std::atomic<unsigned long long> g_command_state_epoch{1};
unsigned long long g_next_command_state_token = 1;
std::atomic<unsigned long long> g_command_state_bypasses{0};
bool EnsureNativeCommandListResetHook(reshade::api::command_list *command_list);
bool PinHookAddress(const void *address, const char *name, HMODULE expected_module);

unsigned long long ReadCommandStateToken(ID3D12GraphicsCommandList *command_list) {
    unsigned long long token = 0;
    UINT bytes = sizeof(token);
    if (command_list == nullptr || FAILED(ID3D12GraphicsCommandList_GetPrivateData(
            command_list, kCommandStateIdentity, &bytes, &token)) || bytes != sizeof(token)) return 0;
    return token;
}
CommandStateRecord *FindCommandStateTokenLocked(unsigned long long token) {
    if (token == 0) return nullptr;
    for (auto &record : g_command_states) if (record.token == token) return &record;
    return nullptr;
}
template <typename Mutation> void MutateNativeCommandState(ID3D12GraphicsCommandList *native, Mutation mutation) {
    // No COM/private-data access on every setter. Unrelated lists cache a no-op.
    thread_local ID3D12GraphicsCommandList *last_native = nullptr;
    thread_local unsigned long long last_epoch = 0;
    thread_local CommandStateRecord *last_record = nullptr;
    const auto epoch = g_command_state_epoch.load(std::memory_order_acquire);
    if (last_native != native || last_epoch != epoch) {
        AcquireSRWLockShared(&g_command_state_lock);
        last_record = nullptr;
        for (auto &record : g_command_states)
            if (record.native == native && record.state != nullptr) { last_record = &record; break; }
        last_native = native;
        last_epoch = epoch;
        ReleaseSRWLockShared(&g_command_state_lock);
    }
    if (last_record == nullptr) return;
    AcquireSRWLockExclusive(&g_command_state_lock);
    if (last_record->native == native && last_record->state != nullptr && last_record->hooks_ready)
        mutation(*last_record->state);
    ReleaseSRWLockExclusive(&g_command_state_lock);
}

struct CommandStateHookFamily {
    void *targets[4] = {};
    void *originals[4] = {};
    bool enabled[4] = {};
    unsigned int count = 0;
};
#define NR_STATE_HOOK(NAME, PARAMETERS, ARGUMENTS, MUTATION) \
    CommandStateHookFamily g_state_##NAME; \
    template <unsigned int I> void STDMETHODCALLTYPE StateHook##NAME PARAMETERS { \
        using Fn = decltype(ID3D12GraphicsCommandListVtbl::NAME); \
        reinterpret_cast<Fn>(g_state_##NAME.originals[I]) ARGUMENTS; \
        MutateNativeCommandState(command_list, [&](NativeCommandState &state) { MUTATION; }); \
    } \
    void *const g_state_detours_##NAME[4] = { \
        reinterpret_cast<void *>(&StateHook##NAME<0>), reinterpret_cast<void *>(&StateHook##NAME<1>), \
        reinterpret_cast<void *>(&StateHook##NAME<2>), reinterpret_cast<void *>(&StateHook##NAME<3>) };

NR_STATE_HOOK(SetPipelineState, (ID3D12GraphicsCommandList *command_list, ID3D12PipelineState *pipeline),
    (command_list, pipeline), CommandStateAssign(state.pipeline, pipeline); CommandStateAssign(state.raytracing_pipeline, static_cast<ID3D12StateObject *>(nullptr)); state.unknown_domains &= ~D3D12_GRAPHICS_STATE_PIPELINE_STATE)
NR_STATE_HOOK(ClearState, (ID3D12GraphicsCommandList *command_list, ID3D12PipelineState *pipeline),
    (command_list, pipeline), ResetNativeCommandState(state, pipeline))
NR_STATE_HOOK(SetDescriptorHeaps, (ID3D12GraphicsCommandList *command_list, UINT count, ID3D12DescriptorHeap *const *heaps),
    (command_list, count, heaps), CommandStateSetHeaps(state, count, heaps))
#define NR_STATE_STAGE(NAME, STAGE) \
NR_STATE_HOOK(Set##NAME##RootSignature, (ID3D12GraphicsCommandList *command_list, ID3D12RootSignature *root), \
    (command_list, root), CommandStateSetRoot(state, STAGE, root)) \
NR_STATE_HOOK(Set##NAME##RootDescriptorTable, (ID3D12GraphicsCommandList *command_list, UINT index, D3D12_GPU_DESCRIPTOR_HANDLE handle), \
    (command_list, index, handle), CommandStateSetValue(state, STAGE, index, CommandRootKind::Table, handle.ptr)) \
NR_STATE_HOOK(Set##NAME##Root32BitConstant, (ID3D12GraphicsCommandList *command_list, UINT index, UINT value, UINT offset), \
    (command_list, index, value, offset), CommandStateSetConstants(state, STAGE, index, 1, &value, offset)) \
NR_STATE_HOOK(Set##NAME##Root32BitConstants, (ID3D12GraphicsCommandList *command_list, UINT index, UINT count, const void *values, UINT offset), \
    (command_list, index, count, values, offset), CommandStateSetConstants(state, STAGE, index, count, values, offset)) \
NR_STATE_HOOK(Set##NAME##RootConstantBufferView, (ID3D12GraphicsCommandList *command_list, UINT index, D3D12_GPU_VIRTUAL_ADDRESS value), \
    (command_list, index, value), CommandStateSetValue(state, STAGE, index, CommandRootKind::Cbv, value)) \
NR_STATE_HOOK(Set##NAME##RootShaderResourceView, (ID3D12GraphicsCommandList *command_list, UINT index, D3D12_GPU_VIRTUAL_ADDRESS value), \
    (command_list, index, value), CommandStateSetValue(state, STAGE, index, CommandRootKind::Srv, value)) \
NR_STATE_HOOK(Set##NAME##RootUnorderedAccessView, (ID3D12GraphicsCommandList *command_list, UINT index, D3D12_GPU_VIRTUAL_ADDRESS value), \
    (command_list, index, value), CommandStateSetValue(state, STAGE, index, CommandRootKind::Uav, value))
NR_STATE_STAGE(Compute, 0)
NR_STATE_STAGE(Graphics, 1)
NR_STATE_HOOK(ExecuteBundle, (ID3D12GraphicsCommandList *command_list, ID3D12GraphicsCommandList *bundle),
    (command_list, bundle), state.known = false; state.unknown_reason = "execute-bundle")
#undef NR_STATE_STAGE
#undef NR_STATE_HOOK

struct CommandSignatureState {
    unsigned int stage = 1;
    unsigned int count = 0;
    bool valid = true;
    D3D12_INDIRECT_ARGUMENT_DESC arguments[64] = {};
};
// Microsoft Agility SDK 1.714's value 11 has a two-UINT union member:
// RootParameterIndex, DestOffsetIn32BitValues. These are the same first two
// UINTs as Constant, but there is no Num32BitValuesToSet field for this type.
constexpr unsigned int kIndirectIncrementingConstant = 11;
static_assert(sizeof(UINT) == 4);
static_assert(sizeof(D3D12_INDIRECT_ARGUMENT_DESC) == 16);
static_assert(alignof(D3D12_INDIRECT_ARGUMENT_DESC) == 4);
static_assert(offsetof(D3D12_INDIRECT_ARGUMENT_DESC, Type) == 0);
static_assert(offsetof(D3D12_INDIRECT_ARGUMENT_DESC, Constant.RootParameterIndex) == 4);
static_assert(offsetof(D3D12_INDIRECT_ARGUMENT_DESC, Constant.DestOffsetIn32BitValues) == 8);
static_assert(D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS == 9);
static_assert(D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH == 10);
void DescribeCommandSignature(const D3D12_COMMAND_SIGNATURE_DESC &description,
                              CommandSignatureState &info) {
    info.count = description.NumArgumentDescs;
    info.stage = 1;
    info.valid = info.count != 0 && info.count <= 64 && description.pArgumentDescs != nullptr;
    if (!info.valid) return;
    unsigned int terminal_count = 0;
    for (UINT i = 0; i < info.count; ++i) {
        info.arguments[i] = description.pArgumentDescs[i];
        const auto &argument = info.arguments[i];
        const unsigned int type = static_cast<unsigned int>(argument.Type);
        bool terminal = false;
        switch (type) {
        case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW:
        case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED:
        case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH:
            terminal = true; info.stage = 1; break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH:
        case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS:
            terminal = true; info.stage = 0; break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW:
        case D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW:
            break; // These do not modify the root/heap/PSO envelope.
        case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT:
            if (argument.Constant.RootParameterIndex >= 64 || argument.Constant.DestOffsetIn32BitValues > 64 ||
                argument.Constant.Num32BitValuesToSet > 64 - argument.Constant.DestOffsetIn32BitValues) info.valid = false;
            break;
        case kIndirectIncrementingConstant:
            if (argument.Constant.RootParameterIndex >= 64 || argument.Constant.DestOffsetIn32BitValues >= 64) info.valid = false;
            break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW:
            if (argument.ConstantBufferView.RootParameterIndex >= 64) info.valid = false;
            break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW:
            if (argument.ShaderResourceView.RootParameterIndex >= 64) info.valid = false;
            break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW:
            if (argument.UnorderedAccessView.RootParameterIndex >= 64) info.valid = false;
            break;
        default:
            info.valid = false; break;
        }
        if (terminal) {
            ++terminal_count;
            if (i + 1 != info.count) info.valid = false;
        }
    }
    // Native successful Create is authoritative for device feature support.
    // We still require one known final operation to select its root stage.
    if (terminal_count != 1) info.valid = false;
}
std::atomic<unsigned long long> g_command_signature_create_calls{0};
std::atomic<unsigned long long> g_command_signature_created{0};
std::atomic<unsigned long long> g_command_signature_metadata_writes{0};
std::atomic<unsigned long long> g_command_signature_metadata_failures{0};
std::atomic<unsigned long long> g_command_signature_invalid_descriptions{0};
std::atomic<unsigned long long> g_command_signature_unknown_uses{0};
void FormatCommandSignatureTypes(const CommandSignatureState &info, char *text, size_t capacity) {
    if (capacity == 0) return;
    text[0] = '\0';
    size_t used = 0;
    for (UINT i = 0; i < info.count && i < 64; ++i) {
        const int written = snprintf(text + used, capacity - used, "%s%u", i == 0 ? "" : ",",
            static_cast<unsigned int>(info.arguments[i].Type));
        if (written < 0 || static_cast<size_t>(written) >= capacity - used) break;
        used += static_cast<size_t>(written);
    }
}
void ApplyIndirectCommandState(NativeCommandState &state, ID3D12CommandSignature *signature) {
    CommandSignatureState info;
    UINT bytes = sizeof(info);
    const HRESULT read = signature == nullptr ? E_POINTER :
        ID3D12CommandSignature_GetPrivateData(signature, kCommandSignatureState, &bytes, &info);
    if (FAILED(read) || bytes != sizeof(info) || !info.valid || info.count > 64 || info.stage > 1) {
        const auto unknown = g_command_signature_unknown_uses.fetch_add(1, std::memory_order_relaxed) + 1;
        if (unknown <= 16 || (unknown % 600) == 0) {
            const bool available = SUCCEEDED(read) && bytes == sizeof(info);
            char types[384] = {};
            if (available) FormatCommandSignatureTypes(info, types, sizeof(types));
            Log("[command-state] indirect signature unavailable: signature=%p getPrivate=0x%08X bytes=%u expected=%zu "
                "metadata=%d valid=%d count=%u stage=%u types=[%s] observedCreate=%llu created=%llu written=%llu writeFailures=%llu invalidDescriptions=%llu unknownUses=%llu.",
                static_cast<void *>(signature), static_cast<unsigned int>(read), bytes, sizeof(info), available ? 1 : 0,
                available ? (info.valid ? 1 : 0) : -1, available ? info.count : 0, available ? info.stage : 0, types,
                g_command_signature_create_calls.load(std::memory_order_relaxed), g_command_signature_created.load(std::memory_order_relaxed),
                g_command_signature_metadata_writes.load(std::memory_order_relaxed), g_command_signature_metadata_failures.load(std::memory_order_relaxed),
                g_command_signature_invalid_descriptions.load(std::memory_order_relaxed), unknown);
        }
        state.known = false; state.unknown_reason = "unknown-indirect-signature"; return;
    }
    for (UINT i = 0; i < info.count; ++i) {
        const auto &argument = info.arguments[i];
        switch (static_cast<unsigned int>(argument.Type)) {
        case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT: {
            const UINT zero[64] = {};
            CommandStateSetConstants(state, info.stage, argument.Constant.RootParameterIndex,
                argument.Constant.Num32BitValuesToSet, zero, argument.Constant.DestOffsetIn32BitValues);
            break;
        }
        case kIndirectIncrementingConstant: {
            const UINT zero = 0;
            CommandStateSetConstants(state, info.stage, argument.Constant.RootParameterIndex,
                1, &zero, argument.Constant.DestOffsetIn32BitValues);
            break;
        }
        case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW:
            CommandStateSetValue(state, info.stage, argument.ConstantBufferView.RootParameterIndex, CommandRootKind::Cbv, 0); break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW:
            CommandStateSetValue(state, info.stage, argument.ShaderResourceView.RootParameterIndex, CommandRootKind::Srv, 0); break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW:
            CommandStateSetValue(state, info.stage, argument.UnorderedAccessView.RootParameterIndex, CommandRootKind::Uav, 0); break;
        default: break;
        }
    }
}
CommandStateHookFamily g_state_ExecuteIndirect;
template <unsigned int I> void STDMETHODCALLTYPE StateHookExecuteIndirect(
    ID3D12GraphicsCommandList *command_list, ID3D12CommandSignature *signature,
    UINT max_count, ID3D12Resource *arguments, UINT64 offset, ID3D12Resource *count_buffer, UINT64 count_offset) {
    using Fn = decltype(ID3D12GraphicsCommandListVtbl::ExecuteIndirect);
    reinterpret_cast<Fn>(g_state_ExecuteIndirect.originals[I])(command_list, signature, max_count, arguments, offset, count_buffer, count_offset);
    MutateNativeCommandState(command_list, [&](NativeCommandState &state) { ApplyIndirectCommandState(state, signature); });
}
void *const g_state_detours_ExecuteIndirect[4] = {
    reinterpret_cast<void *>(&StateHookExecuteIndirect<0>), reinterpret_cast<void *>(&StateHookExecuteIndirect<1>),
    reinterpret_cast<void *>(&StateHookExecuteIndirect<2>), reinterpret_cast<void *>(&StateHookExecuteIndirect<3>) };
CommandStateHookFamily g_state_SetPipelineState1;
template <unsigned int I> void STDMETHODCALLTYPE StateHookSetPipelineState1(
    ID3D12GraphicsCommandList4 *command_list, ID3D12StateObject *pipeline) {
    using Fn = decltype(ID3D12GraphicsCommandList4Vtbl::SetPipelineState1);
    reinterpret_cast<Fn>(g_state_SetPipelineState1.originals[I])(command_list, pipeline);
    MutateNativeCommandState(reinterpret_cast<ID3D12GraphicsCommandList *>(command_list), [&](NativeCommandState &state) {
        CommandStateAssign(state.pipeline, static_cast<ID3D12PipelineState *>(nullptr));
        CommandStateAssign(state.raytracing_pipeline, pipeline);
        state.unknown_domains &= ~D3D12_GRAPHICS_STATE_PIPELINE_STATE;
    });
}
void *const g_state_detours_SetPipelineState1[4] = {
    reinterpret_cast<void *>(&StateHookSetPipelineState1<0>), reinterpret_cast<void *>(&StateHookSetPipelineState1<1>),
    reinterpret_cast<void *>(&StateHookSetPipelineState1<2>), reinterpret_cast<void *>(&StateHookSetPipelineState1<3>) };
#define NR_META_STATE_HOOK(NAME, FIELD) \
    CommandStateHookFamily g_state_##NAME; \
    template <unsigned int I> void STDMETHODCALLTYPE StateHook##NAME(ID3D12GraphicsCommandList4 *command_list, \
        ID3D12MetaCommand *meta, const void *parameters, SIZE_T bytes) { \
        using Fn = decltype(ID3D12GraphicsCommandList4Vtbl::NAME); \
        reinterpret_cast<Fn>(g_state_##NAME.originals[I])(command_list, meta, parameters, bytes); \
        MutateNativeCommandState(reinterpret_cast<ID3D12GraphicsCommandList *>(command_list), \
            [&](NativeCommandState &state) { state.unknown_domains |= state.FIELD; }); \
    } \
    void *const g_state_detours_##NAME[4] = { \
        reinterpret_cast<void *>(&StateHook##NAME<0>), reinterpret_cast<void *>(&StateHook##NAME<1>), \
        reinterpret_cast<void *>(&StateHook##NAME<2>), reinterpret_cast<void *>(&StateHook##NAME<3>) };
NR_META_STATE_HOOK(InitializeMetaCommand, meta_initialization_dirty)
NR_META_STATE_HOOK(ExecuteMetaCommand, meta_execution_dirty)
#undef NR_META_STATE_HOOK
// The bundled MinGW header predates CL10. ABI verified against Microsoft's
// Agility SDK 1.714.0-preview d3d12.h/IDL: IID below, vtable slot 84,
// void SetProgram(const D3D12_SET_PROGRAM_DESC *). The opaque descriptor is
// forwarded unchanged; work-graph state is deliberately not reconstructed.
constexpr GUID kCommandList10Interface = {0x7013c015,0xd161,0x4b63,{0xa0,0x8c,0x23,0x85,0x52,0xdd,0x8a,0xcc}};
constexpr unsigned int kCommandListSetProgramSlot = 84;
CommandStateHookFamily g_state_SetProgram;
template <unsigned int I> void STDMETHODCALLTYPE StateHookSetProgram(ID3D12GraphicsCommandList *command_list, const void *description) {
    using Fn = void (STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList *, const void *);
    reinterpret_cast<Fn>(g_state_SetProgram.originals[I])(command_list, description);
    MutateNativeCommandState(command_list, [&](NativeCommandState &state) {
        state.known = false; state.unknown_reason = "work-graph-SetProgram";
    });
}
void *const g_state_detours_SetProgram[4] = {
    reinterpret_cast<void *>(&StateHookSetProgram<0>), reinterpret_cast<void *>(&StateHookSetProgram<1>),
    reinterpret_cast<void *>(&StateHookSetProgram<2>), reinterpret_cast<void *>(&StateHookSetProgram<3>) };
CommandStateHookFamily g_state_CreateCommandSignature;
template <unsigned int I> HRESULT STDMETHODCALLTYPE StateHookCreateCommandSignature(
    ID3D12Device *device, const D3D12_COMMAND_SIGNATURE_DESC *description,
    ID3D12RootSignature *root, REFIID iid, void **output) {
    using Fn = decltype(ID3D12DeviceVtbl::CreateCommandSignature);
    const auto observed = g_command_signature_create_calls.fetch_add(1, std::memory_order_relaxed) + 1;
    const HRESULT result = reinterpret_cast<Fn>(g_state_CreateCommandSignature.originals[I])(device, description, root, iid, output);
    if (SUCCEEDED(result) && output != nullptr && *output != nullptr && description != nullptr) {
        g_command_signature_created.fetch_add(1, std::memory_order_relaxed);
        ID3D12CommandSignature *signature = nullptr;
        const HRESULT queried = IUnknown_QueryInterface(static_cast<IUnknown *>(*output), IID_ID3D12CommandSignature, reinterpret_cast<void **>(&signature));
        if (SUCCEEDED(queried) && signature != nullptr) {
            CommandSignatureState info;
            DescribeCommandSignature(*description, info);
            if (!info.valid) g_command_signature_invalid_descriptions.fetch_add(1, std::memory_order_relaxed);
            const HRESULT written = ID3D12CommandSignature_SetPrivateData(signature, kCommandSignatureState, sizeof(info), &info);
            if (SUCCEEDED(written)) g_command_signature_metadata_writes.fetch_add(1, std::memory_order_relaxed);
            else g_command_signature_metadata_failures.fetch_add(1, std::memory_order_relaxed);
            if (observed <= 16 || ((!info.valid || FAILED(written)) && (observed % 60) == 0)) {
                char types[384] = {};
                FormatCommandSignatureTypes(info, types, sizeof(types));
                Log("[command-state] observed CreateCommandSignature: call=%llu signature=%p result=0x%08X metadataWrite=0x%08X "
                    "valid=%d count=%u stage=%u types=[%s] root=%p.", observed, static_cast<void *>(signature),
                    static_cast<unsigned int>(result), static_cast<unsigned int>(written), info.valid ? 1 : 0,
                    info.count, info.stage, types, static_cast<void *>(root));
            }
            ID3D12CommandSignature_Release(signature);
        } else {
            g_command_signature_metadata_failures.fetch_add(1, std::memory_order_relaxed);
            if (observed <= 16) Log("[command-state] CreateCommandSignature metadata query failed: call=%llu query=0x%08X result=0x%08X.",
                observed, static_cast<unsigned int>(queried), static_cast<unsigned int>(result));
        }
    } else if (observed <= 16) {
        Log("[command-state] observed CreateCommandSignature returned no object: call=%llu result=0x%08X description=%p.",
            observed, static_cast<unsigned int>(result), static_cast<const void *>(description));
    }
    return result;
}
void *const g_state_detours_CreateCommandSignature[4] = {
    reinterpret_cast<void *>(&StateHookCreateCommandSignature<0>), reinterpret_cast<void *>(&StateHookCreateCommandSignature<1>),
    reinterpret_cast<void *>(&StateHookCreateCommandSignature<2>), reinterpret_cast<void *>(&StateHookCreateCommandSignature<3>) };

struct CommandStateHookRequest { CommandStateHookFamily *family; void *target; void *const *detours; };
bool PublishCommandStateHooks(CommandStateHookRequest *requests, unsigned int count) {
    // MinHook's queued list is global; serialize with NGX observer publication.
    AcquireSRWLockExclusive(&g_evaluate_hook_lock);
    bool ok = true;
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) ok = false;
    void *queued[32] = {};
    bool *enabled_flags[32] = {};
    unsigned int queued_count = 0;
    for (unsigned int r = 0; ok && r < count; ++r) {
        auto &request = requests[r];
        auto &family = *request.family;
        unsigned int i = 0;
        for (; i < family.count; ++i) if (family.targets[i] == request.target) break;
        if (i == family.count) {
            if (i == 4 || request.target == nullptr || !PinHookAddress(request.target, "D3D12 command state", nullptr)) { ok = false; break; }
            if (MH_CreateHook(request.target, request.detours[i], &family.originals[i]) != MH_OK) { ok = false; break; }
            family.targets[i] = request.target;
            ++family.count;
        }
        if (family.enabled[i]) continue;
        const auto status = MH_QueueEnableHook(request.target);
        if (status != MH_OK && status != MH_ERROR_ENABLED) { ok = false; break; }
        enabled_flags[queued_count] = &family.enabled[i];
        queued[queued_count++] = request.target;
    }
    if (ok) ok = MH_ApplyQueued() == MH_OK;
    if (ok) for (unsigned int i = 0; i < queued_count; ++i) *enabled_flags[i] = true;
    if (!ok) {
        // Do not leave queued enables for an unrelated future NGX transaction.
        for (unsigned int i = 0; i < queued_count; ++i) MH_QueueDisableHook(queued[i]);
        MH_ApplyQueued();
        Log("[command-state] hook publication failed; this carrier remains native.");
    }
    ReleaseSRWLockExclusive(&g_evaluate_hook_lock);
    return ok;
}
void OnCommandStateInitDevice(reshade::api::device *device) {
    if (device == nullptr || device->get_api() != reshade::api::device_api::d3d12) return;
    auto *native = reinterpret_cast<ID3D12Device *>(device->get_native());
    if (native == nullptr) return;
    CommandStateMetaContract meta;
    UINT meta_bytes = sizeof(meta);
    if (FAILED(ID3D12Device_GetPrivateData(native, kCommandStateMetaContract, &meta_bytes, &meta)) || meta_bytes != sizeof(meta)) {
        ID3D12Device5 *extended = nullptr;
        const HRESULT queried = ID3D12Device_QueryInterface(native, IID_ID3D12Device5, reinterpret_cast<void **>(&extended));
        if (queried == E_NOINTERFACE) meta.known = true; // This device cannot create meta commands.
        if (SUCCEEDED(queried) && extended != nullptr) {
            UINT count = 0;
            if (SUCCEEDED(ID3D12Device5_EnumerateMetaCommands(extended, &count, nullptr)) && count <= 256) {
                auto *descriptions = count == 0 ? nullptr : new (std::nothrow) D3D12_META_COMMAND_DESC[count];
                if (count == 0 || descriptions != nullptr) {
                    UINT actual = count;
                    meta.known = count == 0 || SUCCEEDED(ID3D12Device5_EnumerateMetaCommands(extended, &actual, descriptions));
                    if (actual > count) meta.known = false;
                    if (meta.known) for (UINT i = 0; i < actual; ++i) {
                        meta.initialization_dirty |= static_cast<UINT>(descriptions[i].InitializationDirtyState);
                        meta.execution_dirty |= static_cast<UINT>(descriptions[i].ExecutionDirtyState);
                    }
                    delete[] descriptions;
                }
            }
            ID3D12Device5_Release(extended);
        }
        ID3D12Device_SetPrivateData(native, kCommandStateMetaContract, sizeof(meta), &meta);
        Log("[command-state] device meta-command contract: known=%d initDirty=0x%X executeDirty=0x%X supportedMask=0x%X.",
            meta.known ? 1 : 0, meta.initialization_dirty, meta.execution_dirty, kCommandStateCoveredDomains);
    }
    CommandStateHookRequest request{&g_state_CreateCommandSignature,
        reinterpret_cast<void *>(native->lpVtbl->CreateCommandSignature), g_state_detours_CreateCommandSignature};
    PublishCommandStateHooks(&request, 1);
}
bool InstallNativeCommandStateHooks(ID3D12GraphicsCommandList *native) {
    CommandStateHookRequest requests[24] = {};
    unsigned int count = 0;
#define NR_ADD_STATE_HOOK(NAME) requests[count++] = { &g_state_##NAME, reinterpret_cast<void *>(native->lpVtbl->NAME), g_state_detours_##NAME }
    NR_ADD_STATE_HOOK(SetPipelineState); NR_ADD_STATE_HOOK(ClearState);
    NR_ADD_STATE_HOOK(SetDescriptorHeaps); NR_ADD_STATE_HOOK(ExecuteBundle); NR_ADD_STATE_HOOK(ExecuteIndirect);
#define NR_ADD_STATE_STAGE(NAME) \
    NR_ADD_STATE_HOOK(Set##NAME##RootSignature); NR_ADD_STATE_HOOK(Set##NAME##RootDescriptorTable); \
    NR_ADD_STATE_HOOK(Set##NAME##Root32BitConstant); NR_ADD_STATE_HOOK(Set##NAME##Root32BitConstants); \
    NR_ADD_STATE_HOOK(Set##NAME##RootConstantBufferView); NR_ADD_STATE_HOOK(Set##NAME##RootShaderResourceView); \
    NR_ADD_STATE_HOOK(Set##NAME##RootUnorderedAccessView)
    NR_ADD_STATE_STAGE(Compute); NR_ADD_STATE_STAGE(Graphics);
#undef NR_ADD_STATE_STAGE
#undef NR_ADD_STATE_HOOK
    ID3D12GraphicsCommandList4 *extended = nullptr;
    if (SUCCEEDED(ID3D12GraphicsCommandList_QueryInterface(native, IID_ID3D12GraphicsCommandList4, reinterpret_cast<void **>(&extended)))) {
        if (reinterpret_cast<void *>(extended) != reinterpret_cast<void *>(native)) {
            // Distinct tear-off interfaces need their own full setter family.
            // Do not silently miss base setters or the active DXR PSO.
            ID3D12GraphicsCommandList4_Release(extended);
            Log("[command-state] distinct command-list4 tear-off interface is unsupported; carrier remains native.");
            return false;
        }
        requests[count++] = { &g_state_SetPipelineState1, reinterpret_cast<void *>(extended->lpVtbl->SetPipelineState1), g_state_detours_SetPipelineState1 };
        requests[count++] = { &g_state_InitializeMetaCommand, reinterpret_cast<void *>(extended->lpVtbl->InitializeMetaCommand), g_state_detours_InitializeMetaCommand };
        requests[count++] = { &g_state_ExecuteMetaCommand, reinterpret_cast<void *>(extended->lpVtbl->ExecuteMetaCommand), g_state_detours_ExecuteMetaCommand };
    }
    IUnknown *latest = nullptr;
    if (SUCCEEDED(ID3D12GraphicsCommandList_QueryInterface(native, kCommandList10Interface, reinterpret_cast<void **>(&latest)))) {
        if (reinterpret_cast<void *>(latest) != reinterpret_cast<void *>(native)) {
            IUnknown_Release(latest);
            if (extended != nullptr) ID3D12GraphicsCommandList4_Release(extended);
            Log("[command-state] distinct command-list10 tear-off interface is unsupported; carrier remains native.");
            return false;
        }
        auto **vtable = *reinterpret_cast<void ***>(latest);
        requests[count++] = { &g_state_SetProgram, vtable[kCommandListSetProgramSlot], g_state_detours_SetProgram };
    }
    const bool result = PublishCommandStateHooks(requests, count);
    if (latest != nullptr) IUnknown_Release(latest);
    if (extended != nullptr) ID3D12GraphicsCommandList4_Release(extended);
    return result;
}
void OnCommandStateInitList(reshade::api::command_list *owner) {
    if (owner == nullptr || owner->get_device() == nullptr ||
        owner->get_device()->get_api() != reshade::api::device_api::d3d12) return;
    auto *native = reinterpret_cast<ID3D12GraphicsCommandList *>(owner->get_native());
    if (native == nullptr) return;
    const auto type = ID3D12GraphicsCommandList_GetType(native);
    if (type != D3D12_COMMAND_LIST_TYPE_DIRECT) return;
    OnCommandStateInitDevice(owner->get_device());
    auto *native_device = reinterpret_cast<ID3D12Device *>(owner->get_device()->get_native());
    CommandStateMetaContract meta;
    UINT meta_bytes = sizeof(meta);
    if (FAILED(ID3D12Device_GetPrivateData(native_device, kCommandStateMetaContract, &meta_bytes, &meta)) || meta_bytes != sizeof(meta)) meta = {};
    AcquireSRWLockExclusive(&g_command_state_lock);
    if (FindCommandStateTokenLocked(ReadCommandStateToken(native)) == nullptr) {
        for (auto &record : g_command_states) if (record.token == 0) {
            const auto token = g_next_command_state_token++;
            if (SUCCEEDED(ID3D12GraphicsCommandList_SetPrivateData(native, kCommandStateIdentity, sizeof(token), &token))) {
                record.token = token; record.native = native; record.owner = owner; record.meta_contract = meta;
                g_command_state_epoch.fetch_add(1, std::memory_order_release);
            }
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_command_state_lock);
}
void OnCommandStateBeforeReset(reshade::api::command_list *owner) {
    if (owner == nullptr || owner->get_device() == nullptr || owner->get_device()->get_api() != reshade::api::device_api::d3d12) return;
    auto *native = reinterpret_cast<ID3D12GraphicsCommandList *>(owner->get_native());
    if (ReadCommandStateToken(native) == 0) OnCommandStateInitList(owner);
    const auto token = ReadCommandStateToken(native);
    AcquireSRWLockExclusive(&g_command_state_lock);
    auto *record = FindCommandStateTokenLocked(token);
    const bool active = record != nullptr && record->state != nullptr;
    if (record != nullptr && record->native != native) {
        record->native = native; record->hooks_ready = false;
        if (record->state != nullptr) record->state->known = false;
        g_command_state_epoch.fetch_add(1, std::memory_order_release);
    }
    const bool install = active && !record->hooks_ready;
    ReleaseSRWLockExclusive(&g_command_state_lock);
    const bool ready = active && (!install || InstallNativeCommandStateHooks(native)) && EnsureNativeCommandListResetHook(owner);
    AcquireSRWLockExclusive(&g_command_state_lock);
    if (record != nullptr && record->token == token) record->hooks_ready = ready;
    ReleaseSRWLockExclusive(&g_command_state_lock);
}
void OnCommandStateResetResult(ID3D12GraphicsCommandList *native, HRESULT result, ID3D12PipelineState *pipeline) {
    MutateNativeCommandState(native, [&](NativeCommandState &state) {
        if (SUCCEEDED(result)) ResetNativeCommandState(state, pipeline);
        else { state.known = false; state.unknown_reason = "failed-native-reset"; }
    });
}
void OnCommandStateDestroyList(reshade::api::command_list *owner) {
    if (owner == nullptr || owner->get_device() == nullptr || owner->get_device()->get_api() != reshade::api::device_api::d3d12) return;
    auto *native = reinterpret_cast<ID3D12GraphicsCommandList *>(owner->get_native());
    const auto token = ReadCommandStateToken(native);
    AcquireSRWLockExclusive(&g_command_state_lock);
    auto *record = FindCommandStateTokenLocked(token);
    if (record != nullptr) {
        if (record->state != nullptr) { ReleaseNativeCommandState(*record->state); delete record->state; }
        *record = {};
        g_command_state_epoch.fetch_add(1, std::memory_order_release);
    }
    ReleaseSRWLockExclusive(&g_command_state_lock);
}
bool CaptureNativeCommandState(ID3D12GraphicsCommandList *command_list, NativeCommandState &snapshot,
                               ID3D12GraphicsCommandList **native_output = nullptr) {
    if (native_output != nullptr) *native_output = nullptr;
    // Restoring graphics tables is only legal on DIRECT lists. Do not record
    // even a null graphics-root setter onto a COMPUTE/COPY command list.
    if (command_list == nullptr || ID3D12GraphicsCommandList_GetType(command_list) != D3D12_COMMAND_LIST_TYPE_DIRECT) {
        static std::atomic<unsigned int> rejected_type{0};
        if (rejected_type.fetch_add(1, std::memory_order_relaxed) < 3)
            Log("[command-state] NR bypass: DIRECT command list required; no plugin commands recorded.");
        SetBypassReason(BypassReason::CommandStateUnverified);
        return false;
    }
    const auto token = ReadCommandStateToken(command_list);
    reshade::api::command_list *owner = nullptr;
    ID3D12GraphicsCommandList *native = nullptr;
    bool needs_install = false;
    bool captured = false;
    const char *unavailable_reason = "unregistered-list";
    UINT unknown_domains = 0;
    AcquireSRWLockExclusive(&g_command_state_lock);
    auto *record = FindCommandStateTokenLocked(token);
    if (record != nullptr) {
        if (record->state == nullptr) {
            record->state = new (std::nothrow) NativeCommandState;
            if (record->state != nullptr) {
                record->state->meta_initialization_dirty = record->meta_contract.initialization_dirty;
                record->state->meta_execution_dirty = record->meta_contract.execution_dirty;
            }
            g_command_state_epoch.fetch_add(1, std::memory_order_release);
        }
        owner = record->owner; native = record->native;
        needs_install = record->state != nullptr && !record->hooks_ready;
        const bool meta_covered = record->meta_contract.known &&
            ((record->meta_contract.initialization_dirty | record->meta_contract.execution_dirty) & ~kCommandStateCoveredDomains) == 0;
        captured = meta_covered && record->state != nullptr && record->hooks_ready && CanRestoreNativeCommandState(*record->state);
        unavailable_reason = !meta_covered ? "meta-command-coverage" : record->state == nullptr ? "snapshot-allocation" :
            !record->hooks_ready ? "native-hooks-unavailable" : !record->state->known ? record->state->unknown_reason :
            record->state->unknown_domains != 0 ? "meta-command-dirty-state" : "pipeline-unbound";
        if (record->state != nullptr) unknown_domains = record->state->unknown_domains;
        if (captured) {
            CopyNativeCommandState(snapshot, *record->state);
            if (native_output != nullptr) *native_output = native;
        }
    }
    ReleaseSRWLockExclusive(&g_command_state_lock);
    if (needs_install) {
        const bool ready = InstallNativeCommandStateHooks(native) && EnsureNativeCommandListResetHook(owner);
        AcquireSRWLockExclusive(&g_command_state_lock);
        if (record->token == token) record->hooks_ready = ready;
        ReleaseSRWLockExclusive(&g_command_state_lock);
    }
    if (!captured) {
        const auto count = g_command_state_bypasses.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count <= 6 || (count % 600) == 0)
            Log("[command-state] NR bypass: reason=%s token=%llu registered=%d installing=%d dirty=0x%X count=%llu; successful native Reset and game bindings can recover.", unavailable_reason, token, record != nullptr ? 1 : 0, needs_install ? 1 : 0, unknown_domains, count);
        SetBypassReason(BypassReason::CommandStateUnverified);
    }
    return captured;
}
struct NativeCommandStateScope {
    ID3D12GraphicsCommandList *command_list;
    // Games may dispatch NGX on 64 KiB worker stacks. A state is ~36 KiB;
    // never put this snapshot (or a temporary copy) on the caller's stack.
    NativeCommandState *saved;
    bool active;
    explicit NativeCommandStateScope(ID3D12GraphicsCommandList *list)
        : command_list(nullptr), saved(new (std::nothrow) NativeCommandState),
          active(saved != nullptr && CaptureNativeCommandState(list, *saved, &command_list)) {
        if (saved == nullptr) SetBypassReason(BypassReason::CommandStateUnverified);
    }
    ~NativeCommandStateScope() {
        Restore();
        if (saved != nullptr) { ReleaseNativeCommandState(*saved); delete saved; }
    }
    void Restore() {
        if (!active) return;
        // Every additional NR/codec command uses this same native interface.
        // ReShade's wrapper caches therefore retain the game's values, while
        // native heaps never reach a wrapper expecting proxy heap objects.
        RestoreNativeCommandState(command_list, *saved);
        const auto token = ReadCommandStateToken(command_list);
        AcquireSRWLockExclusive(&g_command_state_lock);
        auto *record = FindCommandStateTokenLocked(token);
        if (record != nullptr && record->state != nullptr)
            CopyNativeCommandState(*record->state, *saved);
        ReleaseSRWLockExclusive(&g_command_state_lock);
        static std::atomic<unsigned int> restored{0};
        if (restored.fetch_add(1, std::memory_order_relaxed) < 3)
            Log("[command-state] restored native state: token=%llu pso=%p raytracing=%p computeRoot=%p graphicsRoot=%p resourceHeap=%p samplerHeap=%p.",
                token, static_cast<void *>(saved->pipeline), static_cast<void *>(saved->raytracing_pipeline),
                static_cast<void *>(saved->roots[0]), static_cast<void *>(saved->roots[1]),
                static_cast<void *>(saved->heaps[0]), static_cast<void *>(saved->heaps[1]));
        active = false;
    }
    NativeCommandStateScope(const NativeCommandStateScope &) = delete;
    NativeCommandStateScope &operator=(const NativeCommandStateScope &) = delete;
};
