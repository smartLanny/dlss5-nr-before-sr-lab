// Included after the slot/context declarations and exact-fence predicate.
// All slot operations run under g_chain_cs. Only the export job owns objects
// after detachment; the worker does not access slot/context state.
std::atomic<unsigned int> g_stage_capture_status{0};
std::atomic<int> g_stage_capture_last_result{0};
std::atomic<unsigned long long> g_stage_capture_request_id{0};
std::atomic<bool> g_stage_capture_rebuild_pending{false};
std::atomic<unsigned int> g_stage_capture_retry_count{0};
constexpr unsigned int kStageCaptureMinimumEvaluations = 64;
constexpr unsigned int kStageCaptureMaximumRetries = 1;

struct StageCapturePreparedContext {
    unsigned long long request_id = 0;
    const NgxHandle *sr_handle = nullptr;
    unsigned long long feature_generation = 0;
    int mode = 0;
};
// Only accessed under g_chain_cs; the UI publishes a new request atomically
// and never edits this binding directly.
StageCapturePreparedContext g_stage_capture_prepared_context = {};

unsigned int StageCaptureStatus() {
    return g_stage_capture_status.load(std::memory_order_acquire);
}

bool RequestStageCaptureInternal(bool owned) {
    EnterCriticalSection(&g_chain_cs);
    // The internal acceptance owner keeps its lease throughout performance and
    // deep-capture phases. Never drop that global lease to admit a request.
    if(nr_f8::g_config_hold.load()!=owned || nr_f8::IsActive()) {
        LeaveCriticalSection(&g_chain_cs);return false;
    }
    unsigned int expected = 0;
    // Reserve the request before publishing its identity. State 4 is a brief
    // request publication/cancellation state, never eligible for render-thread arm.
    if (!g_stage_capture_status.compare_exchange_strong(
            expected, 4, std::memory_order_acq_rel)) {LeaveCriticalSection(&g_chain_cs);return false;}
    g_stage_capture_request_id.fetch_add(1, std::memory_order_relaxed);
    g_stage_capture_last_result.store(0, std::memory_order_relaxed);
    g_stage_capture_retry_count.store(0, std::memory_order_relaxed);
    g_stage_capture_rebuild_pending.store(true, std::memory_order_relaxed);
    g_stage_capture_status.store(1, std::memory_order_release);
    LeaveCriticalSection(&g_chain_cs);
    Log("Stage capture requested: next eligible warmed-up NR composition; "
        "one outstanding request, sampled raw stages; optional native output sidecar. "
        "NR is recreated before capture, waits at least 64 successful "
        "evaluations since Reset, then retires to drain exact-fence resources. "
        "This settling interval does not prove temporal convergence.");
    return true;
}

bool RequestStageCapture() { return RequestStageCaptureInternal(false); }
// Internal configuration-lease owner only; status 2/3 still must drain normally.
bool RequestStageCaptureOwned() { return RequestStageCaptureInternal(true); }

bool CancelPendingStageCapture(unsigned long long request) {
    if (request == 0) return false;
    bool cancelled = false;
    // Prepare and Arm own this lock. Status 2/3 already owns resources or an
    // export job and must retain its exact-fence lifetime, even after timeout.
    EnterCriticalSection(&g_chain_cs);
    unsigned int expected = 1;
    if (g_stage_capture_status.compare_exchange_strong(
            expected, 4, std::memory_order_acq_rel)) {
        // Check the reserved identity so a late cancellation can never discard
        // a newer request. Request publication shares the configuration lock.
        if (g_stage_capture_request_id.load(std::memory_order_acquire) == request) {
            g_stage_capture_prepared_context = {};
            g_stage_capture_rebuild_pending.store(false, std::memory_order_relaxed);
            g_stage_capture_retry_count.store(0, std::memory_order_relaxed);
            g_stage_capture_last_result.store(-1, std::memory_order_relaxed);
            g_stage_capture_status.store(0, std::memory_order_release);
            cancelled = true;
        } else {
            g_stage_capture_status.store(1, std::memory_order_release);
        }
    }
    LeaveCriticalSection(&g_chain_cs);
    if (cancelled)
        Log("Stage capture cancelled before arming: request=%llu; no capture GPU "
            "resources were released and a new request may be accepted.", request);
    return cancelled;
}

void PrepareStageCaptureContextLocked(const NgxHandle *sr_handle,
                                       unsigned long long feature_generation,
                                       int mode) {
    if (StageCaptureStatus() != 1) return;
    if (!g_stage_capture_rebuild_pending.exchange(false, std::memory_order_acq_rel)) {
        const auto &prepared = g_stage_capture_prepared_context;
        if (prepared.request_id ==
                g_stage_capture_request_id.load(std::memory_order_relaxed) &&
            (prepared.sr_handle != sr_handle ||
             prepared.feature_generation != feature_generation || prepared.mode != mode)) {
            // Preparation may precede a failed queue/slot admission. If the
            // selected contract then disappears, no slot owns any capture GPU
            // work yet (status1), so explicitly unlock instead of waiting forever.
            Log("Stage capture cancelled before arming: selected carrier/generation/mode "
                "changed. Request unlocked; press F7 again after the scene settles.");
            g_stage_capture_last_result.store(-1, std::memory_order_relaxed);
            g_stage_capture_status.store(0, std::memory_order_release);
        }
        return;
    }
    g_stage_capture_prepared_context = {
        g_stage_capture_request_id.load(std::memory_order_relaxed),
        sr_handle, feature_generation, mode};
    // A continuously reused Output may never have an idle descriptor heap:
    // same-queue NR overlap is permitted while prior frames still own it.
    // Retire the chosen history ONCE and arm its replacement's pristine slot.
    for (auto &context : g_contexts) {
        if (context.sr_handle == sr_handle && !context.retired &&
            context.feature_generation == feature_generation && context.mode == mode) {
            context.retired = true;
            Log("Stage capture preparing fresh NR instance: request=%llu "
                "previousInstance=%llu mode=%d; old resources keep their exact fences.",
                g_stage_capture_request_id.load(std::memory_order_relaxed),
                context.instance_id, mode);
        }
    }
}

unsigned long long PreparedStageCaptureRequestId(
    const NgxHandle *sr_handle, unsigned long long feature_generation, int mode) {
    const auto &prepared = g_stage_capture_prepared_context;
    const unsigned long long request =
        g_stage_capture_request_id.load(std::memory_order_relaxed);
    if (StageCaptureStatus() != 1 ||
        g_stage_capture_rebuild_pending.load(std::memory_order_acquire) ||
        prepared.request_id != request || prepared.sr_handle != sr_handle ||
        prepared.feature_generation != feature_generation || prepared.mode != mode)
        return 0;
    return request;
}

void RetireStaleStageCaptureLocked(NrContext &context, const NrSlot &current) {
    const unsigned long long request =
        g_stage_capture_request_id.load(std::memory_order_relaxed);
    if (StageCaptureStatus() != 2 || context.stage_capture_request_id != request)
        return;
    for (auto &owner : context.slots) {
        const NrStageCapture &capture = owner.stage_capture;
        if (&owner == &current || !capture.armed || capture.pending ||
            capture.info.request_id != request) continue;
        const unsigned int last_seen = capture.info.identity.evaluation;
        if (context.evaluations < last_seen ||
            context.evaluations - last_seen < kStageCaptureMinimumEvaluations)
            continue;
        // An ordinary double/triple buffer revisits its armed slot quickly.
        // If a one-off Output/Exposure is gone for 64 successful evaluations,
        // retire its history and let the exact-fence destructor retry once.
        // No descriptor/resource is mutated while it may still be in flight.
        context.retired = true;
        Log("Stage capture target stopped recurring: request=%llu lastSeenEval=%u "
            "currentEval=%u; retiring owner to drain, automatic retry budget=%u.",
            request, last_seen, context.evaluations, kStageCaptureMaximumRetries);
        return;
    }
}

void RetireStageCaptureOwnerLocked(NrSlot &slot) {
    for (auto &context : g_contexts) {
        for (auto &candidate : context.slots) {
            if (&candidate != &slot) continue;
            // Do not append future frames to this capture's slot: otherwise
            // its latest fence/recording references can advance indefinitely.
            context.retired = true;
            Log("Stage capture recorded: request=%llu instance=%llu frame=%llu "
                "sampled=%ux%u stride=%u; owner retired, awaiting exact fence and recording release.",
                slot.stage_capture.info.request_id,
                slot.stage_capture.info.identity.instance_id,
                slot.stage_capture.info.identity.frame_id,
                slot.stage_capture.info.width, slot.stage_capture.info.height,
                slot.stage_capture.info.stride);
            return;
        }
    }
}

bool CanRecordStageCapture(const NrStageCapture &capture) {
    return capture.armed && !capture.pending && capture.buffer != nullptr &&
           capture.readback != nullptr && capture.info.identity.warmed_up &&
           capture.info.identity.since_reset >= kStageCaptureMinimumEvaluations;
}

bool ComputeStageCaptureGrid(unsigned int source_width,
                              unsigned int source_height,
                              unsigned int &stride, unsigned int &width,
                              unsigned int &height, UINT64 &bytes) {
    stride = width = height = 0;
    bytes = 0;
    if (source_width == 0 || source_height == 0 ||
        source_width > 16384 || source_height > 16384) return false;
    stride = 1;
    for (;;) {
        width = (source_width + stride - 1) / stride;
        height = (source_height + stride - 1) / stride;
        bytes = static_cast<UINT64>(width) * height * 4u * 16u + 16u;
        // Count the GPU UAV and readback allocations together. The worker
        // streams rows from the mapped readback, without another image copy.
        if (width <= 1024 && height <= 1024 &&
            bytes * 2u <= 64u * 1024u * 1024u) return true;
        stride *= 2;
    }
}

void BindStageCaptureDescriptor(NrSlot &slot, ID3D12Resource *buffer,
                                 UINT64 bytes) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC view = {};
    view.Format = DXGI_FORMAT_R32_TYPELESS;
    view.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    view.Buffer.NumElements = buffer != nullptr
        ? static_cast<UINT>(bytes / sizeof(std::uint32_t)) : 4u;
    view.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu =
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(slot.heap);
    cpu.ptr += static_cast<SIZE_T>(g_codec.descriptor_size) *
               kCodecCaptureUavIndex;
    ID3D12Device_CreateUnorderedAccessView(
        g_codec.device, buffer, nullptr, &view, cpu);
}

void DestroyStageCapture(NrSlot &slot) {
    const bool owned_request = slot.stage_capture.armed ||
                              slot.stage_capture.pending;
    const unsigned long long request = slot.stage_capture.info.request_id;
    ReleaseCom(slot.stage_capture.readback);
    ReleaseCom(slot.stage_capture.buffer);
#ifdef NR_HAS_NATIVE_OUTPUT_CAPTURE
    ReleaseNativeOutputCapture(slot.stage_capture.native_output);
#endif
    slot.stage_capture = {};
    if (owned_request && request != 0 &&
        request == g_stage_capture_request_id.load(std::memory_order_relaxed) &&
        StageCaptureStatus() == 2) {
        // A failed create/evaluate or discarded list must not silently consume
        // the user's one-shot request. The enclosing slot retirement already
        // proved safety; let a later successful instance fulfil the request.
        const unsigned int retries =
            g_stage_capture_retry_count.fetch_add(1, std::memory_order_relaxed);
        if (retries < kStageCaptureMaximumRetries) {
            g_stage_capture_rebuild_pending.store(true, std::memory_order_relaxed);
            g_stage_capture_status.store(1, std::memory_order_release);
            Log("Stage capture retrying request=%llu after exact-fence retirement "
                "(retry=%u/%u).", request, retries + 1, kStageCaptureMaximumRetries);
        } else {
            Log("Stage capture failed: request=%llu could not retain a recurring "
                "target within the bounded retry budget. Request unlocked; retry manually "
                "after the scene/resources settle.", request);
            g_stage_capture_last_result.store(-1, std::memory_order_relaxed);
            g_stage_capture_status.store(0, std::memory_order_release);
        }
    }
}

void ArmStageCaptureLocked(NrContext &context, NrSlot &slot) {
    RetireStaleStageCaptureLocked(context, slot);
    if (StageCaptureStatus() != 1 || slot.stage_capture.buffer != nullptr ||
        context.retired ||
        g_stage_capture_rebuild_pending.load(std::memory_order_acquire) ||
        context.stage_capture_request_id == 0 ||
        context.stage_capture_request_id !=
            g_stage_capture_request_id.load(std::memory_order_relaxed) ||
        !CanMutateSlotLocked(slot)) return;
    NrStageCapture capture = {};
    const unsigned int capture_source_width = slot.active_render_width != 0
        ? slot.active_render_width : context.width;
    const unsigned int capture_source_height = slot.active_render_height != 0
        ? slot.active_render_height : context.height;
    if (!ComputeStageCaptureGrid(capture_source_width, capture_source_height,
            capture.info.stride, capture.info.width, capture.info.height,
            capture.bytes)) {
        Log("Stage capture rejected unsupported source extent %ux%u.",
            capture_source_width, capture_source_height);
        g_stage_capture_last_result.store(-1, std::memory_order_relaxed);
        g_stage_capture_status.store(0, std::memory_order_release);
        return;
    }
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = capture.bytes;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    HRESULT result = ID3D12Device_CreateCommittedResource(
        g_codec.device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_ID3D12Resource,
        reinterpret_cast<void **>(&capture.buffer));
    if (SUCCEEDED(result) && capture.buffer != nullptr) {
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        result = ID3D12Device_CreateCommittedResource(
            g_codec.device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_ID3D12Resource,
            reinterpret_cast<void **>(&capture.readback));
    }
    if (FAILED(result) || capture.buffer == nullptr || capture.readback == nullptr) {
        ReleaseCom(capture.readback);
        ReleaseCom(capture.buffer);
        Log("Stage capture allocation failed: 0x%08X; rendering continues.",
            static_cast<unsigned int>(result));
        g_stage_capture_last_result.store(-1, std::memory_order_relaxed);
        g_stage_capture_status.store(0, std::memory_order_release);
        return;
    }
    capture.info.request_id = g_stage_capture_request_id.load(std::memory_order_relaxed);
    capture.info.feature_generation = context.feature_generation;
    capture.info.source_width = capture_source_width;
    capture.info.source_height = capture_source_height;
    capture.info.model_width = context.width;
    capture.info.model_height = context.height;
    capture.info.source_format = static_cast<unsigned int>(slot.source_format);
    capture.info.result_format = static_cast<unsigned int>(slot.result_format);
    capture.info.mode = context.mode;
    capture.armed = true;
    slot.stage_capture = capture;
    BindStageCaptureDescriptor(slot, capture.buffer, capture.bytes);
    g_stage_capture_status.store(2, std::memory_order_release);
    Log("Stage capture armed: request=%llu mode=%d sampled=%ux%u stride=%u; "
        "captureRecreatedContext=1 waiting for at least 64 successful NR evaluations since Reset.", capture.info.request_id,
        context.mode, capture.info.width, capture.info.height, capture.info.stride);
}

struct StageCaptureExport {
#ifdef NR_HAS_NATIVE_OUTPUT_CAPTURE
    NrNativeOutputCapture native_output{};
#endif
    ID3D12Resource *readback = nullptr;
    UINT64 bytes = 0;
    NrStageCaptureInfo info = {};
};

bool WriteStageCaptureBytes(HANDLE file, const void *data, DWORD bytes) {
    DWORD written = 0;
    return WriteFile(file, data, bytes, &written, nullptr) != FALSE &&
           written == bytes;
}

bool WriteStageCapturePfm(const wchar_t *directory, const wchar_t *name,
                          const float *pixels, unsigned int width,
                          unsigned int height) {
    if (pixels == nullptr || width == 0 || height == 0 ||
        width > 1024 || height > 1024) return false;
    wchar_t path[MAX_PATH] = {};
    if (swprintf_s(path, L"%ls\\%ls.pfm", directory, name) < 0) return false;
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    char header[64] = {};
    const int header_length = std::snprintf(
        header, sizeof(header), "PF\n%u %u\n-1.0\n", width, height);
    bool ok = header_length > 0 &&
        WriteStageCaptureBytes(file, header, static_cast<DWORD>(header_length));
    float row[1024 * 3] = {};
    for (unsigned int y = height; ok && y != 0; --y) {
        const float *source = pixels + static_cast<std::size_t>(y - 1) * width * 4;
        for (unsigned int x = 0; x < width; ++x) {
            row[x * 3 + 0] = source[x * 4 + 0];
            row[x * 3 + 1] = source[x * 4 + 1];
            row[x * 3 + 2] = source[x * 4 + 2];
        }
        ok = WriteStageCaptureBytes(file, row, width * 3u * sizeof(float));
    }
    CloseHandle(file);
    return ok;
}

bool WriteStageCaptureMetadata(const wchar_t *directory,
                               const NrStageCaptureInfo &info, bool images_ok) {
    wchar_t path[MAX_PATH] = {};
    if (swprintf_s(path, L"%ls\\metadata.txt", directory) < 0) return false;
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    char text[4096] = {};
    const NrDiagnosticIdentity &id = info.identity;
    const int length = std::snprintf(text, sizeof(text),
        "format=NRStageCapture-v2\nbuild=%s\ncomplete=%d\n"
        "request=%llu\ninstance=%llu\nfeatureGeneration=%llu\nframe=%llu\n"
        "configGeneration=%u\nevaluation=%u\nsinceReset=%u\nwarmedUp=%d\nmode=%d\n"
        "sourceExtent=%ux%u\nmodelExtent=%ux%u\nsampledExtent=%ux%u\nstride=%u\n"
        "sourceBase=%u,%u\ndestinationBase=%u,%u\nsourceFormat=%u\nresultFormat=%u\n"
        "codecFlags=%u\nintensity=%.9g\nstyle=%u\nlocalTone=%.9g\n"
        "localStructure=%.9g\nskinStructure=%.9g\nautoMask=%d\nuiCorrection=%d\n"
        "proxyScale=%.9g\nproxyMapping=%s\ntransfer=%.9g\ncolorFollowEffective=%.9g\nmaxLumaRatio=%.9g\n"
        "workMode=%d\nworkLinearScale=%.9g\nlightingLock=%.9g\nedgeGuard=%.9g\n"
        "proxyCurve=%u\nframeConfidence=%.9g\n"
        "lightStyleSupported=%d\nlightStyleRecorded=%d\nlightControlModeRecorded=%d\nlightBroad=%.9g\nlightDark=%.9g\nlightReflection=%.9g\nlightStructure=%.9g\nlightGlow=%.9g\nlightPreview=%u\n"
        "preExposure=%.9g\nhasPreExposure=%d\nscalarExposureScale=%.9g\nhasExposureScale=%d\n"
        "sampling=nearest grid at active-region coordinates (x*stride,y*stride)\n"
        "pixels=PFM little-endian float RGB, raw shader values; alpha omitted\n"
        "rawRGBA=stages.rgba32f; little-endian float32; four planar RGBA images original/proxy/neural/composited; top-down; sampledExtent\n"
        "original=storage-domain shader load; proxy/neural=model encoded domain\n"
        "composited=shader result BEFORE destination format quantization\n"
        "nativeSrOutput=%s\n"
        "displayScreenshot=not captured; separately triggered ReShade screenshots are NOT same-frame evidence\n"
        "exposureTexture=when codecFlags bit64 is set, gpuRawExposure is the external texture load; otherwise derived scalar\n"
        "gpuRawExposure=%.9g\ngpuResolvedExposure=%.9g\ngpuSourceToCanonical=%.9g\ngpuPreExposure=%.9g\n"
        "gpuExposureSampling=CombineCS pixel(0,0), same dispatch as stages; not an EncodeCS sample\n"
        "comparison=decode with the recorded codec contract; raw stages do not share a photometric domain\n"
        "captureRecreatedContext=1\nevaluationsSinceReset=%u\nminimumCaptureEvaluations=64\n"
        "temporalHistory=fresh NR context; 64 successful evaluations is a settling interval, NOT verified convergence\n"
        "afterCapture=NR context retires for exact-fence export; following frames recreate NR and warm up again\n",
        kBuildIdentity, images_ok ? 1 : 0, info.request_id, id.instance_id,
        info.feature_generation, id.frame_id, id.config_generation, id.evaluation,
        id.since_reset, id.warmed_up ? 1 : 0, info.mode,
        info.source_width, info.source_height, info.model_width, info.model_height,
        info.width, info.height, info.stride,
        info.source_base_x, info.source_base_y, info.destination_base_x,
        info.destination_base_y, info.source_format, info.result_format, info.codec_flags,
        static_cast<double>(id.tuning.intensity), id.tuning.style,
        static_cast<double>(id.tuning.local_tone), static_cast<double>(id.tuning.local_structure),
        static_cast<double>(id.tuning.skin_structure), id.tuning.auto_mask ? 1 : 0,
        id.tuning.ui_correction ? 1 : 0, static_cast<double>(info.proxy_scale),
        (info.codec_flags & 256u) != 0u ? "rational-peak" : "existing",
        static_cast<double>(info.transfer_strength), static_cast<double>(info.color_strength),
        static_cast<double>(info.max_luma_ratio), info.work_mode,
        static_cast<double>(info.work_linear_scale),
        static_cast<double>(info.lighting_lock),
        static_cast<double>(info.edge_guard), info.proxy_curve,
        static_cast<double>(info.frame_confidence),
        info.light_supported?1:0,(info.codec_flags&262144u)!=0u,
        info.light_supported && (info.codec_flags&262144u)!=0u ? static_cast<int>(info.light_control_mode) : -1,
        static_cast<double>(info.light_broad),static_cast<double>(info.light_dark),
        static_cast<double>(info.light_reflection),static_cast<double>(info.light_structure),
        static_cast<double>(info.light_glow),info.light_preview,
        static_cast<double>(info.pre_exposure),
        info.has_pre_exposure ? 1 : 0, static_cast<double>(info.exposure_scale),
        info.has_exposure_scale ? 1 : 0,
        info.mode == 1 ? "represented by original stage (post-SR source/snapshot)"
#ifdef NR_HAS_NATIVE_OUTPUT_CAPTURE
                       : "native-output.json declares same-call presence or explicit refusal; native storage domain is unverified",
#else
                       : "unavailable; arrival state not proven, so no extra output read/transition",
#endif
        static_cast<double>(info.gpu_raw_exposure), static_cast<double>(info.gpu_resolved_exposure),
        static_cast<double>(info.gpu_source_to_canonical), static_cast<double>(info.gpu_pre_exposure),
        id.since_reset);
    bool ok = length > 0 && static_cast<std::size_t>(length) < sizeof(text) &&
        WriteStageCaptureBytes(file, text, static_cast<DWORD>(length));
#if defined(NR_HAS_NETWORK_MODEL_SELECTION)
    if (ok) {
        const int network_length = std::snprintf(text, sizeof(text),
            "networkRequest=%d\nruntimeGeneration=%llu\nruntimeModule=%llu\nnetworkExecutionVerified=0\n"
            "networkContract=request sent as UInt DLSSNR.Hint.Render.Preset at this instance's Create; "
            "successful Create/Evaluate are not an internal network readback\n",
            id.tuning.network_preset, id.tuning.runtime_generation, id.tuning.runtime_module_address);
        ok = network_length > 0 && static_cast<std::size_t>(network_length) < sizeof(text) &&
            WriteStageCaptureBytes(file, text, static_cast<DWORD>(network_length));
    }
#endif
    CloseHandle(file);
    return ok;
}

#ifdef NR_HAS_NATIVE_OUTPUT_CAPTURE
#include "nr_native_output_capture_export.inl"
#endif

DWORD WINAPI ExportStageCapture(void *parameter) {
    auto *job = static_cast<StageCaptureExport *>(parameter);
    wchar_t addon_directory[MAX_PATH] = {};
    wchar_t parent[MAX_PATH] = {};
    wchar_t directory[MAX_PATH] = {};
    AddonDirectory(addon_directory);
    bool ok = swprintf_s(parent, L"%lsnr-stage-captures", addon_directory) >= 0;
    if (ok && !CreateDirectoryW(parent, nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS) ok = false;
    if (ok) ok = swprintf_s(directory, L"%ls\\capture-%lu-%llu-%llu", parent,
        GetCurrentProcessId(), job->info.request_id, GetTickCount64()) >= 0;
    if (ok) ok = CreateDirectoryW(directory, nullptr) != FALSE;
#ifdef NR_HAS_NATIVE_OUTPUT_CAPTURE
    const bool directory_created=ok;
#endif
    const D3D12_RANGE read_range = {0, static_cast<SIZE_T>(job->bytes)};
    void *mapped = nullptr;
    if (ok) ok = SUCCEEDED(ID3D12Resource_Map(
        job->readback, 0, &read_range, &mapped)) && mapped != nullptr;
    if (mapped != nullptr) {
        const wchar_t *names[4] = {L"original", L"proxy", L"neural", L"composited"};
        const float *pixels = static_cast<const float *>(mapped);
        const std::size_t plane_values =
            static_cast<std::size_t>(job->info.width) * job->info.height * 4;
        const float *exposure = pixels + plane_values * 4;
        job->info.gpu_raw_exposure = exposure[0];
        job->info.gpu_resolved_exposure = exposure[1];
        job->info.gpu_source_to_canonical = exposure[2];
        job->info.gpu_pre_exposure = exposure[3];
        for (unsigned int stage = 0; stage < 4; ++stage) {
            const bool written = WriteStageCapturePfm(directory, names[stage],
                pixels + stage * plane_values, job->info.width, job->info.height);
            ok = written && ok;
        }
        // Keep alpha and non-finite bit patterns for debugging format/alpha
        // faults. Four planar RGBA32F images in the same order as the PFM files.
        wchar_t raw_path[MAX_PATH] = {};
        bool raw_ok = swprintf_s(raw_path, L"%ls\\stages.rgba32f", directory) >= 0;
        HANDLE raw = raw_ok ? CreateFileW(raw_path, GENERIC_WRITE, FILE_SHARE_READ,
            nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr) : INVALID_HANDLE_VALUE;
        raw_ok = raw != INVALID_HANDLE_VALUE;
        if (raw_ok) {
            raw_ok = job->bytes <= MAXDWORD && WriteStageCaptureBytes(raw, mapped, static_cast<DWORD>(job->bytes - 16u));
            CloseHandle(raw);
        }
        ok = raw_ok && ok;
        const D3D12_RANGE written_range = {0, 0};
        ID3D12Resource_Unmap(job->readback, 0, &written_range);
        ok = WriteStageCaptureMetadata(directory, job->info, ok) && ok;
    }
    ReleaseCom(job->readback);
#ifdef NR_HAS_NATIVE_OUTPUT_CAPTURE
    const bool native_export_ok=directory_created&&ExportNativeOutputCapture(directory,job->native_output);
    if(!native_export_ok) {
        job->native_output.header.status=nr_native_capture::Status::Missing;
        job->native_output.header.reason=nr_native_capture::Reason::ExportFailed;
    }
    // P/N/E export success and O presence are separate, explicit outcomes.
    PublishStageCaptureExportReceipt(job->info.request_id,directory,ok,job->native_output);
    ReleaseNativeOutputCapture(job->native_output);
#endif
    Log("Stage capture %s: request=%llu instance=%llu frame=%llu "
        "sampled=%ux%u stride=%u directory=%ls.", ok ? "exported" : "failed",
        job->info.request_id, job->info.identity.instance_id,
        job->info.identity.frame_id, job->info.width, job->info.height,
        job->info.stride, directory);
    FeedbackStageExported(job->info.request_id, directory, ok);
    delete job;
    g_stage_capture_last_result.store(ok ? 1 : -1, std::memory_order_relaxed);
    g_stage_capture_status.store(0, std::memory_order_release);
    return 0;
}

void CollectStageCaptureLocked(NrSlot &slot) {
    NrStageCapture &capture = slot.stage_capture;
    if (!capture.pending || !CanMutateSlotLocked(slot)) return;
    auto *job = new (std::nothrow) StageCaptureExport{};
    if (job == nullptr) return; // Retry after a later completed-slot poll.
    job->readback = capture.readback;
    job->bytes = capture.bytes;
    job->info = capture.info;
#ifdef NR_HAS_NATIVE_OUTPUT_CAPTURE
    job->native_output=capture.native_output;
    auto& native=job->native_output.header;
    // Even a refused/not-pre-SR sidecar retains the parent stage identity.
    native.request=capture.info.request_id;native.instance=capture.info.identity.instance_id;
    native.frame=capture.info.identity.frame_id;native.feature=capture.info.feature_generation;
    native.config=capture.info.identity.config_generation;native.evaluation=capture.info.identity.evaluation;
    if(capture.info.mode!=2)native.reason=nr_native_capture::Reason::NotPreSr;
    if(native.status==nr_native_capture::Status::Recorded) {
        if(slot.last_submission_queue&&slot.last_submission_queue->identity&&slot.completion_fence_value) {
            native.queue=slot.last_submission_queue->identity;native.fence=slot.completion_fence_value;
            native.status=nr_native_capture::Status::Complete;
        } else {
            native.status=nr_native_capture::Status::Missing;
            native.reason=nr_native_capture::Reason::CompletionUnknown;
        }
    }
#endif
    // No recorded or in-flight command list can reference this descriptor.
    // Restore its null binding before releasing the UAV. Readback ownership
    // transfers to the worker, so a recycled slot cannot overwrite its pixels.
    BindStageCaptureDescriptor(slot, nullptr, 0);
    ReleaseCom(capture.buffer);
    capture = {};
    g_stage_capture_status.store(3, std::memory_order_release);
    // The addon is process-pinned by HookWorker before any NR can execute.
    if (!QueueUserWorkItem(&ExportStageCapture, job, WT_EXECUTEDEFAULT)) {
        ReleaseCom(job->readback);
#ifdef NR_HAS_NATIVE_OUTPUT_CAPTURE
        ReleaseNativeOutputCapture(job->native_output);
#endif
        delete job;
        Log("Stage capture export worker could not start; rendering continues.");
        g_stage_capture_last_result.store(-1, std::memory_order_relaxed);
        g_stage_capture_status.store(0, std::memory_order_release);
    }
}
