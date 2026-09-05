// Native D3D12 compute-state envelope. Public ReShade events do not expose
// SetDescriptorHeaps or every root GPU address, so setters are authoritative.
// The shadow starts UNKNOWN when instrumentation is installed into a recording;
// only an observed successful Reset/ClearState establishes a complete baseline.
// SDK semantics: same root signature preserves arguments; changed heaps
// invalidate both stages' tables; ExecuteIndirect clears its root arguments.

// BEGIN COMMAND_STATE_MODEL (extracted unchanged by the regression harness)
enum class CommandRootKind : unsigned char { Empty, Table, Constants, Cbv, Srv, Uav };
// ClearState(NULL) is the only public way to restore Reset(NULL)'s dummy PSO.
// Its additional dynamic state lives in the existing heap snapshot, never on
// the game worker stack. A recorded bit distinguishes an omitted/default setter.
struct CommandDynamicState {
    UINT64 recorded = 0;
    D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    UINT viewport_count = 0, scissor_count = 0;
    D3D12_VIEWPORT viewports[16] = {};
    D3D12_RECT scissors[16] = {};
    FLOAT blend[4] = {};
    UINT stencil_front = 0, stencil_back = 0;
    bool separate_stencil = false, has_index = false, has_depth_target = false;
    D3D12_INDEX_BUFFER_VIEW index = {};
    D3D12_VERTEX_BUFFER_VIEW vertices[32] = {};
    UINT vertex_mask = 0;
    D3D12_STREAM_OUTPUT_BUFFER_VIEW stream_output[4] = {};
    UINT stream_mask = 0, target_count = 0;
    BOOL contiguous_targets = FALSE;
    D3D12_CPU_DESCRIPTOR_HANDLE targets[8] = {}, depth_target = {};
    UINT64 predicate_offset = 0;
    D3D12_PREDICATION_OP predicate_operation = D3D12_PREDICATION_OP_EQUAL_ZERO;
    FLOAT depth_min = 0, depth_max = 1;
    UINT sample_count = 0, sample_pixels = 0, view_mask = 0;
    D3D12_SAMPLE_POSITION samples[64] = {};
    D3D12_SHADING_RATE shading_rate = D3D12_SHADING_RATE_1X1;
    D3D12_SHADING_RATE_COMBINER combiners[2] = {};
    bool has_combiners = false;
    FLOAT depth_bias = 0, depth_bias_clamp = 0, slope_bias = 0;
    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE strip_cut = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
};
enum : UINT64 {
    kDynamicTopology=1, kDynamicViewport=2, kDynamicScissor=4, kDynamicBlend=8,
    kDynamicStencil=16, kDynamicIndex=32, kDynamicTargets=64, kDynamicPredicate=128,
    kDynamicDepthBounds=256, kDynamicSamples=512, kDynamicViewMask=1024,
    kDynamicShadingRate=2048, kDynamicShadingImage=4096, kDynamicDepthBias=8192,
    kDynamicStripCut=16384
};
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
    ID3D12Resource *predicate = nullptr, *shading_image = nullptr;
    ID3D12DescriptorHeap *rtv_cache = nullptr, *dsv_cache = nullptr;
    bool null_pipeline_baseline = false;
    // Coverage is not inferred from a null pointer or an unobserved zero mask.
    bool null_guard_covered = false;
    bool null_guard_reset_observed = false;
    bool null_guard_tainted = false;
    const char *baseline_origin = "unobserved";
    UINT64 reset_serial = 0, pipeline_writes = 0;
    CommandDynamicState dynamic;
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
    CommandStateAssign(state.rtv_cache, static_cast<ID3D12DescriptorHeap *>(nullptr));
    CommandStateAssign(state.dsv_cache, static_cast<ID3D12DescriptorHeap *>(nullptr));
    CommandStateAssign(state.predicate, static_cast<ID3D12Resource *>(nullptr));
    CommandStateAssign(state.shading_image, static_cast<ID3D12Resource *>(nullptr));
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
    state.null_pipeline_baseline = false;
    state.null_guard_covered = false;
    state.null_guard_reset_observed = false;
    state.null_guard_tainted = false;
    state.baseline_origin = "unobserved";
    state.reset_serial = state.pipeline_writes = 0;
    memset(&state.dynamic, 0, sizeof(state.dynamic));
}
void ResetNativeCommandState(NativeCommandState &state, ID3D12PipelineState *pipeline,
                             const char *origin = "Reset") {
    const UINT meta_initialization_dirty = state.meta_initialization_dirty;
    const UINT meta_execution_dirty = state.meta_execution_dirty;
    const UINT64 serial = state.reset_serial + 1;
    const bool covered = state.null_guard_covered;
    const bool fresh_reset = std::strcmp(origin, "Reset") == 0;
    const bool reset_observed = fresh_reset ? covered : state.null_guard_reset_observed;
    const bool tainted = !fresh_reset && state.null_guard_tainted;
    ReleaseNativeCommandState(state);
    state.null_guard_covered = covered;
    state.null_guard_reset_observed = reset_observed;
    state.null_guard_tainted = tainted;
    state.meta_initialization_dirty = meta_initialization_dirty;
    state.meta_execution_dirty = meta_execution_dirty;
    CommandStateAssign(state.pipeline, pipeline);
    state.known = true;
    state.unknown_reason = "none";
    state.null_pipeline_baseline = pipeline == nullptr;
    state.baseline_origin = origin;
    state.reset_serial = serial;
}
bool EnsureCommandCpuHeap(ID3D12Device *device, ID3D12DescriptorHeap *&heap,
                          D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count) {
    if (heap != nullptr) return true;
    D3D12_DESCRIPTOR_HEAP_DESC desc{}; desc.Type=type; desc.NumDescriptors=count;
    return SUCCEEDED(ID3D12Device_CreateDescriptorHeap(device, &desc, IID_ID3D12DescriptorHeap,
        reinterpret_cast<void **>(&heap))) && heap != nullptr;
}
bool CopyCommandCpuHeap(ID3D12DescriptorHeap *&destination, ID3D12DescriptorHeap *source,
                        D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count) {
    if (source == nullptr || count == 0) return true;
    ID3D12Device *device=nullptr;
    if (FAILED(ID3D12DescriptorHeap_GetDevice(source, IID_ID3D12Device, reinterpret_cast<void **>(&device))) || !device) return false;
    const bool ready=EnsureCommandCpuHeap(device, destination, type, type==D3D12_DESCRIPTOR_HEAP_TYPE_RTV ? 8 : 1);
    if (ready) ID3D12Device_CopyDescriptorsSimple(device, count,
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(destination),
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(source), type);
    ID3D12Device_Release(device);
    return ready;
}
bool CopyNativeCommandState(NativeCommandState &destination, const NativeCommandState &source) {
    if (&destination == &source) return true;
    // RTV/DSV handles are consumed at recording time. Applications may recycle
    // their original CPU slots immediately; snapshots must own descriptor copies.
    // Copy rather than share the caches: a nested scope/setter may update either.
    if (source.null_pipeline_baseline &&
        (!CopyCommandCpuHeap(destination.rtv_cache, source.rtv_cache, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, source.dynamic.target_count) ||
         !CopyCommandCpuHeap(destination.dsv_cache, source.dsv_cache, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, source.dynamic.has_depth_target ? 1 : 0))) {
        destination.known=false; destination.unknown_reason="dynamic-descriptor-snapshot-allocation";
        return false;
    }
    CommandStateAssign(destination.pipeline, source.pipeline);
    CommandStateAssign(destination.raytracing_pipeline, source.raytracing_pipeline);
    CommandStateAssign(destination.predicate, source.predicate);
    CommandStateAssign(destination.shading_image, source.shading_image);
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
    destination.null_pipeline_baseline = source.null_pipeline_baseline;
    destination.null_guard_covered = source.null_guard_covered;
    destination.null_guard_reset_observed = source.null_guard_reset_observed;
    destination.null_guard_tainted = source.null_guard_tainted;
    destination.baseline_origin = source.baseline_origin;
    destination.reset_serial = source.reset_serial;
    destination.pipeline_writes = source.pipeline_writes;
    memcpy(&destination.dynamic, &source.dynamic, sizeof(source.dynamic));
    if (source.null_pipeline_baseline && source.rtv_cache && source.dynamic.target_count)
        destination.dynamic.targets[0]=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(destination.rtv_cache);
    if (source.null_pipeline_baseline && source.dsv_cache && source.dynamic.has_depth_target)
        destination.dynamic.depth_target=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(destination.dsv_cache);
    return true;
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
void CommandStateInvalidDynamic(NativeCommandState &state) {
    state.known = false; state.unknown_reason = "invalid-dynamic-state";
}
void CommandStateViewports(NativeCommandState &state, UINT count, const D3D12_VIEWPORT *values) {
    if (count > 16 || (count && !values)) { CommandStateInvalidDynamic(state); return; }
    state.dynamic.viewport_count=count; state.dynamic.recorded |= kDynamicViewport;
    if (count) memcpy(state.dynamic.viewports, values, count*sizeof(*values));
}
void CommandStateScissors(NativeCommandState &state, UINT count, const D3D12_RECT *values) {
    if (count > 16 || (count && !values)) { CommandStateInvalidDynamic(state); return; }
    state.dynamic.scissor_count=count; state.dynamic.recorded |= kDynamicScissor;
    if (count) memcpy(state.dynamic.scissors, values, count*sizeof(*values));
}
void CommandStateVertices(NativeCommandState &state, UINT first, UINT count, const D3D12_VERTEX_BUFFER_VIEW *values) {
    if (first > 32 || count > 32-first) { CommandStateInvalidDynamic(state); return; }
    for (UINT i=0; i<count; ++i) { state.dynamic.vertices[first+i] = values ? values[i] : D3D12_VERTEX_BUFFER_VIEW{}; state.dynamic.vertex_mask |= UINT{1}<<(first+i); }
}
void CommandStateStreamOutput(NativeCommandState &state, UINT first, UINT count, const D3D12_STREAM_OUTPUT_BUFFER_VIEW *values) {
    if (first > 4 || count > 4-first) { CommandStateInvalidDynamic(state); return; }
    for (UINT i=0; i<count; ++i) { state.dynamic.stream_output[first+i] = values ? values[i] : D3D12_STREAM_OUTPUT_BUFFER_VIEW{}; state.dynamic.stream_mask |= UINT{1}<<(first+i); }
}
void CommandStateTargets(NativeCommandState &state, ID3D12GraphicsCommandList *list, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE *values,
                         BOOL contiguous, const D3D12_CPU_DESCRIPTOR_HANDLE *depth) {
    if (count > 8 || (count && !values)) { CommandStateInvalidDynamic(state); return; }
    auto &d=state.dynamic; d.recorded |= kDynamicTargets; d.target_count=count; d.contiguous_targets=contiguous;
    d.has_depth_target=depth!=nullptr; d.depth_target=depth ? *depth : D3D12_CPU_DESCRIPTOR_HANDLE{};
    if (count) memcpy(d.targets, values, (contiguous ? 1 : count)*sizeof(*values));
    if (!state.null_pipeline_baseline || (!count && !depth)) return;
    ID3D12Device *device=nullptr;
    const bool device_ready=list && SUCCEEDED(ID3D12GraphicsCommandList_GetDevice(list, IID_ID3D12Device,
        reinterpret_cast<void **>(&device))) && device;
    if (!device_ready || (count && !EnsureCommandCpuHeap(device,state.rtv_cache,D3D12_DESCRIPTOR_HEAP_TYPE_RTV,8)) ||
        (depth && !EnsureCommandCpuHeap(device,state.dsv_cache,D3D12_DESCRIPTOR_HEAP_TYPE_DSV,1))) {
        if (device) ID3D12Device_Release(device);
        state.known=false; state.unknown_reason="dynamic-descriptor-cache-allocation"; return;
    }
    if (count) {
        const auto first=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(state.rtv_cache);
        const UINT stride=ID3D12Device_GetDescriptorHandleIncrementSize(device,D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        if (contiguous) ID3D12Device_CopyDescriptorsSimple(device,count,first,values[0],D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        else for (UINT i=0;i<count;++i) {
            D3D12_CPU_DESCRIPTOR_HANDLE target{first.ptr+static_cast<SIZE_T>(stride)*i};
            ID3D12Device_CopyDescriptorsSimple(device,1,target,values[i],D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        }
        d.targets[0]=first; d.contiguous_targets=TRUE;
    }
    if (depth) {
        d.depth_target=ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(state.dsv_cache);
        ID3D12Device_CopyDescriptorsSimple(device,1,d.depth_target,*depth,D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    }
    ID3D12Device_Release(device);
}
void CommandStateSamples(NativeCommandState &state, UINT count, UINT pixels, D3D12_SAMPLE_POSITION *values) {
    if (count > 16 || pixels > 4 || (count*pixels && !values)) { CommandStateInvalidDynamic(state); return; }
    auto &d=state.dynamic; d.recorded |= kDynamicSamples; d.sample_count=count; d.sample_pixels=pixels;
    if (count*pixels) memcpy(d.samples, values, count*pixels*sizeof(*values));
}
// Full dynamic graphics replay stays experimental. The automatic path requires
// complete guard publication followed by a fresh successful native Reset.
bool CanAutoRestorePristineNull(const NativeCommandState &state) {
    return state.known && state.null_pipeline_baseline &&
        state.pipeline == nullptr && state.raytracing_pipeline == nullptr &&
        state.null_guard_covered && state.null_guard_reset_observed &&
        !state.null_guard_tainted && state.unknown_domains == 0 &&
        state.dynamic.recorded == 0 && state.dynamic.vertex_mask == 0 &&
        state.dynamic.stream_mask == 0;
}

// A separate, bounded subset: only viewport/scissor arrays changed after a
// fully observed successful Reset. No handles, descriptors, predication,
// vertex/stream-output buffers, queries, opaque commands or unknown domains
// are admitted by this path. Full dynamic replay is still experimental.
bool CanAutoRestoreRasterNull(const NativeCommandState &state) {
    const auto &d = state.dynamic;
    if (!state.known || !state.null_pipeline_baseline || state.pipeline ||
        state.raytracing_pipeline || !state.null_guard_covered ||
        !state.null_guard_reset_observed || state.null_guard_tainted ||
        state.unknown_domains != 0 || d.vertex_mask != 0 || d.stream_mask != 0 ||
        d.recorded == 0 || (d.recorded & ~(kDynamicViewport | kDynamicScissor)) != 0 ||
        d.viewport_count > 16 || d.scissor_count > 16) return false;
    for (UINT i = 0; i < d.viewport_count; ++i) {
        const auto &v = d.viewports[i];
        // Ordered bounded comparisons also reject NaN and infinities.
        if (!(v.TopLeftX >= D3D12_VIEWPORT_BOUNDS_MIN &&
              v.TopLeftX <= D3D12_VIEWPORT_BOUNDS_MAX &&
              v.TopLeftY >= D3D12_VIEWPORT_BOUNDS_MIN &&
              v.TopLeftY <= D3D12_VIEWPORT_BOUNDS_MAX &&
              v.Width >= 0 && v.Height >= 0 &&
              v.Width <= D3D12_VIEWPORT_BOUNDS_MAX - v.TopLeftX &&
              v.Height <= D3D12_VIEWPORT_BOUNDS_MAX - v.TopLeftY &&
              v.MinDepth >= 0 && v.MaxDepth <= 1 && v.MinDepth <= v.MaxDepth))
            return false;
    }
    for (UINT i = 0; i < d.scissor_count; ++i) {
        const auto &r = d.scissors[i];
        if (r.left > r.right || r.top > r.bottom) return false;
    }
    return true;
}

bool CanRestoreNativeCommandState(const NativeCommandState &state,
                                  bool allow_null_pso = false) {
    // A null pointer alone proves nothing. Only an observed successful native
    // Reset/ClearState establishes the dummy PSO and every ClearState domain.
    return state.known && state.unknown_domains == 0 &&
        (state.pipeline != nullptr || (state.dynamic.recorded & (kDynamicDepthBias | kDynamicStripCut)) == 0) &&
        (state.pipeline != nullptr || state.raytracing_pipeline != nullptr ||
         (allow_null_pso && state.null_pipeline_baseline));
}
// ABI slots below are from Microsoft Agility SDK 1.714 d3d12.h/IDL. Install
// verifies each corresponding interface is available and is not a tear-off.
template <typename... Args> void CommandStateExtendedCall(ID3D12GraphicsCommandList *list,
                                                         UINT slot, Args... args) {
    using Fn = void (STDMETHODCALLTYPE *)(ID3D12GraphicsCommandList *, Args...);
    auto **vtable = *reinterpret_cast<void ***>(list);
    reinterpret_cast<Fn>(vtable[slot])(list, args...);
}
void RestoreCommandDynamicState(ID3D12GraphicsCommandList *list, const NativeCommandState &state) {
    const auto &d = state.dynamic;
    if (d.recorded & kDynamicTopology) ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, d.topology);
    if (d.recorded & kDynamicViewport) ID3D12GraphicsCommandList_RSSetViewports(list, d.viewport_count, d.viewports);
    if (d.recorded & kDynamicScissor) ID3D12GraphicsCommandList_RSSetScissorRects(list, d.scissor_count, d.scissors);
    if (d.recorded & kDynamicBlend) ID3D12GraphicsCommandList_OMSetBlendFactor(list, d.blend);
    if (d.recorded & kDynamicStencil) {
        if (d.separate_stencil) CommandStateExtendedCall(list, 81, d.stencil_front, d.stencil_back);
        else ID3D12GraphicsCommandList_OMSetStencilRef(list, d.stencil_front);
    }
    if (d.recorded & kDynamicIndex) ID3D12GraphicsCommandList_IASetIndexBuffer(list, d.has_index ? &d.index : nullptr);
    for (UINT i=0; i<32; ++i) if (d.vertex_mask & (UINT{1}<<i))
        ID3D12GraphicsCommandList_IASetVertexBuffers(list, i, 1, &d.vertices[i]);
    for (UINT i=0; i<4; ++i) if (d.stream_mask & (UINT{1}<<i))
        ID3D12GraphicsCommandList_SOSetTargets(list, i, 1, &d.stream_output[i]);
    if (d.recorded & kDynamicTargets) ID3D12GraphicsCommandList_OMSetRenderTargets(list,
        d.target_count, d.targets, d.contiguous_targets, d.has_depth_target ? &d.depth_target : nullptr);
    if (d.recorded & kDynamicDepthBounds) CommandStateExtendedCall(list, 62, d.depth_min, d.depth_max);
    if (d.recorded & kDynamicSamples) CommandStateExtendedCall(list, 63, d.sample_count, d.sample_pixels,
        const_cast<D3D12_SAMPLE_POSITION *>(d.samples));
    if (d.recorded & kDynamicViewMask) CommandStateExtendedCall(list, 65, d.view_mask);
    if (d.recorded & kDynamicShadingRate) CommandStateExtendedCall(list, 77, d.shading_rate,
        d.has_combiners ? d.combiners : static_cast<const D3D12_SHADING_RATE_COMBINER *>(nullptr));
    if (d.recorded & kDynamicShadingImage) CommandStateExtendedCall(list, 78, state.shading_image);
    if (d.recorded & kDynamicPredicate) ID3D12GraphicsCommandList_SetPredication(list, state.predicate,
        d.predicate_offset, d.predicate_operation);
}
void RestoreNativeCommandState(ID3D12GraphicsCommandList *command_list,
                               const NativeCommandState &state) {
    const bool restore_dummy = state.pipeline == nullptr && state.raytracing_pipeline == nullptr &&
        state.null_pipeline_baseline;
    if (restore_dummy) ID3D12GraphicsCommandList_ClearState(command_list, nullptr);
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
    if (restore_dummy) RestoreCommandDynamicState(command_list, state);
    if (state.pipeline != nullptr) ID3D12GraphicsCommandList_SetPipelineState(command_list, state.pipeline);
    else if (state.raytracing_pipeline != nullptr) {
        ID3D12GraphicsCommandList4 *extended = nullptr;
        if (SUCCEEDED(ID3D12GraphicsCommandList_QueryInterface(command_list, IID_ID3D12GraphicsCommandList4, reinterpret_cast<void **>(&extended)))) {
            ID3D12GraphicsCommandList4_SetPipelineState1(extended, state.raytracing_pipeline);
            ID3D12GraphicsCommandList4_Release(extended);
        }
    }
    // SetPipelineState resets these CL9 values. Restore them only after the
    // ordinary graphics/compute PSO; dummy/DXR recordings with such values
    // are not admitted because their raster pipeline contract is unproven.
    if (state.pipeline != nullptr) {
        const auto &d=state.dynamic;
        if (d.recorded & kDynamicDepthBias) CommandStateExtendedCall(command_list,82,d.depth_bias,d.depth_bias_clamp,d.slope_bias);
        if (d.recorded & kDynamicStripCut) CommandStateExtendedCall(command_list,83,d.strip_cut);
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
    bool null_guard_requested = false;
    ULONGLONG next_hook_attempt = 0;
    CommandStateMetaContract meta_contract;
};
CommandStateRecord g_command_states[2048] = {};
SRWLOCK g_command_state_lock = SRWLOCK_INIT;
std::atomic<unsigned long long> g_command_state_epoch{1};
unsigned long long g_next_command_state_token = 1;
std::atomic<unsigned long long> g_command_state_bypasses{0};
std::atomic<bool> g_command_state_registration_repair_needed{false};
#define NR_HAS_COMMAND_STATE_EVIDENCE 1
struct CommandStateEvidence {
    bool available = false, captured = false, registered = false, hooks_ready = false;
    bool guard_requested = false, guard_covered = false, fresh_reset = false, tainted = false;
    bool meta_known = false, has_pipeline = false, has_ray_pipeline = false, null_baseline = false;
    bool roots[2] = {}, heaps[2] = {};
    const char *reason = "no-carrier-sample";
    const char *baseline = "unobserved";
    const char *route = "native-bypass";
    UINT dirty = 0, meta_init = 0, meta_execute = 0, vertex_mask = 0, stream_mask = 0;
    UINT arguments[2] = {};
    UINT64 sequence = 0, token = 0, reset_serial = 0, pipeline_writes = 0, dynamic = 0;
    ULONGLONG sampled_at = 0;
};
CommandStateEvidence g_command_state_evidence;
CommandStateEvidence SnapshotCommandStateEvidence() {
    CommandStateEvidence copy;
    if (!TryAcquireSRWLockShared(&g_command_state_lock)) {
        copy.reason = "snapshot-busy";
        return copy;
    }
    copy = g_command_state_evidence;
    ReleaseSRWLockShared(&g_command_state_lock);
    return copy;
}
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
    (command_list, pipeline), CommandStateAssign(state.pipeline, pipeline); CommandStateAssign(state.raytracing_pipeline, static_cast<ID3D12StateObject *>(nullptr)); state.unknown_domains &= ~D3D12_GRAPHICS_STATE_PIPELINE_STATE; state.dynamic.recorded &= ~(kDynamicDepthBias | kDynamicStripCut); state.null_pipeline_baseline=false; ++state.pipeline_writes; if (!pipeline) {state.known=false; state.unknown_reason="invalid-null-PSO-setter";})
NR_STATE_HOOK(ClearState, (ID3D12GraphicsCommandList *command_list, ID3D12PipelineState *pipeline),
    (command_list, pipeline), ResetNativeCommandState(state, pipeline, "ClearState"))
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
    (command_list, bundle), state.known = false; state.unknown_reason = "execute-bundle"; state.null_guard_tainted=true)
NR_STATE_HOOK(IASetPrimitiveTopology, (ID3D12GraphicsCommandList *command_list, D3D12_PRIMITIVE_TOPOLOGY topology),
    (command_list, topology), state.dynamic.topology=topology; state.dynamic.recorded |= kDynamicTopology)
NR_STATE_HOOK(RSSetViewports, (ID3D12GraphicsCommandList *command_list, UINT count, const D3D12_VIEWPORT *values),
    (command_list, count, values), CommandStateViewports(state, count, values))
NR_STATE_HOOK(RSSetScissorRects, (ID3D12GraphicsCommandList *command_list, UINT count, const D3D12_RECT *values),
    (command_list, count, values), CommandStateScissors(state, count, values))
NR_STATE_HOOK(OMSetBlendFactor, (ID3D12GraphicsCommandList *command_list, const FLOAT values[4]),
    (command_list, values), if (!values) CommandStateInvalidDynamic(state); else {memcpy(state.dynamic.blend, values, sizeof(state.dynamic.blend)); state.dynamic.recorded |= kDynamicBlend;})
NR_STATE_HOOK(OMSetStencilRef, (ID3D12GraphicsCommandList *command_list, UINT value),
    (command_list, value), state.dynamic.stencil_front=state.dynamic.stencil_back=value; state.dynamic.separate_stencil=false; state.dynamic.recorded |= kDynamicStencil)
NR_STATE_HOOK(IASetIndexBuffer, (ID3D12GraphicsCommandList *command_list, const D3D12_INDEX_BUFFER_VIEW *value),
    (command_list, value), state.dynamic.has_index=value!=nullptr; state.dynamic.index=value ? *value : D3D12_INDEX_BUFFER_VIEW{}; state.dynamic.recorded |= kDynamicIndex)
NR_STATE_HOOK(IASetVertexBuffers, (ID3D12GraphicsCommandList *command_list, UINT first, UINT count, const D3D12_VERTEX_BUFFER_VIEW *values),
    (command_list, first, count, values), CommandStateVertices(state, first, count, values))
NR_STATE_HOOK(SOSetTargets, (ID3D12GraphicsCommandList *command_list, UINT first, UINT count, const D3D12_STREAM_OUTPUT_BUFFER_VIEW *values),
    (command_list, first, count, values), CommandStateStreamOutput(state, first, count, values))
NR_STATE_HOOK(OMSetRenderTargets, (ID3D12GraphicsCommandList *command_list, UINT count, const D3D12_CPU_DESCRIPTOR_HANDLE *values, BOOL contiguous, const D3D12_CPU_DESCRIPTOR_HANDLE *depth),
    (command_list, count, values, contiguous, depth), CommandStateTargets(state, command_list, count, values, contiguous, depth))
NR_STATE_HOOK(SetPredication, (ID3D12GraphicsCommandList *command_list, ID3D12Resource *buffer, UINT64 offset, D3D12_PREDICATION_OP operation),
    (command_list, buffer, offset, operation), CommandStateAssign(state.predicate, buffer); state.dynamic.predicate_offset=offset; state.dynamic.predicate_operation=operation; state.dynamic.recorded |= kDynamicPredicate)
#undef NR_STATE_STAGE
#undef NR_STATE_HOOK

// CL1/3/4/5/8/9 ABI signatures are forwarded exactly. Metadata-only updates
// happen after native execution, like the base setter family above.
#define NR_EXTENDED_STATE_HOOK(NAME, PARAMETERS, ARGUMENTS, MUTATION) \
    CommandStateHookFamily g_state_##NAME; \
    template <unsigned int I> void STDMETHODCALLTYPE StateHook##NAME PARAMETERS { \
        using Fn = void (STDMETHODCALLTYPE *) PARAMETERS; \
        reinterpret_cast<Fn>(g_state_##NAME.originals[I]) ARGUMENTS; \
        MutateNativeCommandState(command_list, [&](NativeCommandState &state) { MUTATION; }); \
    } \
    void *const g_state_detours_##NAME[4] = { \
        reinterpret_cast<void *>(&StateHook##NAME<0>), reinterpret_cast<void *>(&StateHook##NAME<1>), \
        reinterpret_cast<void *>(&StateHook##NAME<2>), reinterpret_cast<void *>(&StateHook##NAME<3>) };
NR_EXTENDED_STATE_HOOK(OMSetDepthBounds, (ID3D12GraphicsCommandList *command_list, FLOAT low, FLOAT high),
    (command_list, low, high), state.dynamic.depth_min=low; state.dynamic.depth_max=high; state.dynamic.recorded |= kDynamicDepthBounds)
NR_EXTENDED_STATE_HOOK(SetSamplePositions, (ID3D12GraphicsCommandList *command_list, UINT count, UINT pixels, D3D12_SAMPLE_POSITION *values),
    (command_list, count, pixels, values), CommandStateSamples(state, count, pixels, values))
NR_EXTENDED_STATE_HOOK(SetViewInstanceMask, (ID3D12GraphicsCommandList *command_list, UINT mask),
    (command_list, mask), state.dynamic.view_mask=mask; state.dynamic.recorded |= kDynamicViewMask)
NR_EXTENDED_STATE_HOOK(SetProtectedResourceSession, (ID3D12GraphicsCommandList *command_list, ID3D12ProtectedResourceSession *session),
    (command_list, session), state.known=false; state.unknown_reason="protected-resource-session"; state.null_guard_tainted=true)
NR_EXTENDED_STATE_HOOK(BeginRenderPass, (ID3D12GraphicsCommandList *command_list, UINT count, const D3D12_RENDER_PASS_RENDER_TARGET_DESC *targets, const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC *depth, D3D12_RENDER_PASS_FLAGS flags),
    (command_list, count, targets, depth, flags), state.known=false; state.unknown_reason="render-pass"; state.null_guard_tainted=true)
NR_EXTENDED_STATE_HOOK(RSSetShadingRate, (ID3D12GraphicsCommandList *command_list, D3D12_SHADING_RATE rate, const D3D12_SHADING_RATE_COMBINER *combiners),
    (command_list, rate, combiners), state.dynamic.shading_rate=rate; state.dynamic.has_combiners=combiners!=nullptr; if (combiners) memcpy(state.dynamic.combiners, combiners, sizeof(state.dynamic.combiners)); state.dynamic.recorded |= kDynamicShadingRate)
NR_EXTENDED_STATE_HOOK(RSSetShadingRateImage, (ID3D12GraphicsCommandList *command_list, ID3D12Resource *resource),
    (command_list, resource), CommandStateAssign(state.shading_image, resource); state.dynamic.recorded |= kDynamicShadingImage)
NR_EXTENDED_STATE_HOOK(OMSetFrontAndBackStencilRef, (ID3D12GraphicsCommandList *command_list, UINT front, UINT back),
    (command_list, front, back), state.dynamic.stencil_front=front; state.dynamic.stencil_back=back; state.dynamic.separate_stencil=true; state.dynamic.recorded |= kDynamicStencil)
NR_EXTENDED_STATE_HOOK(RSSetDepthBias, (ID3D12GraphicsCommandList *command_list, FLOAT bias, FLOAT clamp, FLOAT slope),
    (command_list, bias, clamp, slope), state.dynamic.depth_bias=bias; state.dynamic.depth_bias_clamp=clamp; state.dynamic.slope_bias=slope; state.dynamic.recorded |= kDynamicDepthBias)
NR_EXTENDED_STATE_HOOK(IASetIndexBufferStripCutValue, (ID3D12GraphicsCommandList *command_list, D3D12_INDEX_BUFFER_STRIP_CUT_VALUE value),
    (command_list, value), state.dynamic.strip_cut=value; state.dynamic.recorded |= kDynamicStripCut)
#undef NR_EXTENDED_STATE_HOOK

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
            if (argument.VertexBuffer.Slot >= 32) info.valid = false;
            break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW:
            break;
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
std::atomic<bool> g_command_signature_hook_published{false};
std::atomic<bool> g_command_signature_start_deferred{false};
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
        case D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW:
            CommandStateVertices(state, argument.VertexBuffer.Slot, 1, nullptr); break;
        case D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW:
            state.dynamic.has_index=false; state.dynamic.recorded |= kDynamicIndex; break;
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
        state.null_pipeline_baseline=false; ++state.pipeline_writes;
        state.dynamic.recorded &= ~(kDynamicDepthBias | kDynamicStripCut);
        if (!pipeline) {state.known=false; state.unknown_reason="invalid-null-DXR-PSO-setter";}
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
        state.known = false; state.unknown_reason = "work-graph-SetProgram"; state.null_guard_tainted = true;
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
    void *queued[64] = {};
    bool *enabled_flags[64] = {};
    if (count > 64) ok = false;
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
    // Command signatures can be created early and reused much later by
    // ExecuteIndirect on the SR carrier list. Observe their immutable
    // descriptions at device creation regardless of the transient master
    // switch snapshot. Delaying this hook produces permanently unknowable
    // signatures and alternating native/NR frames when a game later uses
    // those signatures for weather, map or UI work.
    g_command_signature_start_deferred.store(false,
                                             std::memory_order_release);
    CommandStateHookRequest request{&g_state_CreateCommandSignature,
        reinterpret_cast<void *>(native->lpVtbl->CreateCommandSignature), g_state_detours_CreateCommandSignature};
    if (PublishCommandStateHooks(&request, 1))
        g_command_signature_hook_published.store(true,
                                                 std::memory_order_release);
}
bool EnsureCommandSignatureHook(reshade::api::device *device) {
    if (device == nullptr || device->get_api() != reshade::api::device_api::d3d12)
        return false;
    // Idempotently verify the early observer before publishing the wider
    // command-list state hooks. This is a recovery path for devices that were
    // registered after the normal init callback, not a delayed-start policy.
    OnCommandStateInitDevice(device);
    auto *native = reinterpret_cast<ID3D12Device *>(device->get_native());
    if (native == nullptr) return false;
    CommandStateHookRequest request{&g_state_CreateCommandSignature,
        reinterpret_cast<void *>(native->lpVtbl->CreateCommandSignature), g_state_detours_CreateCommandSignature};
    const bool published = PublishCommandStateHooks(&request, 1);
    if (published)
        g_command_signature_hook_published.store(true,
                                                 std::memory_order_release);
    return published;
}
struct CommandStateExtendedHookRequest { CommandStateHookFamily *family; UINT slot; void *const *detours; };
bool AppendCommandStateInterfaceHooks(ID3D12GraphicsCommandList *native, REFIID iid,
    CommandStateHookRequest *requests, unsigned int &count,
    const CommandStateExtendedHookRequest *extra, unsigned int extra_count) {
    IUnknown *queried=nullptr;
    const HRESULT hr=ID3D12GraphicsCommandList_QueryInterface(native, iid, reinterpret_cast<void **>(&queried));
    if (hr == E_NOINTERFACE) return true;
    if (FAILED(hr) || queried == nullptr) return false;
    if (count > 48 || extra_count > 48-count) { IUnknown_Release(queried); return false; }
    const bool same=reinterpret_cast<void *>(native)==reinterpret_cast<void *>(queried);
    if (same) {
        auto **vtable=*reinterpret_cast<void ***>(queried);
        for (unsigned int i=0; i<extra_count; ++i)
            requests[count++]={extra[i].family, vtable[extra[i].slot], extra[i].detours};
    } else Log("[command-state] extended interface tear-off refused; state cannot be reconstructed.");
    IUnknown_Release(queried);
    return same;
}
bool InstallNativeCommandStateHooks(ID3D12GraphicsCommandList *native, bool observe_pristine_null = false) {
    CommandStateHookRequest requests[48] = {};
    unsigned int count = 0;
    const bool allow_null_pso = AllowExperimentalNullPsoCarrier() || observe_pristine_null;
#define NR_ADD_STATE_HOOK(NAME) requests[count++] = { &g_state_##NAME, reinterpret_cast<void *>(native->lpVtbl->NAME), g_state_detours_##NAME }
    NR_ADD_STATE_HOOK(SetPipelineState); NR_ADD_STATE_HOOK(ClearState);
    NR_ADD_STATE_HOOK(SetDescriptorHeaps); NR_ADD_STATE_HOOK(ExecuteBundle); NR_ADD_STATE_HOOK(ExecuteIndirect);
#define NR_ADD_STATE_STAGE(NAME) \
    NR_ADD_STATE_HOOK(Set##NAME##RootSignature); NR_ADD_STATE_HOOK(Set##NAME##RootDescriptorTable); \
    NR_ADD_STATE_HOOK(Set##NAME##Root32BitConstant); NR_ADD_STATE_HOOK(Set##NAME##Root32BitConstants); \
    NR_ADD_STATE_HOOK(Set##NAME##RootConstantBufferView); NR_ADD_STATE_HOOK(Set##NAME##RootShaderResourceView); \
    NR_ADD_STATE_HOOK(Set##NAME##RootUnorderedAccessView)
    NR_ADD_STATE_STAGE(Compute); NR_ADD_STATE_STAGE(Graphics);
    if (allow_null_pso) {
        NR_ADD_STATE_HOOK(IASetPrimitiveTopology); NR_ADD_STATE_HOOK(RSSetViewports); NR_ADD_STATE_HOOK(RSSetScissorRects);
        NR_ADD_STATE_HOOK(OMSetBlendFactor); NR_ADD_STATE_HOOK(OMSetStencilRef); NR_ADD_STATE_HOOK(IASetIndexBuffer);
        NR_ADD_STATE_HOOK(IASetVertexBuffers); NR_ADD_STATE_HOOK(SOSetTargets); NR_ADD_STATE_HOOK(OMSetRenderTargets);
        NR_ADD_STATE_HOOK(SetPredication);
    }
#undef NR_ADD_STATE_STAGE
#undef NR_ADD_STATE_HOOK
    constexpr GUID list8={0xee936ef9,0x599d,0x4d28,{0x93,0x8e,0x23,0xc4,0xad,0x05,0xce,0x51}};
    constexpr GUID list9={0x34ed2808,0xffe6,0x4c2b,{0xb1,0x1a,0xca,0xbd,0x2b,0x0c,0x59,0xe1}};
#define NR_EXTRA(NAME, SLOT) {&g_state_##NAME, SLOT, g_state_detours_##NAME}
    const CommandStateExtendedHookRequest extra1[]={NR_EXTRA(OMSetDepthBounds,62), NR_EXTRA(SetSamplePositions,63), NR_EXTRA(SetViewInstanceMask,65)};
    const CommandStateExtendedHookRequest extra3[]={NR_EXTRA(SetProtectedResourceSession,67)};
    const CommandStateExtendedHookRequest extra5[]={NR_EXTRA(RSSetShadingRate,77), NR_EXTRA(RSSetShadingRateImage,78)};
    const CommandStateExtendedHookRequest extra8[]={NR_EXTRA(OMSetFrontAndBackStencilRef,81)};
    const CommandStateExtendedHookRequest extra9[]={NR_EXTRA(RSSetDepthBias,82), NR_EXTRA(IASetIndexBufferStripCutValue,83)};
#undef NR_EXTRA
    if (allow_null_pso &&
        (!AppendCommandStateInterfaceHooks(native, IID_ID3D12GraphicsCommandList1, requests,count,extra1,3) ||
         !AppendCommandStateInterfaceHooks(native, IID_ID3D12GraphicsCommandList3, requests,count,extra3,1) ||
         !AppendCommandStateInterfaceHooks(native, IID_ID3D12GraphicsCommandList5, requests,count,extra5,2) ||
         !AppendCommandStateInterfaceHooks(native, list8, requests,count,extra8,1) ||
         !AppendCommandStateInterfaceHooks(native, list9, requests,count,extra9,2))) return false;
    ID3D12GraphicsCommandList4 *extended = nullptr;
    const HRESULT queried4=ID3D12GraphicsCommandList_QueryInterface(native, IID_ID3D12GraphicsCommandList4, reinterpret_cast<void **>(&extended));
    if (queried4 != E_NOINTERFACE && (FAILED(queried4) || extended == nullptr)) return false;
    if (SUCCEEDED(queried4)) {
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
        if (allow_null_pso)
            requests[count++] = { &g_state_BeginRenderPass, reinterpret_cast<void *>(extended->lpVtbl->BeginRenderPass), g_state_detours_BeginRenderPass };
    }
    IUnknown *latest = nullptr;
    const HRESULT queried10 = allow_null_pso ? ID3D12GraphicsCommandList_QueryInterface(
        native, kCommandList10Interface, reinterpret_cast<void **>(&latest)) : E_NOINTERFACE;
    if (queried10 != E_NOINTERFACE && (FAILED(queried10) || latest == nullptr)) {
        if (extended != nullptr) ID3D12GraphicsCommandList4_Release(extended);
        return false;
    }
    if (SUCCEEDED(queried10)) {
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
    const char *registration_failure = nullptr;
    AcquireSRWLockExclusive(&g_command_state_lock);
    if (FindCommandStateTokenLocked(ReadCommandStateToken(native)) == nullptr) {
        registration_failure = "registry-capacity";
        for (auto &record : g_command_states) if (record.token == 0) {
            const auto token = g_next_command_state_token++;
            if (SUCCEEDED(ID3D12GraphicsCommandList_SetPrivateData(native, kCommandStateIdentity, sizeof(token), &token))) {
                record.token = token; record.native = native; record.owner = owner; record.meta_contract = meta;
                g_command_state_epoch.fetch_add(1, std::memory_order_release);
                registration_failure = nullptr;
            } else registration_failure = "private-data-publication";
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_command_state_lock);
    if (registration_failure) {
        static std::atomic<unsigned int> failures{0};
        if (failures.fetch_add(1, std::memory_order_relaxed) < 6)
            Log("[command-state] list registration failed: reason=%s; no state is inferred.", registration_failure);
    }
}

void OnCommandStateObserveSubmission(reshade::api::command_list *owner) {
    if (!g_command_state_registration_repair_needed.load(std::memory_order_relaxed)) return;
    if (!owner || !owner->get_device() ||
        owner->get_device()->get_api() != reshade::api::device_api::d3d12) return;
    auto *native = reinterpret_cast<ID3D12GraphicsCommandList *>(owner->get_native());
    if (!native || ID3D12GraphicsCommandList_GetType(native) != D3D12_COMMAND_LIST_TYPE_DIRECT) return;
    const auto token = ReadCommandStateToken(native);
    AcquireSRWLockShared(&g_command_state_lock);
    const bool registered = FindCommandStateTokenLocked(token) != nullptr;
    ReleaseSRWLockShared(&g_command_state_lock);
    if (registered) return;
    // Real submission proves only the owner mapping, never the current bindings.
    OnCommandStateInitList(owner);
    const auto observed_token = ReadCommandStateToken(native);
    AcquireSRWLockShared(&g_command_state_lock);
    const bool recovered = FindCommandStateTokenLocked(observed_token) != nullptr;
    ReleaseSRWLockShared(&g_command_state_lock);
    static std::atomic<unsigned int> reports{0};
    if (reports.fetch_add(1, std::memory_order_relaxed) < 3)
        Log("[command-state] late ReShade list observation: registered=%d; baseline remains unknown until native Reset.",
            recovered ? 1 : 0);
}
void CompleteCommandStateInstallation(unsigned long long token,
        ID3D12GraphicsCommandList *native, bool ready, bool observed_null) {
    AcquireSRWLockExclusive(&g_command_state_lock);
    auto *record = FindCommandStateTokenLocked(token);
    if (record && record->native == native && record->state) {
        record->hooks_ready = ready;
        record->next_hook_attempt = ready ? 0 : GetTickCount64() + 1000;
        if (ready && observed_null && !record->state->null_guard_covered) {
            record->state->null_guard_covered = true;
            record->state->null_guard_reset_observed = false;
            record->state->known = false;
            record->state->unknown_reason = "null-guard-awaiting-reset";
        }
    }
    ReleaseSRWLockExclusive(&g_command_state_lock);
}
void OnCommandStateBeforeReset(reshade::api::command_list *owner) {
    if (owner == nullptr || owner->get_device() == nullptr || owner->get_device()->get_api() != reshade::api::device_api::d3d12) return;
    auto *native = reinterpret_cast<ID3D12GraphicsCommandList *>(owner->get_native());
    if (ReadCommandStateToken(native) == 0) OnCommandStateInitList(owner);
    const auto token = ReadCommandStateToken(native);
    bool install = false, observe_null = false;
    AcquireSRWLockExclusive(&g_command_state_lock);
    auto *record = FindCommandStateTokenLocked(token);
    if (record != nullptr && record->native != native) {
        record->native = native; record->hooks_ready = false; record->next_hook_attempt = 0;
        if (record->state != nullptr) {
            record->state->known = false;
            record->state->null_guard_covered = false;
            record->state->null_guard_reset_observed = false;
        }
        g_command_state_epoch.fetch_add(1, std::memory_order_release);
    }
    if (record && record->state && !record->hooks_ready) {
        const ULONGLONG now = GetTickCount64();
        if (now >= record->next_hook_attempt) {
            record->next_hook_attempt = now + 1000;
            install = true;
            observe_null = record->null_guard_requested;
        }
    }
    ReleaseSRWLockExclusive(&g_command_state_lock);
    if (install) {
        const bool ready = EnsureCommandSignatureHook(owner->get_device()) &&
            InstallNativeCommandStateHooks(native, observe_null) &&
            EnsureNativeCommandListResetHook(owner);
        CompleteCommandStateInstallation(token, native, ready, observe_null);
    }
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
    if (command_list == nullptr || ID3D12GraphicsCommandList_GetType(command_list) != D3D12_COMMAND_LIST_TYPE_DIRECT) {
        CommandStateEvidence rejected;
        rejected.available = true; rejected.reason = "direct-list-required";
        rejected.sampled_at = GetTickCount64();
        AcquireSRWLockExclusive(&g_command_state_lock);
        rejected.sequence = g_command_state_evidence.sequence + 1;
        g_command_state_evidence = rejected;
        ReleaseSRWLockExclusive(&g_command_state_lock);
        SetBypassReason(BypassReason::CommandStateUnverified);
        return false;
    }
    const auto token = ReadCommandStateToken(command_list);
    reshade::api::command_list *owner = nullptr;
    ID3D12GraphicsCommandList *native = nullptr;
    bool needs_install = false, observe_null = false, captured = false;
    const bool experimental = AllowExperimentalNullPsoCarrier();
    const char *unavailable_reason = "unregistered-list";
    CommandStateEvidence evidence;
    evidence.available = true; evidence.token = token; evidence.sampled_at = GetTickCount64();
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
        const bool meta_covered = record->meta_contract.known &&
            ((record->meta_contract.initialization_dirty | record->meta_contract.execution_dirty) & ~kCommandStateCoveredDomains) == 0;
        // The recording predates guard publication: never treat its zero mask
        // as proof of pristine graphics. A new successful Reset is mandatory.
        if (!experimental && meta_covered && record->state && record->hooks_ready &&
            record->state->known && record->state->null_pipeline_baseline &&
            !record->null_guard_requested) {
            record->null_guard_requested = true;
            record->hooks_ready = false;
            record->next_hook_attempt = 0;
            record->state->known = false;
            record->state->unknown_reason = "null-guard-awaiting-reset";
        }
        if (record->state && !record->hooks_ready && evidence.sampled_at >= record->next_hook_attempt) {
            needs_install = true;
            record->next_hook_attempt = evidence.sampled_at + 1000;
        }
        observe_null = record->null_guard_requested;
        const bool pristine = record->state && CanAutoRestorePristineNull(*record->state);
        const bool raster = record->state && CanAutoRestoreRasterNull(*record->state);
        captured = meta_covered && record->state != nullptr && record->hooks_ready &&
            CanRestoreNativeCommandState(*record->state,
                experimental || pristine || raster);
        unavailable_reason = !meta_covered ? "meta-command-coverage" : record->state == nullptr ? "snapshot-allocation" :
            !record->hooks_ready ? "native-hooks-unavailable" : !record->state->known ? record->state->unknown_reason :
            record->state->unknown_domains != 0 ? "meta-command-dirty-state" :
            (record->state->dynamic.recorded & (kDynamicDepthBias | kDynamicStripCut)) && !record->state->pipeline ? "dynamic-raster-without-PSO" :
            record->state->null_pipeline_baseline && !experimental && !pristine && !raster ?
                (record->state->null_guard_tainted ? "null-guard-recording-tainted" :
                 !record->state->null_guard_reset_observed ? "null-guard-awaiting-reset" : "null-guard-graphics-state-used") : "pipeline-unbound";
        evidence.registered = true; evidence.hooks_ready = record->hooks_ready;
        evidence.guard_requested = record->null_guard_requested;
        evidence.meta_known = record->meta_contract.known;
        evidence.meta_init = record->meta_contract.initialization_dirty;
        evidence.meta_execute = record->meta_contract.execution_dirty;
        if (record->state != nullptr) {
            const auto &state = *record->state;
            evidence.dirty = state.unknown_domains;
            evidence.reset_serial = state.reset_serial; evidence.pipeline_writes = state.pipeline_writes;
            evidence.baseline = state.baseline_origin; evidence.null_baseline = state.null_pipeline_baseline;
            evidence.guard_covered = state.null_guard_covered; evidence.fresh_reset = state.null_guard_reset_observed;
            evidence.tainted = state.null_guard_tainted;
            evidence.dynamic = state.dynamic.recorded;
            evidence.vertex_mask = state.dynamic.vertex_mask; evidence.stream_mask = state.dynamic.stream_mask;
            evidence.has_pipeline = state.pipeline != nullptr; evidence.has_ray_pipeline = state.raytracing_pipeline != nullptr;
            for (unsigned stage = 0; stage < 2; ++stage) {
                evidence.roots[stage] = state.roots[stage] != nullptr;
                evidence.heaps[stage] = state.heaps[stage] != nullptr;
                for (const auto &argument : state.arguments[stage])
                    if (argument.kind != CommandRootKind::Empty) ++evidence.arguments[stage];
            }
        }
        if (captured) {
            captured = CopyNativeCommandState(snapshot, *record->state);
            if (!captured) unavailable_reason = "dynamic-descriptor-snapshot-allocation";
            if (captured && native_output != nullptr) *native_output = native;
        }
        if (captured) evidence.route = pristine && !experimental ? "pristine-null" :
            raster && !experimental ? "guarded-null-raster" :
            evidence.has_pipeline ? "typed-pso" : evidence.has_ray_pipeline ? "dxr-pso" : "experimental-null";
    }
    // A different, registered view can evaluate before this missing list is
    // submitted. It must not erase the pending recovery request (SF6/multi-view).
    // Keep it sticky; the submission caller already gates work on master/traffic.
    if (record == nullptr)
        g_command_state_registration_repair_needed.store(true, std::memory_order_relaxed);
    evidence.captured = captured;
    evidence.reason = captured ? "none" : unavailable_reason;
    evidence.sequence = g_command_state_evidence.sequence + 1;
    const bool changed_reason = std::strcmp(evidence.reason, g_command_state_evidence.reason) != 0;
    g_command_state_evidence = evidence;
    ReleaseSRWLockExclusive(&g_command_state_lock);
    if (needs_install) {
        const bool ready = owner != nullptr &&
            EnsureCommandSignatureHook(owner->get_device()) &&
            InstallNativeCommandStateHooks(native, observe_null) &&
            EnsureNativeCommandListResetHook(owner);
        CompleteCommandStateInstallation(token, native, ready, observe_null);
    }
    if (!captured) {
        const auto count = g_command_state_bypasses.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count <= 6 || (count % 600) == 0 || (changed_reason && count <= 60))
            Log("[command-state] NR bypass: reason=%s token=%llu registered=%d installing=%d dirty=0x%X count=%llu baseline=%s resetSerial=%llu pipelineWrites=%llu nullBaseline=%d guard=%d/%d/%d tainted=%d dynamic=0x%llX vb=0x%X so=0x%X; unknown state remains native.",
                unavailable_reason, token, evidence.registered ? 1 : 0, needs_install ? 1 : 0, evidence.dirty, count,
                evidence.baseline, evidence.reset_serial, evidence.pipeline_writes, evidence.null_baseline ? 1 : 0,
                evidence.guard_requested ? 1 : 0, evidence.guard_covered ? 1 : 0, evidence.fresh_reset ? 1 : 0,
                evidence.tainted ? 1 : 0, evidence.dynamic, evidence.vertex_mask, evidence.stream_mask);
        SetBypassReason(BypassReason::CommandStateUnverified);
    } else if (snapshot.null_pipeline_baseline) {
        static std::atomic<unsigned int> null_snapshots{0};
        if (null_snapshots.fetch_add(1, std::memory_order_relaxed) < 6)
            Log("[command-state] known-null snapshot: token=%llu baseline=%s resetSerial=%llu route=%s dynamic=0x%llX; native ClearState(NULL) restores the dummy PSO.",
                token, snapshot.baseline_origin, snapshot.reset_serial, evidence.route, snapshot.dynamic.recorded);
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
        if (saved->null_pipeline_baseline) {
            static std::atomic<unsigned int> null_restores{0};
            if (null_restores.fetch_add(1, std::memory_order_relaxed) < 3)
                Log("[command-state] restored known-null baseline: token=%llu origin=%s resetSerial=%llu dynamic=0x%llX.",
                    token, saved->baseline_origin, saved->reset_serial, saved->dynamic.recorded);
        }
        active = false;
    }
    NativeCommandStateScope(const NativeCommandStateScope &) = delete;
    NativeCommandStateScope &operator=(const NativeCommandStateScope &) = delete;
};
