// Included after the complete production Core by the existing queue host.
// Real WARP native objects, production recording/submit/Reset/collector code;
// registration metadata and the provider-release observer are controlled inputs.
// No NR/NGX model, shader, ReShade game host or game is loaded.
namespace queue_test {
using namespace reshade::api;

// Concrete implementations of the real SDK interfaces, used only to enter the
// actual OnDestroyCommandList callback. Unexpected API use fails immediately;
// these do not replace the native object, its COM lifetime or any NR consumer.
[[noreturn]] void UnexpectedNativeOwnerMethod(const char* method) {
    std::printf("Unexpected owner API: %s\n",method);
    Check(false,"native owner callback only queries object/device identity");
    std::abort();
}
#define NR_EVENT_UNEXPECTED(...) __VA_ARGS__ override { UnexpectedNativeOwnerMethod(#__VA_ARGS__); }
struct NativeEventDevice final : reshade::api::device {
    ID3D12Device* native;
    explicit NativeEventDevice(ID3D12Device* value):native(value){}
    uint64_t get_native() const override { return reinterpret_cast<uint64_t>(native); }
    NR_EVENT_UNEXPECTED(void get_private_data(const uint8_t guid[16], uint64_t *data) const)
    NR_EVENT_UNEXPECTED(void set_private_data(const uint8_t guid[16], const uint64_t data))
    device_api get_api() const override { return device_api::d3d12; }
    NR_EVENT_UNEXPECTED(bool check_capability(device_caps capability) const)
    NR_EVENT_UNEXPECTED(bool check_format_support(format format, resource_usage usage) const)
    NR_EVENT_UNEXPECTED(bool create_sampler(const sampler_desc &desc, sampler *out_sampler))
    NR_EVENT_UNEXPECTED(void destroy_sampler(sampler sampler))
    NR_EVENT_UNEXPECTED(bool create_resource(const resource_desc &desc, const subresource_data *initial_data, resource_usage initial_state, resource *out_resource, void **shared_handle = nullptr))
    NR_EVENT_UNEXPECTED(void destroy_resource(resource resource))
    NR_EVENT_UNEXPECTED(resource_desc get_resource_desc(resource resource) const)
    NR_EVENT_UNEXPECTED(bool create_resource_view(resource resource, resource_usage usage_type, const resource_view_desc &desc, resource_view *out_view))
    NR_EVENT_UNEXPECTED(void destroy_resource_view(resource_view view))
    NR_EVENT_UNEXPECTED(resource get_resource_from_view(resource_view view) const)
    NR_EVENT_UNEXPECTED(resource_view_desc get_resource_view_desc(resource_view view) const)
    NR_EVENT_UNEXPECTED(bool map_buffer_region(resource resource, uint64_t offset, uint64_t size, map_access access, void **out_data))
    NR_EVENT_UNEXPECTED(void unmap_buffer_region(resource resource))
    NR_EVENT_UNEXPECTED(bool map_texture_region(resource resource, uint32_t subresource, const subresource_box *box, map_access access, subresource_data *out_data))
    NR_EVENT_UNEXPECTED(void unmap_texture_region(resource resource, uint32_t subresource))
    NR_EVENT_UNEXPECTED(void update_buffer_region(const void *data, resource dest, uint64_t dest_offset, uint64_t size))
    NR_EVENT_UNEXPECTED(void update_texture_region(const subresource_data &data, resource dest, uint32_t dest_subresource, const subresource_box *dest_box = nullptr))
    NR_EVENT_UNEXPECTED(bool create_pipeline(pipeline_layout layout, uint32_t subobject_count, const pipeline_subobject *subobjects, pipeline *out_pipeline))
    NR_EVENT_UNEXPECTED(void destroy_pipeline(pipeline pipeline))
    NR_EVENT_UNEXPECTED(bool create_pipeline_layout(uint32_t param_count, const pipeline_layout_param *params, pipeline_layout *out_layout))
    NR_EVENT_UNEXPECTED(void destroy_pipeline_layout(pipeline_layout layout))
    NR_EVENT_UNEXPECTED(bool allocate_descriptor_tables(uint32_t count, pipeline_layout layout, uint32_t param, descriptor_table *out_tables))
    NR_EVENT_UNEXPECTED(void free_descriptor_tables(uint32_t count, const descriptor_table *tables))
    NR_EVENT_UNEXPECTED(void get_descriptor_heap_offset(descriptor_table table, uint32_t binding, uint32_t array_offset, descriptor_heap *out_heap, uint32_t *out_offset) const)
    NR_EVENT_UNEXPECTED(void copy_descriptor_tables(uint32_t count, const descriptor_table_copy *copies))
    NR_EVENT_UNEXPECTED(void update_descriptor_tables(uint32_t count, const descriptor_table_update *updates))
    NR_EVENT_UNEXPECTED(bool create_query_heap(query_type type, uint32_t count, query_heap *out_heap))
    NR_EVENT_UNEXPECTED(void destroy_query_heap(query_heap heap))
    NR_EVENT_UNEXPECTED(bool get_query_heap_results(query_heap heap, uint32_t first, uint32_t count, void *results, uint32_t stride))
    NR_EVENT_UNEXPECTED(void set_resource_name(resource resource, const char *name))
    NR_EVENT_UNEXPECTED(void set_resource_view_name(resource_view view, const char *name))
    NR_EVENT_UNEXPECTED(bool create_fence(uint64_t initial_value, fence_flags flags, fence *out_fence, void **shared_handle = nullptr))
    NR_EVENT_UNEXPECTED(void destroy_fence(fence fence))
    NR_EVENT_UNEXPECTED(uint64_t get_completed_fence_value(fence fence) const)
    NR_EVENT_UNEXPECTED(bool wait(fence fence, uint64_t value, uint64_t timeout = UINT64_MAX))
    NR_EVENT_UNEXPECTED(bool signal(fence fence, uint64_t value))
    NR_EVENT_UNEXPECTED(bool get_property(device_properties property, void *data) const)
    NR_EVENT_UNEXPECTED(uint64_t get_resource_view_gpu_address(resource_view view) const)
    NR_EVENT_UNEXPECTED(void get_acceleration_structure_size(acceleration_structure_type type, acceleration_structure_build_flags flags, uint32_t input_count, const acceleration_structure_build_input *inputs, uint64_t *out_size, uint64_t *out_build_scratch_size, uint64_t *out_update_scratch_size) const)
    NR_EVENT_UNEXPECTED(bool get_pipeline_shader_group_handles(pipeline pipeline, uint32_t first, uint32_t count, void *out_handles))
};
struct NativeEventList final : reshade::api::command_list {
    ID3D12GraphicsCommandList* native; reshade::api::device* parent;
    NativeEventList(ID3D12GraphicsCommandList* value,reshade::api::device* device):native(value),parent(device){}
    uint64_t get_native() const override { return reinterpret_cast<uint64_t>(native); }
    NR_EVENT_UNEXPECTED(void get_private_data(const uint8_t guid[16], uint64_t *data) const)
    NR_EVENT_UNEXPECTED(void set_private_data(const uint8_t guid[16], const uint64_t data))
    device *get_device() override { return parent; }
    NR_EVENT_UNEXPECTED(void barrier(uint32_t count, const resource *resources, const resource_usage *old_states, const resource_usage *new_states))
    NR_EVENT_UNEXPECTED(void begin_render_pass(uint32_t count, const render_pass_render_target_desc *rts, const render_pass_depth_stencil_desc *ds = nullptr))
    NR_EVENT_UNEXPECTED(void end_render_pass())
    NR_EVENT_UNEXPECTED(void bind_render_targets_and_depth_stencil(uint32_t count, const resource_view *rtvs, resource_view dsv = { 0 }))
    NR_EVENT_UNEXPECTED(void bind_pipeline(pipeline_stage stages, pipeline pipeline))
    NR_EVENT_UNEXPECTED(void bind_pipeline_states(uint32_t count, const dynamic_state *states, const uint32_t *values))
    NR_EVENT_UNEXPECTED(void bind_viewports(uint32_t first, uint32_t count, const viewport *viewports))
    NR_EVENT_UNEXPECTED(void bind_scissor_rects(uint32_t first, uint32_t count, const rect *rects))
    NR_EVENT_UNEXPECTED(void push_constants(shader_stage stages, pipeline_layout layout, uint32_t param, uint32_t first, uint32_t count, const void *values))
    NR_EVENT_UNEXPECTED(void push_descriptors(shader_stage stages, pipeline_layout layout, uint32_t param, const descriptor_table_update &update))
    NR_EVENT_UNEXPECTED(void bind_descriptor_tables(shader_stage stages, pipeline_layout layout, uint32_t first, uint32_t count, const descriptor_table *tables))
    NR_EVENT_UNEXPECTED(void bind_index_buffer(resource buffer, uint64_t offset, uint32_t index_size))
    NR_EVENT_UNEXPECTED(void bind_vertex_buffers(uint32_t first, uint32_t count, const resource *buffers, const uint64_t *offsets, const uint32_t *strides))
    NR_EVENT_UNEXPECTED(void bind_stream_output_buffers(uint32_t first, uint32_t count, const resource *buffers, const uint64_t *offsets, const uint64_t *max_sizes, const resource *counter_buffers, const uint64_t *counter_offsets))
    NR_EVENT_UNEXPECTED(void draw(uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance))
    NR_EVENT_UNEXPECTED(void draw_indexed(uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance))
    NR_EVENT_UNEXPECTED(void dispatch(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z))
    NR_EVENT_UNEXPECTED(void draw_or_dispatch_indirect(indirect_command type, resource buffer, uint64_t offset, uint32_t draw_count, uint32_t stride))
    NR_EVENT_UNEXPECTED(void copy_resource(resource source, resource dest))
    NR_EVENT_UNEXPECTED(void copy_buffer_region(resource source, uint64_t source_offset, resource dest, uint64_t dest_offset, uint64_t size))
    NR_EVENT_UNEXPECTED(void copy_buffer_to_texture(resource source, uint64_t source_offset, uint32_t row_length, uint32_t slice_height, resource dest, uint32_t dest_subresource, const subresource_box *dest_box = nullptr))
    NR_EVENT_UNEXPECTED(void copy_texture_region(resource source, uint32_t source_subresource, const subresource_box *source_box, resource dest, uint32_t dest_subresource, const subresource_box *dest_box, filter_mode filter = filter_mode::min_mag_mip_point))
    NR_EVENT_UNEXPECTED(void copy_texture_to_buffer(resource source, uint32_t source_subresource, const subresource_box *source_box, resource dest, uint64_t dest_offset, uint32_t row_length = 0, uint32_t slice_height = 0))
    NR_EVENT_UNEXPECTED(void resolve_texture_region(resource source, uint32_t source_subresource, const subresource_box *source_box, resource dest, uint32_t dest_subresource, uint32_t dest_x, uint32_t dest_y, uint32_t dest_z, format format))
    NR_EVENT_UNEXPECTED(void clear_depth_stencil_view(resource_view dsv, const float *depth, const uint8_t *stencil, uint32_t rect_count = 0, const rect *rects = nullptr))
    NR_EVENT_UNEXPECTED(void clear_render_target_view(resource_view rtv, const float color[4], uint32_t rect_count = 0, const rect *rects = nullptr))
    NR_EVENT_UNEXPECTED(void clear_unordered_access_view_uint(resource_view uav, const uint32_t values[4], uint32_t rect_count = 0, const rect *rects = nullptr))
    NR_EVENT_UNEXPECTED(void clear_unordered_access_view_float(resource_view uav, const   float values[4], uint32_t rect_count = 0, const rect *rects = nullptr))
    NR_EVENT_UNEXPECTED(void generate_mipmaps(resource_view srv))
    NR_EVENT_UNEXPECTED(void begin_query(query_heap heap, query_type type, uint32_t index))
    NR_EVENT_UNEXPECTED(void end_query(query_heap heap, query_type type, uint32_t index))
    NR_EVENT_UNEXPECTED(void copy_query_heap_results(query_heap heap, query_type type, uint32_t first, uint32_t count, resource dest, uint64_t dest_offset, uint32_t stride))
    NR_EVENT_UNEXPECTED(void begin_debug_event(const char *label, const float color[4] = nullptr))
    NR_EVENT_UNEXPECTED(void end_debug_event())
    NR_EVENT_UNEXPECTED(void insert_debug_marker(const char *label, const float color[4] = nullptr))
    NR_EVENT_UNEXPECTED(void dispatch_mesh(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z))
    NR_EVENT_UNEXPECTED(void dispatch_rays(resource raygen, uint64_t raygen_offset, uint64_t raygen_size, resource miss, uint64_t miss_offset, uint64_t miss_size, uint64_t miss_stride, resource hit_group, uint64_t hit_group_offset, uint64_t hit_group_size, uint64_t hit_group_stride, resource callable, uint64_t callable_offset, uint64_t callable_size, uint64_t callable_stride, uint32_t width, uint32_t height, uint32_t depth))
    NR_EVENT_UNEXPECTED(void copy_acceleration_structure(resource_view source, resource_view dest, acceleration_structure_copy_mode mode))
    NR_EVENT_UNEXPECTED(void build_acceleration_structure(acceleration_structure_type type, acceleration_structure_build_flags flags, uint32_t input_count, const acceleration_structure_build_input *inputs, resource scratch, uint64_t scratch_offset, resource_view source, resource_view dest, acceleration_structure_build_mode mode))
    NR_EVENT_UNEXPECTED(void query_acceleration_structures(uint32_t count, const resource_view *acceleration_structures, query_heap heap, query_type type, uint32_t first))
    NR_EVENT_UNEXPECTED(void update_buffer_region(const void *data, resource dest, uint64_t dest_offset, uint64_t size))
    NR_EVENT_UNEXPECTED(void update_texture_region(const subresource_data &data, resource dest, uint32_t dest_subresource, const subresource_box *dest_box = nullptr))
};

#undef NR_EVENT_UNEXPECTED

unsigned native_owner_releases=0;
bool native_final_release_active=false, native_collector_active=false;
DWORD WINAPI TryNativeReleaseChainLock(void*) {
    if(!TryEnterCriticalSection(&g_chain_cs))return 1;
    LeaveCriticalSection(&g_chain_cs);return 0;
}
NgxResult NativeOwnerRelease(void* entry,void* handle) {
    Check(entry==reinterpret_cast<void*>(&NativeOwnerRelease) &&
          handle==reinterpret_cast<void*>(static_cast<uintptr_t>(340)),
          "controlled provider Release receives the exact owned handle");
    Check(!native_final_release_active && !native_collector_active,
          "NATIVE_RELEASE_MUST_RUN_ONLY_IN_OUTSIDE_LOCK_DRAIN");
    // A different thread proves this is not a recursive acquisition by the
    // final-Release or collector thread. No sleep or GPU wait is involved.
    HANDLE thread=CreateThread(nullptr,0,TryNativeReleaseChainLock,nullptr,0,nullptr);
    Check(thread!=nullptr,"chain-lock observer thread created");
    Check(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0,"chain-lock observer finished");
    DWORD code=99;Check(GetExitCodeThread(thread,&code)!=FALSE,"chain-lock result read");
    CloseHandle(thread);
    Check(code==0,"NATIVE_RELEASE_DRAIN_REQUIRES_UNLOCKED_CHAIN");
    ++native_owner_releases;return kNgxSuccess;
}
NrContext& NativeOwner() {
    auto& nr=g_contexts[0];
    nr.sr_handle=reinterpret_cast<const NgxHandle*>(static_cast<uintptr_t>(339));
    nr.nr_handle=reinterpret_cast<void*>(static_cast<uintptr_t>(340));
    nr.instance_id=339;
    g_nr_api.initialized=true;
    g_nr_api.signed_release=reinterpret_cast<void*>(&NativeOwnerRelease);
    g_nr_api.bridge_release=NativeOwnerRelease;
    return nr;
}
void NativeCollect() {
    native_collector_active=true;
    EnterCriticalSection(&g_chain_cs);CollectRetiredContextsLocked();LeaveCriticalSection(&g_chain_cs);
    native_collector_active=false;
}
void NativeFinalRelease(ID3D12GraphicsCommandList*& native) {
    native_final_release_active=true;ReleaseCom(native);native_final_release_active=false;
}
unsigned long long NativeRecord(List& list,NrContext& nr) {
    Check(RecordNrUseLocked(list.native,nr,nr.slots[0]),
          "actual NR recording producer pins the native list");
    unsigned long long token=0;
    Check(ReadLifecycleToken(list.native,token),"NR token captured while list is alive");
    return token;
}
void NativeSubmitAndComplete(List& list,Fixture& f,NrContext& nr) {
    list.Close();list.Submit(f.a);
    Check(!g_lifecycle_tracking_uncertain.load(),"one-shot native submit has exact proof");
    Wait(nr.last_submission_queue->fence,nr.completion_fence_value);
    Check(IsContextFenceCompleteLocked(nr),"exact submitted context fence completed");
}
void NativeFinishDeath(ID3D12GraphicsCommandList*& native,UINT64 state_token,
        unsigned long long token,NrContext& nr) {
    NativeFinalRelease(native);
    Check(!FindCommandStateTokenLocked(state_token),"final native destruction retires observation state");
    Check(FindLifecycleCommandListLocked(token) && nr.recording_command_lists==1 &&
          nr.slots[0].recording_command_lists==1 && !native_owner_releases &&
          !g_pending_destroy_contexts[0].sr_handle,
          "FINAL_NATIVE_RELEASE_MUST_ONLY_PUBLISH_DEATH");
    NativeCollect();
    std::printf("NATIVE_NR_AFTER_FINAL stateGone=1 ledgerPresent=%u recording=%u slot=%u retiredLive=%u pendingDestroy=%u\n",
        FindLifecycleCommandListLocked(token)!=nullptr,nr.recording_command_lists,
        nr.slots[0].recording_command_lists,nr.sr_handle!=nullptr,
        g_pending_destroy_contexts[0].sr_handle!=nullptr);
    Check(!FindLifecycleCommandListLocked(token) && !nr.recording_command_lists &&
          !nr.slots[0].recording_command_lists,"NATIVE_FINAL_DESTROY_MUST_RETIRE_NR_RECORDING");
    Check(g_pending_destroy_contexts[0].nr_handle && !native_owner_releases,
          "original collector schedules ownership without provider Release");
    NativeCollect();
    Check(!g_lifecycle_tracking_uncertain.load() && !g_lifecycle_active_command_lists.load() &&
          g_pending_destroy_contexts[0].nr_handle && !native_owner_releases,
          "repeated collector consumes native death exactly once");
    DrainPendingContextDestructions();
    Check(!g_contexts[0].sr_handle&&!g_pending_destroy_contexts[0].sr_handle && native_owner_releases==1,
          "existing collector and outside-lock drain retire a native-destroyed owner");
}

void NativeNrRetirement(bool replace_marker) {
    Fixture f;
    auto list=std::make_unique<List>(f);
    const auto state_token=ReadCommandStateToken(list->native);
    Check(state_token && FindCommandStateTokenLocked(state_token),
          "native command-state registration exists");
    auto& nr=NativeOwner();
    const auto token=NativeRecord(*list,nr);
    NativeSubmitAndComplete(*list,f,nr);nr.retired=true;
    Check(!CanDestroyContextLocked(nr),"completed fence does not retire a replayable native list");
    auto* retained=list->native;
    ID3D12GraphicsCommandList_AddRef(retained);
    ReleaseCom(list->native);list.reset();NativeCollect();
    Check(FindCommandStateTokenLocked(state_token) && nr.recording_command_lists==1 &&
          nr.slots[0].recording_command_lists==1 && !g_pending_destroy_contexts[0].sr_handle,
          "extra raw reference retains replayable recording after first owner exits");
    if(replace_marker) {
        Hr(ID3D12GraphicsCommandList_SetPrivateDataInterface(retained,kNativeCommandLifetime,nullptr),
           "replace native observation marker without destroying the list");
        NativeCollect();
        Check(!FindCommandStateTokenLocked(state_token),"marker replacement retires observation state");
        Check(nr.recording_command_lists==1 && nr.slots[0].recording_command_lists==1 &&
              !g_pending_destroy_contexts[0].sr_handle && !CanDestroyContextLocked(nr),
              "private-data marker replacement is not native destruction proof");
    }
    std::printf("NATIVE_NR_BEFORE_FINAL stateToken=%llu token=%llu recording=%u slot=%u pending=%llu fence=%llu completed=%llu markerReplaced=%u\n",
        state_token,token,nr.recording_command_lists,nr.slots[0].recording_command_lists,
        nr.pending_recording_token,nr.completion_fence_value,
        ID3D12Fence_GetCompletedValue(nr.last_submission_queue->fence),replace_marker);
    NativeFinishDeath(retained,state_token,token,nr);
}

void NativeWrapperRetention(bool replace_marker=false) {
    Fixture f;List list(f);
    NativeEventDevice device(f.device);
    NativeEventList owner(list.native,&device);
    OnCommandStateInitList(&owner);
    const auto state_token=ReadCommandStateToken(list.native);
    Check(FindCommandStateTokenLocked(state_token) &&
          FindCommandStateTokenLocked(state_token)->owner==&owner,
          "actual ReShade init path attaches the concrete SDK event owner");
    auto& nr=NativeOwner();const auto token=NativeRecord(list,nr);
    NativeSubmitAndComplete(list,f,nr);nr.retired=true;
    auto* retained=list.native;ID3D12GraphicsCommandList_AddRef(retained);
    if(replace_marker) {
        Hr(ID3D12GraphicsCommandList_SetPrivateDataInterface(retained,kNativeCommandLifetime,nullptr),
           "observation marker disappears before its wrapper exits");
        Check(!FindCommandStateTokenLocked(state_token),"replaced marker retires only observation state");
    }
    OnDestroyCommandList(&owner); // Actual production destroy callback.
    ReleaseCom(list.native);NativeCollect();
    unsigned long long observed=0;
    Check(ReadLifecycleToken(retained,observed) && observed==token &&
          FindLifecycleCommandListLocked(token) && nr.recording_command_lists==1 &&
          nr.slots[0].recording_command_lists==1 && nr.sr_handle && !native_owner_releases &&
          !g_pending_destroy_contexts[0].sr_handle,
          "NATIVE_WRAPPER_DESTROY_MUST_RETAIN_NR_RECORDING");
    Check(replace_marker?!FindCommandStateTokenLocked(state_token):
          FindCommandStateTokenLocked(state_token) && !FindCommandStateTokenLocked(state_token)->owner,
          "wrapper exit preserves marker retirement or clears only the optional owner mapping");
    NativeFinishDeath(retained,state_token,token,nr);
}

HRESULT STDMETHODCALLTYPE NativeReset(ID3D12GraphicsCommandList* list,
        ID3D12CommandAllocator* allocator,ID3D12PipelineState* pso) {
    return ID3D12GraphicsCommandList_Reset(list,allocator,pso);
}
HRESULT STDMETHODCALLTYPE NativeFailedReset(ID3D12GraphicsCommandList*,
        ID3D12CommandAllocator*,ID3D12PipelineState*) { return E_FAIL; }
void NativeResetRetirement(bool fail) {
    Fixture f;List list(f);auto& nr=NativeOwner();
    const auto state_token=ReadCommandStateToken(list.native);
    const auto old_token=NativeRecord(list,nr);NativeSubmitAndComplete(list,f,nr);
    Hr(ID3D12Device_CreateCommandAllocator(f.device,D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_ID3D12CommandAllocator,reinterpret_cast<void**>(&list.replacement)),"native Reset replacement allocator");
    g_original_command_list_reset[0]=fail?NativeFailedReset:NativeReset;
    const auto result=ProcessCommandListReset(0,list.native,list.replacement,nullptr);
    unsigned long long observed=0;
    if(fail) {
        Check(result==E_FAIL,"controlled failed native Reset result reaches the production success gate");
        Check(ReadLifecycleToken(list.native,observed) && observed==old_token &&
              FindLifecycleCommandListLocked(old_token) && nr.recording_command_lists==1 &&
              nr.slots[0].recording_command_lists==1 && !native_owner_releases,
              "NATIVE_FAILED_RESET_MUST_RETAIN_NR_TOKEN");
        nr.retired=true;
        NativeFinishDeath(list.native,state_token,old_token,nr);
        return;
    }
    Hr(result,"real native Reset returned success through production gate");
    Check(!ReadLifecycleToken(list.native,observed) && !FindLifecycleCommandListLocked(old_token) &&
          !nr.recording_command_lists && !nr.slots[0].recording_command_lists && nr.sr_handle &&
          !native_owner_releases,"NATIVE_SUCCESSFUL_RESET_MUST_CONSUME_OLD_NR_TOKEN");
    const auto new_token=NativeRecord(list,nr);
    Check(new_token!=old_token && nr.recording_command_lists==1 && nr.slots[0].recording_command_lists==1,
          "fresh recording acquires a distinct NR reset token");
    NativeCollect();
    Check(FindLifecycleCommandListLocked(new_token) && nr.recording_command_lists==1 &&
          nr.slots[0].recording_command_lists==1 && !g_lifecycle_tracking_uncertain.load(),
          "old Reset consumption does not consume the new recording");
    NativeSubmitAndComplete(list,f,nr);nr.retired=true;
    NativeFinishDeath(list.native,state_token,new_token,nr);
}

void NativeDeathPendingFence() {
    Fixture f;List list(f);auto& nr=NativeOwner();
    const auto state_token=ReadCommandStateToken(list.native);
    const auto token=NativeRecord(list,nr);
    auto* gate=f.Fence();
    Hr(ID3D12CommandQueue_Wait(f.a,gate,1),"hold the exact production queue behind a CPU-signaled gate");
    list.Close();list.Submit(f.a);nr.retired=true;
    auto* queue=nr.last_submission_queue;const auto fence_value=nr.completion_fence_value;
    Check(queue && fence_value && !IsContextFenceCompleteLocked(nr),
          "real queue signal cannot complete before the held gate");
    NativeFinalRelease(list.native);
    Check(!FindCommandStateTokenLocked(state_token) && FindLifecycleCommandListLocked(token) &&
          nr.recording_command_lists==1 && nr.slots[0].recording_command_lists==1 && !native_owner_releases,
          "FINAL_NATIVE_RELEASE_MUST_ONLY_PUBLISH_DEATH");
    NativeCollect();
    Check(!FindLifecycleCommandListLocked(token) && !nr.recording_command_lists &&
          !nr.slots[0].recording_command_lists,"native death consumes only replayable recording references");
    Check(nr.sr_handle && nr.nr_handle && !IsContextFenceCompleteLocked(nr) &&
          !g_pending_destroy_contexts[0].sr_handle && !native_owner_releases &&
          !CanDestroyContextLocked(nr),"NATIVE_DEATH_PENDING_FENCE_MUST_RETAIN_OWNER");
    NativeCollect();
    Check(nr.sr_handle && !g_pending_destroy_contexts[0].sr_handle &&
          !g_lifecycle_tracking_uncertain.load(),"repeated collector preserves the unfinished exact fence");
    Hr(ID3D12Fence_Signal(gate,1),"release the exact queue gate without a timeout or sleep");
    Wait(queue->fence,fence_value);NativeCollect();
    Check(!nr.sr_handle && g_pending_destroy_contexts[0].nr_handle && !native_owner_releases,
          "original collector schedules only after the original queue fence completes");
    DrainPendingContextDestructions();
    Check(native_owner_releases==1 && !g_pending_destroy_contexts[0].sr_handle &&
          !g_lifecycle_tracking_uncertain.load(),"pending-fence native owner eventually retires exactly once");
    ReleaseCom(gate);
}

void NativeUnsubmittedDeath() {
    Fixture f;List list(f);auto& nr=NativeOwner();
    const auto state_token=ReadCommandStateToken(list.native);
    const auto token=NativeRecord(list,nr);nr.retired=true;
    NativeCollect();
    Check(!g_lifecycle_tracking_uncertain.load() && nr.recording_command_lists==1 &&
          !native_owner_releases,"live unsubmitted native recording remains owned");
    NativeFinalRelease(list.native);
    Check(!FindCommandStateTokenLocked(state_token) && FindLifecycleCommandListLocked(token) &&
          nr.recording_command_lists==1 && nr.slots[0].recording_command_lists==1 &&
          !g_lifecycle_tracking_uncertain.load() && !native_owner_releases,
          "FINAL_NATIVE_RELEASE_MUST_ONLY_PUBLISH_DEATH");
    NativeCollect();
    const auto* record=FindLifecycleCommandListLocked(token);
    Check(g_lifecycle_tracking_uncertain.load() && nr.lifecycle_dependencies_quarantined &&
          record && !record->detached && !record->submission_count &&
          nr.pending_recording_token==token && nr.recording_command_lists==1 &&
          nr.slots[0].recording_command_lists==1 && nr.nr_handle &&
          !g_pending_destroy_contexts[0].sr_handle && !native_owner_releases,
          "NATIVE_UNSUBMITTED_DEATH_MUST_RETAIN_QUARANTINED_CLOSURE");
#if defined(NR_HAS_RETIREMENT_EVIDENCE)
    Check(g_nr_retirement_published.tracking_uncertain && g_nr_retirement_published.contexts==1 &&
          g_nr_retirement_published.context[0].first_block==nr_retirement::TrackingUncertain &&
          !g_nr_retirement_published.context[0].post_evaluated &&
          (g_nr_retirement_published.context[0].unknown&nr_retirement::PostReady),
          "NATIVE_UNSUBMITTED_COLLECTOR_MUST_STOP_BEFORE_POST_READY");
#endif
    NativeCollect();DrainPendingContextDestructions();
    Check(FindLifecycleCommandListLocked(token) && nr.nr_handle &&
          nr.recording_command_lists==1 && !native_owner_releases,
          "uncertain collector re-entry and drain retain the complete opaque closure");
    std::printf("NATIVE_UNSUBMITTED_DEATH quarantine=1 ledgerRetained=1 providerRelease=0; post-Maintain entry count requires the CPU collector fixture\n");
}

void NativeNoticeUnavailable(NativeNoticeFailure failure) {
    Fixture f;List list(f,failure);auto& nr=NativeOwner();
    const auto state_token=ReadCommandStateToken(list.native);
    const auto* observed=FindCommandStateTokenLocked(state_token);
    Check(observed && observed->native_created && observed->lifetime_ready,
          "unsupported destruction receipt preserves native command-state observation lifetime");
    const bool admitted=RecordNrUseLocked(list.native,nr,nr.slots[0]);
    unsigned long long token=0;
    Check(!admitted && !ReadLifecycleToken(list.native,token) &&
          !g_lifecycle_active_command_lists.load() && !nr.pending_recording_token &&
          !nr.recording_command_lists && !nr.slots[0].recording_command_lists &&
          !g_lifecycle_tracking_uncertain.load() && !native_owner_releases,
          "NATIVE_UNSUPPORTED_NOTICE_MUST_REFUSE_NR_WITHOUT_HALF_PUBLICATION");
    Check(FindCommandStateTokenLocked(state_token) &&
          FindCommandStateTokenLocked(state_token)->lifetime_ready,
          "failed NR admission leaves the older command-state observation capability available");
    NativeFinalRelease(list.native);NativeCollect();
    Check(!FindCommandStateTokenLocked(state_token) && !g_lifecycle_active_command_lists.load() &&
          !nr.recording_command_lists && !native_owner_releases,
          "unsupported notifier marker and rejected NR admission retire independently");
}
} // namespace queue_test
