// Included from nr_external_provider_consumer.inl inside the pinned core TU.
// External-frame NR uses the same NrContext / NrSlot / BindNrParameters /
// g_nr_api Feature18 path as PrepareAfterSr + FinishAfterSr. Provider identity
// is a private sentinel handle - never an observed Feature1 record.

namespace nr_external_provider_core {

struct ProviderHandleSlot {
    std::uint64_t provider_id = 0;
    std::uint64_t view_id = 0;
    // Monotonic across retire/reuse; never reset to 1 on slot reuse.
    std::uint64_t generation = 0;
    // Address used only as an opaque NrContext key; never registered in g_features.
    alignas(void *) unsigned char opaque[sizeof(void *)]{};
};

inline ProviderHandleSlot g_provider_handles[8] = {};
inline SRWLOCK g_provider_handle_lock = SRWLOCK_INIT;

enum class EvaluateKind : std::uint32_t {
    Failed = 0,
    IdentityRetained = 1, // current-frame seed kept; no Feature18 transfer
    TransferRecorded = 2, // FinishAfterSr transferred NR result
};

struct EvaluateResult {
    EvaluateKind kind = EvaluateKind::Failed;
    NrExternalProviderReason reason = NrExternalProviderReason::RuntimeUnavailable;
};

inline ProviderHandleSlot *FindProviderSlot(std::uint64_t provider_id,
                                            std::uint64_t view_id) {
    ProviderHandleSlot *empty = nullptr;
    for (auto &slot : g_provider_handles) {
        if (slot.provider_id == provider_id && slot.view_id == view_id)
            return &slot;
        if (empty == nullptr && slot.provider_id == 0) empty = &slot;
    }
    return empty;
}

inline const NgxHandle *AcquireProviderHandle(std::uint64_t provider_id,
                                              std::uint64_t view_id,
                                              bool bump_generation,
                                              unsigned long long *generation_out) {
    if (!provider_id || !view_id || !generation_out) return nullptr;
    AcquireSRWLockExclusive(&g_provider_handle_lock);
    ProviderHandleSlot *slot = FindProviderSlot(provider_id, view_id);
    if (slot == nullptr) {
        ReleaseSRWLockExclusive(&g_provider_handle_lock);
        return nullptr;
    }
    if (slot->provider_id == 0) {
        slot->provider_id = provider_id;
        slot->view_id = view_id;
        if (slot->generation == 0) slot->generation = 1;
    }
    if (bump_generation) ++slot->generation;
    *generation_out = slot->generation;
    const NgxHandle *handle =
        reinterpret_cast<const NgxHandle *>(static_cast<void *>(slot->opaque));
    ReleaseSRWLockExclusive(&g_provider_handle_lock);
    return handle;
}

inline void RetireProviderContexts(std::uint64_t provider_id) {
    if (!provider_id) return;
    AcquireSRWLockExclusive(&g_provider_handle_lock);
    for (auto &slot : g_provider_handles) {
        if (slot.provider_id != provider_id) continue;
        const NgxHandle *handle =
            reinterpret_cast<const NgxHandle *>(static_cast<void *>(slot.opaque));
        ++slot.generation; // keep monotonic; do not zero
        const std::uint64_t kept_generation = slot.generation;
        ReleaseSRWLockExclusive(&g_provider_handle_lock);
        EnterCriticalSection(&g_chain_cs);
        RetireContextForHandle(handle);
        LeaveCriticalSection(&g_chain_cs);
        AcquireSRWLockExclusive(&g_provider_handle_lock);
        slot.provider_id = 0;
        slot.view_id = 0;
        slot.generation = kept_generation;
    }
    ReleaseSRWLockExclusive(&g_provider_handle_lock);
}

inline ColorContract ColorContractFromFrame(const NrExternalFrameV1 &frame) {
    ColorContract contract = {};
    contract.pre_exposure = SanitizeExposure(frame.pre_exposure);
    contract.exposure_scale = SanitizeExposure(frame.exposure_scale);
    contract.has_pre_exposure = frame.pre_exposure > 0.000001f &&
        std::isfinite(frame.pre_exposure);
    contract.has_exposure_scale = frame.exposure_scale > 0.000001f &&
        std::isfinite(frame.exposure_scale);
    contract.source_to_canonical = std::clamp(
        contract.exposure_scale / contract.pre_exposure,
        1.0f / 1024.0f, 1024.0f);
    if (frame.exposure.resource != 0) {
        contract.exposure_texture = nr_external_provider_d3d12::AsResource(
            frame.exposure.resource);
        if (contract.exposure_texture != nullptr) {
            const D3D12_RESOURCE_DESC desc =
                ID3D12Resource_GetDesc(contract.exposure_texture);
            contract.exposure_texture_format = desc.Format;
            contract.exposure_texture_width = static_cast<unsigned int>(
                std::min<UINT64>(desc.Width,
                                 std::numeric_limits<unsigned int>::max()));
            contract.exposure_texture_height = desc.Height;
        }
    }
    return contract;
}

inline int CreateFlagsFromFrame(const NrExternalFrameV1 &frame) {
    int flags = 0;
    if ((frame.lease_flags &
         static_cast<std::uint32_t>(NrExternalLeaseFlags::HdrColor)) != 0)
        flags |= 1;
    if ((frame.lease_flags &
         static_cast<std::uint32_t>(NrExternalLeaseFlags::DepthInverted)) != 0)
        flags |= 8;
    return flags;
}

inline NrWorkExtent ResolveExternalWorkExtent(unsigned int render_width,
                                              unsigned int render_height,
                                              unsigned int output_width,
                                              unsigned int output_height) {
    // Same #134 work-budget policy as ResolveNrWorkExtent, but with an explicit
    // output extent (Feeder same-res / no NGX params block).
    NrWorkExtent result = {};
    result.render_width = render_width;
    result.render_height = render_height;
    result.work_width = render_width;
    result.work_height = render_height;
    result.output_width = output_width;
    result.output_height = output_height;
    if (render_width == 0 || render_height == 0 || g_config.work_mode == 0)
        return result;
    if (output_width == 0 || output_height == 0) {
        result.output_width = render_width;
        result.output_height = render_height;
        return result;
    }
    float desired_scale = 1.0f;
    if (g_config.work_mode == 5) {
        desired_scale = g_config.custom_work_scale;
    } else {
        static const float output_scales[] = {
            1.0f, 0.58f, 0.50f, 0.42f, 1.0f / 3.0f};
        const float target_width = output_width *
            output_scales[g_config.work_mode];
        const float target_height = output_height *
            output_scales[g_config.work_mode];
        desired_scale = std::min(target_width / render_width,
                                 target_height / render_height);
    }
    desired_scale = std::clamp(desired_scale, 0.5f, 1.0f);
    unsigned int desired_width = static_cast<unsigned int>(
        std::floor(render_width * desired_scale + 0.5f));
    unsigned int desired_height = static_cast<unsigned int>(
        std::floor(render_height * desired_scale + 0.5f));
    SelectAlignedWorkExtent(desired_width, desired_height,
        render_width, render_height, result.work_width, result.work_height);
    const double work_area = static_cast<double>(result.work_width) *
        result.work_height;
    const double render_area = static_cast<double>(render_width) *
        render_height;
    result.linear_scale = static_cast<float>(std::sqrt(
        work_area / std::max(render_area, 1.0)));
    result.scaled = result.work_width != render_width ||
                    result.work_height != render_height;
    return result;
}

inline bool ResolveExternalGuideBinding(const NrExternalFrameV1 &frame,
                                        const FeatureRecord &feature,
                                        unsigned int width, unsigned int height,
                                        unsigned int guide_width,
                                        unsigned int guide_height,
                                        CodecColorMode color_mode,
                                        NrGuideBinding &binding) {
    binding = {};
    binding.depth = nr_external_provider_d3d12::AsResource(frame.depth.resource);
    binding.motion = nr_external_provider_d3d12::AsResource(frame.motion.resource);
    ID3D12Resource *color =
        nr_external_provider_d3d12::AsResource(frame.color.resource);
    if (binding.depth == nullptr || binding.motion == nullptr || color == nullptr)
        return false;

    NrGuideSignature &s = binding.signature;
    const D3D12_RESOURCE_DESC depth_desc = ID3D12Resource_GetDesc(binding.depth);
    const D3D12_RESOURCE_DESC motion_desc = ID3D12Resource_GetDesc(binding.motion);
    const D3D12_RESOURCE_DESC color_desc = ID3D12Resource_GetDesc(color);
    s.depth_physical_width = depth_desc.Width;
    s.depth_physical_height = depth_desc.Height;
    s.depth_format = static_cast<unsigned int>(depth_desc.Format);
    s.motion_physical_width = motion_desc.Width;
    s.motion_physical_height = motion_desc.Height;
    s.motion_format = static_cast<unsigned int>(motion_desc.Format);
    s.color_physical_width = color_desc.Width;
    s.color_physical_height = color_desc.Height;
    s.color_format = static_cast<unsigned int>(color_desc.Format);
    s.color_x = frame.color.rect_x;
    s.color_y = frame.color.rect_y;
    s.depth_x = frame.depth.rect_x;
    s.depth_y = frame.depth.rect_y;
    s.motion_x = frame.motion.rect_x;
    s.motion_y = frame.motion.rect_y;
    s.depth_width = guide_width;
    s.depth_height = guide_height;
    s.motion_source_width = guide_width;
    s.motion_source_height = guide_height;
    s.motion_bound_width = guide_width;
    s.motion_bound_height = guide_height;
    s.resample_motion = false;
    if (!TextureContainsRegion(binding.depth, s.depth_x, s.depth_y,
                               s.depth_width, s.depth_height) ||
        !TextureContainsRegion(binding.motion, s.motion_x, s.motion_y,
                               s.motion_source_width, s.motion_source_height))
        return false;

    s.motion_scale_x = frame.motion.motion_scale_x;
    s.motion_scale_y = frame.motion.motion_scale_y;
    binding.has_motion_scale_x = std::isfinite(s.motion_scale_x);
    binding.has_motion_scale_y = std::isfinite(s.motion_scale_y);
    binding.valid_motion_scale_x = binding.has_motion_scale_x &&
        std::fabs(s.motion_scale_x) > 0.000001f;
    binding.valid_motion_scale_y = binding.has_motion_scale_y &&
        std::fabs(s.motion_scale_y) > 0.000001f;
    if (!binding.valid_motion_scale_x) s.motion_scale_x = 1.0f;
    if (!binding.valid_motion_scale_y) s.motion_scale_y = 1.0f;
    binding.capture_params.game_mv_x =
        binding.has_motion_scale_x ? s.motion_scale_x : NAN;
    binding.capture_params.game_mv_y =
        binding.has_motion_scale_y ? s.motion_scale_y : NAN;
    binding.capture_params.game_mv_valid =
        binding.has_motion_scale_x && binding.has_motion_scale_y &&
        std::isfinite(binding.capture_params.game_mv_x) &&
        std::isfinite(binding.capture_params.game_mv_y);

    s.normalized_create_flags = feature.create_flags;
    if (color_mode == CodecColorMode::ModelEncodedValueDomain)
        s.normalized_create_flags &= ~1;
    s.normalized_create_flags &= ~2; // same-res MV for Feeder synthetic path
    s.depth_inverted = (feature.create_flags & 8) != 0 ? 1 : 0;
    if (g_config.depth_mode == 0) s.depth_inverted = 0;
    else if (g_config.depth_mode == 1) s.depth_inverted = 1;

    binding.game_reset =
        (frame.lease_flags &
         static_cast<std::uint32_t>(NrExternalLeaseFlags::ResetTemporal)) != 0
            ? 1 : 0;
    binding.capture_params.reset_known = true;
    binding.jitter = ResolveNrJitter(nullptr, g_config.jitter_mode,
                                     guide_width, guide_height, width, height);
    binding.jitter_offset_x = binding.jitter.x;
    binding.jitter_offset_y = binding.jitter.y;
    if (frame.color.jitter_x != 0.0f || frame.color.jitter_y != 0.0f) {
        binding.jitter.known = std::isfinite(frame.color.jitter_x) &&
            std::isfinite(frame.color.jitter_y);
        if (binding.jitter.known) {
            binding.jitter.x = frame.color.jitter_x;
            binding.jitter.y = frame.color.jitter_y;
            binding.jitter.valid_x = true;
            binding.jitter.valid_y = true;
            binding.jitter_offset_x = binding.jitter.x;
            binding.jitter_offset_y = binding.jitter.y;
        }
    }
    return true;
}

// Install the same ExecuteCommandLists detour used by Feature1 so starter
// Feeder EndCommands submits can train real queue affinity (no forged counts).
inline bool EnsureExternalExecuteHook(ID3D12CommandQueue *queue) {
    if (queue == nullptr) return false;
    const D3D12_COMMAND_QUEUE_DESC queue_desc =
        ID3D12CommandQueue_GetDesc(queue);
    if (queue_desc.Type != D3D12_COMMAND_LIST_TYPE_DIRECT) return false;
    void **vtable = *reinterpret_cast<void ***>(queue);
    void *target = vtable != nullptr ? vtable[10] : nullptr;
    if (target == nullptr) return false;

    AcquireSRWLockExclusive(&g_queue_execute_hook_lock);
    for (std::size_t i = 0; i < g_queue_execute_hook_count; ++i) {
        if (g_queue_execute_hook_targets[i] == target) {
            if (!g_queue_execute_hook_enabled[i]) {
                const ULONGLONG now = GetTickCount64();
                if (now >= g_queue_execute_hook_retry_after[i]) {
                    const MH_STATUS retried = MH_EnableHook(target);
                    g_queue_execute_hook_enabled[i] =
                        retried == MH_OK || retried == MH_ERROR_ENABLED;
                    g_queue_execute_hook_retry_after[i] =
                        g_queue_execute_hook_enabled[i] ? 0 : now + 1000;
                }
            }
            const bool ready = g_queue_execute_hook_enabled[i];
            ReleaseSRWLockExclusive(&g_queue_execute_hook_lock);
            return ready;
        }
    }
    if (g_queue_execute_hook_count >= std::size(g_queue_execute_hook_targets)) {
        ReleaseSRWLockExclusive(&g_queue_execute_hook_lock);
        return false;
    }
    const MH_STATUS initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) {
        ReleaseSRWLockExclusive(&g_queue_execute_hook_lock);
        return false;
    }
    const std::size_t index = g_queue_execute_hook_count;
    const MH_STATUS created = MH_CreateHook(
        target, g_queue_execute_detours[index],
        reinterpret_cast<void **>(&g_original_queue_execute[index]));
    if (created != MH_OK) {
        ReleaseSRWLockExclusive(&g_queue_execute_hook_lock);
        return false;
    }
    g_queue_execute_hook_targets[index] = target;
    ++g_queue_execute_hook_count;
    const MH_STATUS enabled = MH_EnableHook(target);
    g_queue_execute_hook_enabled[index] =
        enabled == MH_OK || enabled == MH_ERROR_ENABLED;
    g_queue_execute_hook_retry_after[index] =
        g_queue_execute_hook_enabled[index] ? 0 : GetTickCount64() + 1000;
    const bool ready = g_queue_execute_hook_enabled[index];
    ReleaseSRWLockExclusive(&g_queue_execute_hook_lock);
    return ready;
}

// Honest queue lease: track the producer queue, require fence when asked, and
// accept only affinity markers written by real ExecuteCommandLists. Never loops
// RememberLastSubmissionQueue to fabricate stable_submissions.
inline bool VerifyExternalQueueLease(ID3D12GraphicsCommandList *list,
                                     const NrExternalFrameV1 &frame,
                                     NrExternalProviderReason &reason) {
    if (list == nullptr || frame.queue == 0) {
        reason = NrExternalProviderReason::LeaseInvalid;
        return false;
    }
    auto *queue = reinterpret_cast<ID3D12CommandQueue *>(
        static_cast<std::uintptr_t>(frame.queue));
    if (!EnsureExternalExecuteHook(queue) ||
        !EnsureNativeCommandListResetHookRaw(list)) {
        reason = NrExternalProviderReason::RuntimeUnavailable;
        return false;
    }
    GraphicsQueueRecord *record = TrackGraphicsQueueLocked(queue);
    if (record == nullptr || record->identity == 0) {
        reason = NrExternalProviderReason::LeaseInvalid;
        return false;
    }
    if ((frame.lease_flags &
         static_cast<std::uint32_t>(NrExternalLeaseFlags::QueueFenceRequired)) != 0) {
        if (frame.fence == 0) {
            reason = NrExternalProviderReason::LeaseInvalid;
            return false;
        }
    }
    CommandListQueueMarker marker = {};
    if (!ReadLastSubmissionQueueMarker(list, marker) ||
        marker.identity != record->identity ||
        marker.stable_submissions < kRequiredStableQueueSubmissions ||
        (marker.flags & kQueueMarkerMigrated) != 0) {
        // Starter frames: Feeder must EndCommands (actual submit) without NR so
        // the Execute hook can train affinity. Do not forge counts here.
        reason = NrExternalProviderReason::LeaseInvalid;
        SetBypassReason(BypassReason::QueueAffinityUnverified);
        return false;
    }
    return true;
}

inline bool SeedOutputFromColor(ID3D12GraphicsCommandList *list,
                                const NrExternalFrameV1 &frame) {
    if (frame.color.resource == frame.output.resource) {
        nr_external_provider_d3d12::Transition(
            list, nr_external_provider_d3d12::AsResource(frame.output.resource),
            static_cast<D3D12_RESOURCE_STATES>(frame.output.state_before),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        return true;
    }
    NrExternalResourceV1 seeded = frame.output;
    seeded.state_before = frame.output.state_before;
    seeded.state_after = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    return nr_external_provider_d3d12::CopyRegion(list, frame.color, seeded);
}

inline void RestoreOutputState(ID3D12GraphicsCommandList *list,
                               const NrExternalFrameV1 &frame,
                               D3D12_RESOURCE_STATES current) {
    ID3D12Resource *output =
        nr_external_provider_d3d12::AsResource(frame.output.resource);
    if (list == nullptr || output == nullptr) return;
    const D3D12_RESOURCE_STATES want = static_cast<D3D12_RESOURCE_STATES>(
        frame.output.state_after);
    nr_external_provider_d3d12::Transition(list, output, current, want);
}

// The recovered producer supplies whole same-device 2D textures. Reject ABI
// shapes outside that verified slice before recording barriers or copies.
inline bool ValidateExternalTexture(const NrExternalResourceV1 &r) {
    if (!nr_external_provider::ResourcePresent(r) || r.subresource != 0 ||
        r.rect_x != 0 || r.rect_y != 0 || r.rect_w != r.width ||
        r.rect_h != r.height) return false;
    const auto desc = ID3D12Resource_GetDesc(
        nr_external_provider_d3d12::AsResource(r.resource));
    return desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
        desc.DepthOrArraySize == 1 && desc.MipLevels == 1 &&
        desc.SampleDesc.Count == 1 && desc.Width == r.width &&
        desc.Height == r.height && static_cast<unsigned int>(desc.Format) == r.format;
}

inline bool ValidateExternalTextures(const NrExternalFrameV1 &frame) {
    if (!ValidateExternalTexture(frame.color) ||
        !ValidateExternalTexture(frame.output) ||
        !ValidateExternalTexture(frame.depth) ||
        !ValidateExternalTexture(frame.motion)) return false;
    return frame.color.format == frame.output.format &&
        frame.depth.format == DXGI_FORMAT_R32_FLOAT &&
        frame.motion.format == DXGI_FORMAT_R16G16_FLOAT &&
        frame.motion.motion_unit == NrExternalMotionUnit::Pixels &&
        std::isfinite(frame.motion.motion_scale_x) &&
        std::isfinite(frame.motion.motion_scale_y) &&
        frame.exposure.resource == 0;
}

inline bool ValidateExternalDevice(ID3D12Device *device,
                                  const NrExternalFrameV1 &frame) {
    for (const auto *r : {&frame.color, &frame.output, &frame.depth, &frame.motion}) {
        ID3D12Device *owner = nullptr;
        const HRESULT hr = ID3D12Resource_GetDevice(
            nr_external_provider_d3d12::AsResource(r->resource), IID_ID3D12Device,
            reinterpret_cast<void **>(&owner));
        const bool same = SUCCEEDED(hr) && owner == device;
        ReleaseCom(owner);
        if (!same) return false;
    }
    if (frame.queue == 0 || frame.fence == 0) return false;
    ID3D12Device *owner = nullptr;
    auto *queue = reinterpret_cast<ID3D12CommandQueue *>(frame.queue);
    HRESULT hr = ID3D12CommandQueue_GetDevice(queue, IID_ID3D12Device,
                                            reinterpret_cast<void **>(&owner));
    bool same = SUCCEEDED(hr) && owner == device;
    ReleaseCom(owner);
    if (!same) return false;
    auto *fence = reinterpret_cast<ID3D12Fence *>(frame.fence);
    hr = ID3D12Fence_GetDevice(fence, IID_ID3D12Device,
                              reinterpret_cast<void **>(&owner));
    same = SUCCEEDED(hr) && owner == device;
    ReleaseCom(owner);
    return same;
}

inline EvaluateResult EvaluateExternalFrame(const NrExternalFrameV1 &frame,
                                            bool history_reset) {
    EvaluateResult result;
    // External path must not require native Feature1 evaluate hooks
    // (g_evaluate_hooks_ready / nvngx_dlss). Feature18 is loaded via EnsureNrApi.
    if (g_lifecycle_tracking_uncertain.load(std::memory_order_acquire) ||
        g_nr_failed_create_process_latched.load(std::memory_order_acquire) ||
        OtherNrOwnerActive()) {
        result.reason = NrExternalProviderReason::CompetingNr;
        return result;
    }

    auto *list = nr_external_provider_d3d12::AsList(frame.command_list);
    ID3D12Resource *color =
        nr_external_provider_d3d12::AsResource(frame.color.resource);
    ID3D12Resource *output =
        nr_external_provider_d3d12::AsResource(frame.output.resource);
    if (list == nullptr || color == nullptr || output == nullptr) {
        result.reason = NrExternalProviderReason::MissingRequired;
        return result;
    }
    if (frame.depth.provenance == NrGuideProvenance::Unknown ||
        frame.motion.provenance == NrGuideProvenance::Unknown ||
        frame.depth.resource == 0 || frame.motion.resource == 0) {
        result.reason = NrExternalProviderReason::MissingRequired;
        return result;
    }
    if (!ValidateExternalTextures(frame)) {
        result.reason = NrExternalProviderReason::UnsupportedContract;
        return result;
    }

    const unsigned int render_width = frame.color.rect_w;
    const unsigned int render_height = frame.color.rect_h;
    if (render_width == 0 || render_height == 0 ||
        frame.output.rect_w != render_width ||
        frame.output.rect_h != render_height) {
        result.reason = NrExternalProviderReason::UnsupportedContract;
        return result;
    }

    const D3D12_RESOURCE_DESC output_desc = ID3D12Resource_GetDesc(output);
    if ((output_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0) {
        result.reason = NrExternalProviderReason::UnsupportedContract;
        return result;
    }

    FeatureRecord feature = {};
    feature.feature_id = 0; // never Feature1
    feature.width = render_width;
    feature.height = render_height;
    feature.out_width = frame.output.rect_w;
    feature.out_height = frame.output.rect_h;
    feature.create_flags = CreateFlagsFromFrame(frame);

    unsigned long long generation = 0;
    const NgxHandle *handle = AcquireProviderHandle(
        frame.provider_id, frame.view_id, history_reset, &generation);
    if (handle == nullptr) {
        result.reason = NrExternalProviderReason::Busy;
        return result;
    }
    feature.handle = handle;
    feature.generation = generation;

    EnterCriticalSection(&g_chain_cs);
    // Recheck mutable controls under the same lock used by native NR admission.
    if (!g_config.enabled || ActiveTransferStrength(g_config) <= 0.000001f ||
        OtherNrOwnerActive() || ShouldGuardFeature1ForRayReconstruction()) {
        LeaveCriticalSection(&g_chain_cs);
        result.reason = (!g_config.enabled || ActiveTransferStrength(g_config) <= 0.000001f)
            ? NrExternalProviderReason::Disabled : NrExternalProviderReason::CompetingNr;
        return result;
    }
    ID3D12Device *device = nullptr;
    const HRESULT hr = ID3D12GraphicsCommandList_GetDevice(
        list, IID_ID3D12Device, reinterpret_cast<void **>(&device));
    if (FAILED(hr) || device == nullptr) {
        LeaveCriticalSection(&g_chain_cs);
        result.reason = NrExternalProviderReason::RuntimeUnavailable;
        return result;
    }
    if (!ValidateExternalDevice(device, frame)) {
        ReleaseCom(device);
        LeaveCriticalSection(&g_chain_cs);
        result.reason = NrExternalProviderReason::LeaseInvalid;
        return result;
    }

    const int create_flags = feature.create_flags;
    const PostSrStorageContract storage_contract =
        ClassifyPostSrStorage(output_desc.Format, create_flags);
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support = {};
    HRESULT support_hr = E_INVALIDARG;
    if (!SupportsPostSrStorage(device, output_desc, storage_contract, &support,
                               &support_hr) ||
        !EnsureNrApi(device) || !EnsureCodec(device)) {
        ReleaseCom(device);
        LeaveCriticalSection(&g_chain_cs);
        result.reason = NrExternalProviderReason::RuntimeUnavailable;
        return result;
    }

    NrExternalProviderReason lease_reason =
        NrExternalProviderReason::LeaseInvalid;
    if (!VerifyExternalQueueLease(list, frame, lease_reason)) {
        ReleaseCom(device);
        LeaveCriticalSection(&g_chain_cs);
        result.reason = lease_reason;
        return result;
    }

    const NrWorkExtent work = ResolveExternalWorkExtent(
        render_width, render_height, feature.out_width, feature.out_height);
    if (work.work_width == 0 || work.work_height == 0) {
        ReleaseCom(device);
        LeaveCriticalSection(&g_chain_cs);
        result.reason = NrExternalProviderReason::UnsupportedContract;
        return result;
    }
    const unsigned int width = work.work_width;
    const unsigned int height = work.work_height;

    const CodecColorMode color_mode = PostSrCodecColorMode(storage_contract);
    const ColorContract color_contract = ColorContractFromFrame(frame);
    NrGuideBinding guides = {};
    if (!ResolveExternalGuideBinding(frame, feature, width, height,
                                     render_width, render_height, color_mode,
                                     guides)) {
        ReleaseCom(device);
        LeaveCriticalSection(&g_chain_cs);
        result.reason = NrExternalProviderReason::UnsupportedContract;
        return result;
    }

    // Seed only after validation so declines do not leave mutated barriers.
    if (!SeedOutputFromColor(list, frame)) {
        ReleaseCom(device);
        LeaveCriticalSection(&g_chain_cs);
        result.reason = NrExternalProviderReason::TransportFailed;
        return result;
    }
    bool seeded = true;

    NrContext *context = FindOrCreateContext(
        handle, feature.generation, 1, width, height, color_mode,
        guides.signature,
        UsePeakShoulder(color_mode, (create_flags & 1) != 0,
                        g_config.experimental_peak_shoulder));
    if (context == nullptr || !CanBeginContextUseLocked(list, *context)) {
        if (seeded)
            RestoreOutputState(list, frame,
                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ReleaseCom(device);
        LeaveCriticalSection(&g_chain_cs);
        result.reason = NrExternalProviderReason::LeaseInvalid;
        return result;
    }
    ++context->frames_seen;

    ID3D12Resource *exposure_source =
        (create_flags & 1) != 0 ? color_contract.exposure_texture : nullptr;
    NrSlot *slot = FindOrCreateSlot(*context, device, output, true,
                                    exposure_source, CodecColorMode::Automatic,
                                    create_flags);
    if (slot == nullptr || context->failed || context->retired) {
        RestoreOutputState(list, frame, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ReleaseCom(device);
        LeaveCriticalSection(&g_chain_cs);
        result.reason = NrExternalProviderReason::RuntimeUnavailable;
        return result;
    }
    ArmDeltaStatsCaptureLocked(*slot, *context, color_contract);
    if (!RecordNrUseLocked(list, *context, *slot)) {
        RestoreOutputState(list, frame, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ReleaseCom(device);
        LeaveCriticalSection(&g_chain_cs);
        result.reason = NrExternalProviderReason::LeaseInvalid;
        return result;
    }
    ReleaseCom(device);

    BeginPipelineStatsScopeLocked(feature.generation, 1);

    PostSrSubstitution substitution = {};
    substitution.active = true;
    substitution.original_output = output;
    substitution.context = context;
    substitution.slot = slot;
    substitution.output_base_x = frame.output.rect_x;
    substitution.output_base_y = frame.output.rect_y;
    substitution.width = width;
    substitution.height = height;
    substitution.guide_width = render_width;
    substitution.guide_height = render_height;
    substitution.color_contract = color_contract;
    substitution.guides = guides;
    substitution.native_output_snapshot = slot->snapshots_native_output;
    slot->result_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    const bool transferred = FinishAfterSr(list, nullptr, feature, substitution);
    if (transferred) {
        RestoreOutputState(list, frame, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        LeaveCriticalSection(&g_chain_cs);
        result.kind = EvaluateKind::TransferRecorded;
        result.reason = NrExternalProviderReason::Ready;
        return result;
    }

    // FinishAfterSr false: warmup/identity retention or evaluate failure.
    // Seed is this frame's color — honest identity, never reported as NR success.
    RestoreOutputState(list, frame, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    LeaveCriticalSection(&g_chain_cs);
    result.kind = EvaluateKind::IdentityRetained;
    result.reason = NrExternalProviderReason::IdentityPassthrough;
    return result;
}

} // namespace nr_external_provider_core
