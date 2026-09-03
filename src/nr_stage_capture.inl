// Included after the slot/context declarations and exact-fence predicate.
// All slot operations run under g_chain_cs. Only the export job owns objects
// after detachment; the worker does not access slot/context state.
std::atomic<unsigned int> g_stage_capture_status{0};
std::atomic<int> g_stage_capture_last_result{0};
std::atomic<unsigned long long> g_stage_capture_request_id{0};

unsigned int StageCaptureStatus() {
    return g_stage_capture_status.load(std::memory_order_acquire);
}

bool RequestStageCapture() {
    unsigned int expected = 0;
    // Reserve the request before publishing its identity. State 4 is a brief
    // UI-thread initialization state, never eligible for a render-thread arm.
    if (!g_stage_capture_status.compare_exchange_strong(
            expected, 4, std::memory_order_acq_rel)) return false;
    g_stage_capture_request_id.fetch_add(1, std::memory_order_relaxed);
    g_stage_capture_last_result.store(0, std::memory_order_relaxed);
    g_stage_capture_status.store(1, std::memory_order_release);
    Log("Stage capture requested: next eligible warmed-up NR composition; "
        "one outstanding request, sampled raw stages, no native-SR output read.");
    return true;
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
        bytes = static_cast<UINT64>(width) * height * 4u * 16u;
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
    ReleaseCom(slot.stage_capture.readback);
    ReleaseCom(slot.stage_capture.buffer);
    slot.stage_capture = {};
    if (owned_request) {
        // A failed create/evaluate or discarded list must not silently consume
        // the user's one-shot request. The enclosing slot retirement already
        // proved safety; let a later successful instance fulfil the request.
        g_stage_capture_status.store(1, std::memory_order_release);
    }
}

void ArmStageCaptureLocked(NrContext &context, NrSlot &slot) {
    if (StageCaptureStatus() != 1 || slot.stage_capture.buffer != nullptr ||
        context.nr_handle == nullptr ||
        context.successful_evaluations_since_reset < g_config.warmup_after_create ||
        !CanMutateSlotLocked(slot)) return;
    NrStageCapture capture = {};
    if (!ComputeStageCaptureGrid(context.width, context.height,
            capture.info.stride, capture.info.width, capture.info.height,
            capture.bytes)) {
        Log("Stage capture rejected unsupported source extent %ux%u.",
            context.width, context.height);
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
    capture.info.source_width = context.width;
    capture.info.source_height = context.height;
    capture.info.source_format = static_cast<unsigned int>(slot.source_format);
    capture.info.result_format = static_cast<unsigned int>(slot.result_format);
    capture.info.mode = context.mode;
    capture.armed = true;
    slot.stage_capture = capture;
    BindStageCaptureDescriptor(slot, capture.buffer, capture.bytes);
    g_stage_capture_status.store(2, std::memory_order_release);
}

struct StageCaptureExport {
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
        "format=NRStageCapture-v1\nbuild=%s\ncomplete=%d\n"
        "request=%llu\ninstance=%llu\nfeatureGeneration=%llu\nframe=%llu\n"
        "configGeneration=%u\nevaluation=%u\nsinceReset=%u\nwarmedUp=%d\nmode=%d\n"
        "sourceExtent=%ux%u\nsampledExtent=%ux%u\nstride=%u\n"
        "sourceBase=%u,%u\ndestinationBase=%u,%u\nsourceFormat=%u\nresultFormat=%u\n"
        "codecFlags=%u\nintensity=%.9g\nstyle=%u\nlocalTone=%.9g\n"
        "localStructure=%.9g\nskinStructure=%.9g\nautoMask=%d\nuiCorrection=%d\n"
        "proxyScale=%.9g\ntransfer=%.9g\ncolorFollowEffective=%.9g\nmaxLumaRatio=%.9g\n"
        "preExposure=%.9g\nhasPreExposure=%d\nscalarExposureScale=%.9g\nhasExposureScale=%d\n"
        "sampling=nearest grid at active-region coordinates (x*stride,y*stride)\n"
        "pixels=PFM little-endian float RGB, raw shader values; alpha omitted\n"
        "original=storage-domain shader load; proxy/neural=model encoded domain\n"
        "composited=shader result BEFORE destination format quantization\n"
        "nativeSrOutput=%s\n"
        "displayScreenshot=not captured; separately triggered ReShade screenshots are NOT same-frame evidence\n"
        "exposureTexture=when codecFlags bit64 is set, GPU samples external texture; this file records scalars only\n"
        "comparison=decode with the recorded codec contract; raw stages do not share a photometric domain\n",
        kBuildIdentity, images_ok ? 1 : 0, info.request_id, id.instance_id,
        info.feature_generation, id.frame_id, id.config_generation, id.evaluation,
        id.since_reset, id.warmed_up ? 1 : 0, info.mode,
        info.source_width, info.source_height, info.width, info.height, info.stride,
        info.source_base_x, info.source_base_y, info.destination_base_x,
        info.destination_base_y, info.source_format, info.result_format, info.codec_flags,
        static_cast<double>(id.tuning.intensity), id.tuning.style,
        static_cast<double>(id.tuning.local_tone), static_cast<double>(id.tuning.local_structure),
        static_cast<double>(id.tuning.skin_structure), id.tuning.auto_mask ? 1 : 0,
        id.tuning.ui_correction ? 1 : 0, static_cast<double>(info.proxy_scale),
        static_cast<double>(info.transfer_strength), static_cast<double>(info.color_strength),
        static_cast<double>(info.max_luma_ratio), static_cast<double>(info.pre_exposure),
        info.has_pre_exposure ? 1 : 0, static_cast<double>(info.exposure_scale),
        info.has_exposure_scale ? 1 : 0,
        info.mode == 1 ? "represented by original stage (post-SR source/snapshot)"
                       : "unavailable; arrival state not proven, so no extra output read/transition");
    const bool ok = length > 0 && static_cast<std::size_t>(length) < sizeof(text) &&
        WriteStageCaptureBytes(file, text, static_cast<DWORD>(length));
    CloseHandle(file);
    return ok;
}

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
    const D3D12_RANGE read_range = {0, static_cast<SIZE_T>(job->bytes)};
    void *mapped = nullptr;
    if (ok) ok = SUCCEEDED(ID3D12Resource_Map(
        job->readback, 0, &read_range, &mapped)) && mapped != nullptr;
    if (mapped != nullptr) {
        const wchar_t *names[4] = {L"original", L"proxy", L"neural", L"composited"};
        const float *pixels = static_cast<const float *>(mapped);
        const std::size_t plane_values =
            static_cast<std::size_t>(job->info.width) * job->info.height * 4;
        for (unsigned int stage = 0; stage < 4; ++stage) {
            const bool written = WriteStageCapturePfm(directory, names[stage],
                pixels + stage * plane_values, job->info.width, job->info.height);
            ok = written && ok;
        }
        const D3D12_RANGE written_range = {0, 0};
        ID3D12Resource_Unmap(job->readback, 0, &written_range);
        ok = WriteStageCaptureMetadata(directory, job->info, ok) && ok;
    }
    ReleaseCom(job->readback);
    Log("Stage capture %s: request=%llu instance=%llu frame=%llu "
        "sampled=%ux%u stride=%u directory=%ls.", ok ? "exported" : "failed",
        job->info.request_id, job->info.identity.instance_id,
        job->info.identity.frame_id, job->info.width, job->info.height,
        job->info.stride, directory);
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
        delete job;
        Log("Stage capture export worker could not start; rendering continues.");
        g_stage_capture_last_result.store(-1, std::memory_order_relaxed);
        g_stage_capture_status.store(0, std::memory_order_release);
    }
}
