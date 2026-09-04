// One explicitly requested session. Rendering only publishes small snapshots;
// file reads, hashing and ZIP writing run on a process-pinned worker.
std::atomic<bool> g_feedback_requested{false};
std::atomic<unsigned int> g_feedback_status{0}; // idle, collecting, packaging, complete, partial, failed
SRWLOCK g_feedback_lock = SRWLOCK_INIT;
struct FeedbackSession {
    ULONGLONG started = 0;
    unsigned long long stage_request = 0;
    unsigned int shots_requested = 0;
    bool stage_finished = false;
    bool stage_ok = false;
    char stage_reason[128] = {};
    char postfix[64] = {};
    wchar_t screenshots[3][1024] = {};
    ULONGLONG screenshot_ticks[3] = {};
    wchar_t stage_directory[1024] = {};
    char report[32768] = {};
    std::size_t report_used = 0;
    bool report_truncated = false;
    unsigned int observations = 0;
    unsigned int timeline_omitted = 0;
    ULONGLONG last_observation = 0;
    reshade::api::effect_runtime *runtime_identity = nullptr; // comparison only on worker
};
FeedbackSession *g_feedback_session = nullptr; // protected by g_feedback_lock
wchar_t g_feedback_last_zip[1024] = {};
std::atomic<unsigned int> g_feedback_swap_color_space{0};
std::atomic<unsigned int> g_feedback_swap_format{0};

void FeedbackAppend(FeedbackSession &s, const char *format, ...) {
    // Reserve a final status record. Reject an overflowing append as a whole:
    // partial UTF-8 metadata would make the ZIP job itself invalid.
    if (s.report_used >= sizeof(s.report) - 256) {
        s.report_truncated = true; return;
    }
    const std::size_t available = sizeof(s.report) - s.report_used - 256;
    va_list args;
    va_start(args, format);
    const int n = std::vsnprintf(s.report + s.report_used,
        available, format, args);
    va_end(args);
    if (n < 0 || static_cast<std::size_t>(n) >= available) {
        s.report[s.report_used] = 0;
        s.report_truncated = true;
    } else s.report_used += static_cast<std::size_t>(n);
}

bool RequestFeedbackBundle() {
    unsigned int status = g_feedback_status.load(std::memory_order_acquire);
    if (status == 1 || status == 2) return false;
    return !g_feedback_requested.exchange(true, std::memory_order_acq_rel);
}

void FeedbackStageExported(unsigned long long request, const wchar_t *directory, bool ok) {
    AcquireSRWLockExclusive(&g_feedback_lock);
    auto *s = g_feedback_session;
    if (s != nullptr && s->stage_request == request) {
        s->stage_finished = true;
        s->stage_ok = ok;
        if (directory != nullptr && wcslen(directory) < std::size(s->stage_directory))
            wcscpy_s(s->stage_directory, directory);
        strcpy_s(s->stage_reason, ok ? "exported after exact GPU fence" : "stage export failed; inspect NR log");
    }
    ReleaseSRWLockExclusive(&g_feedback_lock);
}

void OnFeedbackScreenshot(reshade::api::effect_runtime *runtime, const char *path) {
    if (path == nullptr) return;
    AcquireSRWLockExclusive(&g_feedback_lock);
    auto *s = g_feedback_session;
    // The event fires only after ReShade closes its image file. Never search a
    // screenshot directory or accidentally attach an unrelated user's image.
    if (s != nullptr && s->runtime_identity == runtime &&
        g_feedback_status.load(std::memory_order_relaxed) == 1) {
        const char *basename = path;
        for (const char *p = path; *p; ++p) if (*p == '/' || *p == '\\') basename = p + 1;
        for (unsigned int i = 0; i < s->shots_requested && i < 3; ++i) {
            char tag[80] = {};
            std::snprintf(tag, sizeof(tag), "%s_%u", s->postfix, i);
            const char *match = std::strstr(basename, tag);
            if (match != nullptr && match[std::strlen(tag)] == '.' && s->screenshots[i][0] == 0) {
                if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                        s->screenshots[i], static_cast<int>(std::size(s->screenshots[i]))) == 0)
                    s->screenshots[i][0] = 0;
                else {
                    // ReShade retains '.\\' in its absolute saved path. Resolve
                    // that syntax before the ZIP allowlist validates components.
                    wchar_t canonical[1024] = {};
                    const DWORD n = GetFullPathNameW(s->screenshots[i],
                        static_cast<DWORD>(std::size(canonical)), canonical, nullptr);
                    if (n != 0 && n < std::size(canonical)) {
                        wcscpy_s(s->screenshots[i], canonical);
                        s->screenshot_ticks[i] = GetTickCount64();
                    } else s->screenshots[i][0] = 0;
                }
            }
        }
    }
    ReleaseSRWLockExclusive(&g_feedback_lock);
}

void FeedbackObserveEvaluate(const NgxParams *params, const FeatureRecord &feature,
                             unsigned long long carrier) {
    if (g_feedback_status.load(std::memory_order_relaxed) != 1) return;
    if (!TryAcquireSRWLockExclusive(&g_feedback_lock)) return;
    auto *s = g_feedback_session;
    const ULONGLONG now = GetTickCount64();
    if (s != nullptr && s->observations + s->timeline_omitted < 120 &&
        now - s->last_observation >= 100) {
        s->last_observation = now;
        // Leave space for the stage outcome, policy, counters and screenshot
        // identity even when paths/numbers make the optional timeline longer.
        if (s->report_used >= sizeof(s->report) - 8192) {
            ++s->timeline_omitted;
            s->report_truncated = true;
            ReleaseSRWLockExclusive(&g_feedback_lock);
            return;
        }
        ++s->observations;
        float jitter_x = NAN, jitter_y = NAN, mv_x = NAN, mv_y = NAN;
        float pre = NAN, exposure = NAN;
        int reset = -1;
        GetFloat(params, "Jitter.Offset.X", &jitter_x);
        GetFloat(params, "Jitter.Offset.Y", &jitter_y);
        GetFloat(params, "MV.Scale.X", &mv_x);
        GetFloat(params, "MV.Scale.Y", &mv_y);
        GetFloat(params, "DLSS.Pre.Exposure", &pre);
        GetFloat(params, "DLSS.Exposure.Scale", &exposure);
        GetInt(params, "Reset", &reset);
        FeedbackAppend(*s, "frame,%llu,%llu,%llu,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%s\n",
            now - s->started, carrier, feature.generation, reset,
            jitter_x, jitter_y, mv_x, mv_y, pre, exposure,
            BypassReasonName(static_cast<BypassReason>(g_last_bypass_reason.load())));
        if (s->observations == 1 || s->observations == 60) {
            FeedbackAppend(*s, "feature.flags=%d feature.create=%ux%u->%ux%u\n",
                feature.create_flags, feature.width, feature.height, feature.out_width, feature.out_height);
            for (const char *key : {"Color", "Output", "Depth", "MotionVectors", "ExposureTexture"}) {
                ID3D12Resource *resource = nullptr;
                if (GetResource(params, key, &resource)) {
                    const auto d = ID3D12Resource_GetDesc(resource);
                    FeedbackAppend(*s, "resource.%s=dim:%u size:%llux%u array:%u mip:%u format:%u samples:%u flags:%u\n",
                        key, static_cast<unsigned int>(d.Dimension), d.Width, d.Height,
                        d.DepthOrArraySize, d.MipLevels, static_cast<unsigned int>(d.Format),
                        d.SampleDesc.Count, static_cast<unsigned int>(d.Flags));
                } else FeedbackAppend(*s, "resource.%s=absent\n", key);
            }
        }
    }
    ReleaseSRWLockExclusive(&g_feedback_lock);
}

bool FeedbackCreateDirectory(wchar_t (&directory)[1024]) {
    for (unsigned int attempt = 0; attempt < 2; ++attempt) {
        wchar_t parent[MAX_PATH] = {};
        if (attempt == 0) {
            if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, parent))) continue;
        } else {
            const DWORD n = GetTempPathW(MAX_PATH, parent);
            if (n == 0 || n >= MAX_PATH) continue;
        }
        wchar_t root[1024] = {};
        if (swprintf_s(root, L"%ls\\DLSS5-Feedback", parent) < 0) continue;
        if (!CreateDirectoryW(root, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) continue;
        SYSTEMTIME t = {}; GetSystemTime(&t);
        if (swprintf_s(directory, L"%ls\\%04u%02u%02u-%02u%02u%02u-%lu-%llu", root,
                t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
                GetCurrentProcessId(), GetTickCount64()) < 0) continue;
        if (CreateDirectoryW(directory, nullptr)) return true;
    }
    directory[0] = 0;
    return false;
}

bool FeedbackModuleRelevant(const wchar_t *name) {
    wchar_t lower[MAX_PATH] = {};
    wcsncpy_s(lower, name, _TRUNCATE);
    CharLowerBuffW(lower, static_cast<DWORD>(wcslen(lower)));
    return wcsstr(lower, L".addon") != nullptr || wcsstr(lower, L"ngx") != nullptr ||
        wcsstr(lower, L"dlss") != nullptr || wcsstr(lower, L"reshade") != nullptr ||
        wcscmp(lower, L"dxgi.dll") == 0 || wcscmp(lower, L"d3d12.dll") == 0 ||
        wcscmp(lower, L"d3dcompiler_47.dll") == 0 || wcscmp(lower, L"nvwgf2umx.dll") == 0;
}

void FeedbackAddModules(nr_bundle::Job &job, FeedbackSession &s) {
    (void)s;
    HMODULE own = nullptr;
    wchar_t own_path[1024] = {};
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&OnFeedbackScreenshot), &own) &&
        GetModuleFileNameW(own, own_path, static_cast<DWORD>(std::size(own_path))))
        nr_bundle::AddFile(job, own_path, "modules/current-addon", nr_bundle::FileKind::HashOnly, true);
    else nr_bundle::AddMissing(job, "modules/current-addon", "cannot identify loaded addon path", true);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) {
        nr_bundle::AddMissing(job, "modules", "module snapshot failed", true); return;
    }
    MODULEENTRY32W entry = {}; entry.dwSize = sizeof(entry);
    unsigned int count = 0, relevant = 0;
    for (BOOL more = Module32FirstW(snapshot, &entry); more; more = Module32NextW(snapshot, &entry)) {
        if (entry.hModule == own || !FeedbackModuleRelevant(entry.szModule)) continue;
        ++relevant;
        if (count >= 32) continue;
        char leaf[1024] = {}; WideToUtf8(entry.szModule, leaf, sizeof(leaf));
        char name[1100] = {};
        std::snprintf(name, sizeof(name), "modules/%02u-%s", count++, leaf);
        nr_bundle::AddFile(job, entry.szExePath, name, nr_bundle::FileKind::HashOnly, true);
    }
    CloseHandle(snapshot);
    if (relevant > count) nr_bundle::AddMissing(job, "modules/remaining", "module inventory capped at 32 entries", false);
}

void FeedbackAddLogBeside(nr_bundle::Job &job, const wchar_t *module_path,
                         const wchar_t *leaf, const char *name, bool required) {
    wchar_t path[1024] = {};
    if (wcslen(module_path) >= std::size(path)) return;
    wcscpy_s(path, module_path);
    auto slash = wcsrchr(path, L'\\');
    if (slash == nullptr) return;
    slash[1] = 0;
    if (wcscat_s(path, leaf) == 0)
        nr_bundle::AddFile(job, path, name, nr_bundle::FileKind::LogTail, required);
}

DWORD WINAPI FeedbackWorker(void *parameter) {
    auto *s = static_cast<FeedbackSession *>(parameter);
    // A timeout freezes the file list; late GPU exports remain in the ordinary
    // stage directory and can never be mistaken for a different request.
    for (;;) {
        Sleep(100);
        AcquireSRWLockShared(&g_feedback_lock);
        bool ready = s->stage_finished;
        for (const auto &shot : s->screenshots) ready = ready && shot[0] != 0;
        ReleaseSRWLockShared(&g_feedback_lock);
        if ((ready && GetTickCount64() - s->started >= 5000) ||
            GetTickCount64() - s->started >= 30000) break;
    }
    AcquireSRWLockExclusive(&g_feedback_lock);
    g_feedback_status.store(2, std::memory_order_release);
    g_feedback_session = nullptr;
    ReleaseSRWLockExclusive(&g_feedback_lock);
    FeedbackAppend(*s, "collection.elapsedMs=%llu\ncollection.parameterSamples=%u\n",
        GetTickCount64() - s->started, s->observations);
    FeedbackAppend(*s, "stage.request=%llu\nstage.completed=%d\nstage.reason=%s\n",
        s->stage_request, s->stage_ok ? 1 : 0, s->stage_reason);
    for (unsigned int i = 0; i < 3; ++i)
        FeedbackAppend(*s, "screenshot.%u.savedElapsedMs=%llu\n", i,
            s->screenshot_ticks[i] ? s->screenshot_ticks[i] - s->started : 0);
    const auto color = SnapshotColorPolicy();
    FeedbackAppend(*s, "colorPolicy.valuesMeaning=last recorded Combine; current only when status is recent-recorded-combine; not display completion\n"
        "colorPolicy.status=%s\ncolorPolicy.frame=%llu\ncolorPolicy.config=%u\n"
        "colorPolicy.requested=%.9g\ncolorPolicy.effective=%.9g\ncolorPolicy.reason=%s\n"
        "lightingPolicy.requested=%d\nlightingPolicy.effective=%d\nlightingPolicy.reason=%s\n",
        NrColorPolicyStatusName(color.status), color.frame_id, color.config_generation,
        color.policy.requested_color_strength, color.policy.effective_color_strength,
        NrColorReasonName(color.policy.color_reason), color.policy.requested_preserve_lighting ? 1 : 0,
        color.policy.effective_preserve_lighting ? 1 : 0, NrLightingReasonName(color.policy.lighting_reason));
    FeedbackAppend(*s, "end.nrSuccess=%llu\nend.transferSubmitted=%llu\nend.bypass=%s\n",
        g_nr_successful_evaluations.load(), g_nr_transfer_submitted_frames.load(),
        BypassReasonName(static_cast<BypassReason>(g_last_bypass_reason.load())));
    // FeedbackAppend always preserves these final 256 bytes, even on overflow.
    const int summary_bytes = std::snprintf(s->report + s->report_used,
        sizeof(s->report) - s->report_used,
        "collection.metadataTruncated=%d\ncollection.timelineRowsOmitted=%u\n",
        s->report_truncated ? 1 : 0, s->timeline_omitted);
    if (summary_bytes > 0 && static_cast<std::size_t>(summary_bytes) <
            sizeof(s->report) - s->report_used)
        s->report_used += static_cast<std::size_t>(summary_bytes);
    wchar_t directory[1024] = {};
    nr_bundle::Result result = {};
    result.status = nr_bundle::Status::Failed;
    if (FeedbackCreateDirectory(directory)) {
        auto *job = nr_bundle::CreateJob(directory, s->report);
        if (job != nullptr) {
            nr_bundle::AddFile(*job, g_log_path, "logs/nr-current.log", nr_bundle::FileKind::LogTail, true);
            FeedbackAddLogBeside(*job, g_log_path, L"nr-before-sr.previous.log", "logs/nr-previous.log", false);
            FeedbackAddLogBeside(*job, g_log_path, L"ReShade.log", "logs/reshade-addon-dir.log", false);
            wchar_t exe[1024] = {};
            GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
            FeedbackAddLogBeside(*job, exe, L"ReShade.log", "logs/reshade-game-dir.log", false);
            wchar_t reshade_path[1024] = {};
            if (GetModuleFileNameW(reshade::internal::get_reshade_module_handle(), reshade_path,
                    static_cast<DWORD>(std::size(reshade_path))))
                FeedbackAddLogBeside(*job, reshade_path, L"ReShade.log", "logs/reshade-module-dir.log", true);
            nr_bundle::AddFile(*job, g_ini_path, "settings/nr_before_sr.ini", nr_bundle::FileKind::Copy, true);
            nr_bundle::AddFile(*job, g_reshade_ini_path, "settings/ReShade.ini", nr_bundle::FileKind::Copy, false);
            FeedbackAddModules(*job, *s);
            for (unsigned int i = 0; i < 3; ++i) {
                char name[96] = {};
                const wchar_t *extension = wcsrchr(s->screenshots[i], L'.');
                char ext[16] = {};
                if (extension != nullptr && wcslen(extension) < 12) WideToUtf8(extension, ext, sizeof(ext));
                std::snprintf(name, sizeof(name), "screenshots/current-%u%s", i, ext);
                if (s->screenshots[i][0])
                    nr_bundle::AddFile(*job, s->screenshots[i], name, nr_bundle::FileKind::Copy, true, 128ull * 1024 * 1024);
                else nr_bundle::AddMissing(*job, name, "ReShade did not deliver this request's saved image before timeout", true);
            }
            if (s->stage_finished && s->stage_ok) {
                for (const wchar_t *leaf : {L"original.pfm", L"proxy.pfm", L"neural.pfm", L"composited.pfm", L"metadata.txt", L"stages.rgba32f"}) {
                    wchar_t path[1200] = {};
                    swprintf_s(path, L"%ls\\%ls", s->stage_directory, leaf);
                    char name[128] = {}; char utf8[64] = {};
                    WideToUtf8(leaf, utf8, sizeof(utf8));
                    std::snprintf(name, sizeof(name), "stages/%s", utf8);
                    nr_bundle::AddFile(*job, path, name, nr_bundle::FileKind::Copy, true);
                }
            } else nr_bundle::AddMissing(*job, "stages", s->stage_reason[0] ? s->stage_reason :
                "no completed exact-fence NR capture within 30 seconds; see bypass and NGX timeline", true);
            nr_bundle::AddMissing(*job, "guides/depth-motion-raw", "resource layouts/scales/jitter are recorded; raw guide textures not read without a verified state contract", false);
            nr_bundle::AddMissing(*job, "native-sr-same-frame", "display screenshots and NR stages have independent frame identities; native SR output is not an extra readback", false);
            result = nr_bundle::Run(*job);
            nr_bundle::DestroyJob(job);
        }
    }
    AcquireSRWLockExclusive(&g_feedback_lock);
    wcscpy_s(g_feedback_last_zip, result.zip_path);
    ReleaseSRWLockExclusive(&g_feedback_lock);
    Log("[feedback] result=%u zip=%ls included=%u missing=%u requiredComplete=%d win32=%lu",
        static_cast<unsigned int>(result.status), result.zip_path,
        result.included_files, result.missing_files, result.required_complete ? 1 : 0, result.win32_error);
    g_feedback_status.store(result.status == nr_bundle::Status::Failed ? 5 :
        (result.required_complete ? 3 : 4), std::memory_order_release);
    delete s;
    return 0;
}

void FeedbackPresent(reshade::api::effect_runtime *runtime) {
    bool begin_session = false;
    if (g_feedback_requested.exchange(false, std::memory_order_acq_rel)) {
        unsigned int status = g_feedback_status.load(std::memory_order_acquire);
        while (status != 1 && status != 2) {
            // Claim before calling runtime APIs or requesting GPU stages. Two
            // Present threads must never publish competing sessions/workers.
            if (g_feedback_status.compare_exchange_weak(status, 1,
                    std::memory_order_acq_rel, std::memory_order_acquire)) {
                begin_session = true;
                break;
            }
        }
    }
    if (begin_session) {
        AcquireSRWLockExclusive(&g_feedback_lock);
        g_feedback_last_zip[0] = 0;
        ReleaseSRWLockExclusive(&g_feedback_lock);
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&FeedbackWorker), &pinned)) {
            g_feedback_status.store(5); return;
        }
        auto *s = new (std::nothrow) FeedbackSession{};
        if (s == nullptr) { g_feedback_status.store(5); return; }
        s->started = GetTickCount64(); s->runtime_identity = runtime;
        std::snprintf(s->postfix, sizeof(s->postfix), "DLSS5_FB_%lu_%llu", GetCurrentProcessId(), s->started);
        const ChainConfig config = SnapshotConfig();
        const auto device = runtime->get_device();
        char gpu[256] = {}; uint32_t driver = 0, vendor = 0;
        const bool gpu_known = device->get_property(reshade::api::device_properties::description, gpu);
        const bool driver_known = device->get_property(reshade::api::device_properties::driver_version, &driver);
        device->get_property(reshade::api::device_properties::vendor_id, &vendor);
        uint32_t width = 0, height = 0; runtime->get_screenshot_width_and_height(&width, &height);
        // ReShade resource handles are C++ return-by-value wrappers. MinGW and
        // MSVC disagree on their return ABI: get_current_back_buffer crashed
        // the F8 CP repro by passing index=2 as an output pointer. Use the
        // native COM GetDesc out-parameter snapshot published by Present.
        wchar_t exe[1024] = {}; GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
        char exe_utf8[4096] = {}; WideToUtf8(exe, exe_utf8, sizeof(exe_utf8));
        NrDiagnosticIdentity applied = {};
        EnterCriticalSection(&g_chain_cs); applied = g_applied_nr_identity; LeaveCriticalSection(&g_chain_cs);
        FeedbackAppend(*s, "format=DLSS5Feedback-v1\nbuild=%s\nsession=%s\nexe=%s\n"
            "gpu=%s\ndriver.raw=%u\ndriver.known=%d\nvendor=%u\napi=%u\n"
            "display.extent=%ux%u\ndisplay.storageFormat=%u\ndisplay.colorSpace=%u\n"
            "requested.enabled=%d\nrequested.mode=%d\nrequested.config=%u\nrequested.intensity=%.9g\nrequested.style=%u\n"
            "requested.blend=%.9g\nrequested.color=%.9g\nrequested.lighting=%d\n"
            "applied.instance=%llu\napplied.frame=%llu\napplied.config=%u\napplied.intensity=%.9g\napplied.style=%u\n"
            "start.nrSuccess=%llu\nstart.transferSubmitted=%llu\nstart.bypass=%s\n"
            "capture.automaticSettingChanges=none\ncapture.stageRequestRecreatesNRHistory=1\n"
            "capture.displayFrames=three current-chain images, independent from NR stages, not controlled A/B\n"
            "capture.rawStageDomain=see stages/metadata.txt; before destination quantization\n"
            "capture.crashDump=not generated; game may supply crash report separately\n"
            "timeline.columns=tag,elapsedMs,SRcarrier,featureGeneration,reset,jitterX,jitterY,mvScaleX,mvScaleY,preExposure,exposureScale,previousBypass\n",
            kBuildIdentity, s->postfix, exe_utf8, gpu_known ? gpu : "unknown", driver, driver_known ? 1 : 0,
            vendor, static_cast<unsigned int>(device->get_api()), width, height,
            g_feedback_swap_format.load(), g_feedback_swap_color_space.load(),
            config.enabled ? 1 : 0, config.mode, g_config_generation.load(), config.intensity, config.style,
            config.mode == 2 ? config.transfer_strength : config.post_transfer_strength,
            config.color_strength, config.preserve_lighting ? 1 : 0,
            applied.instance_id, applied.frame_id, applied.config_generation, applied.tuning.intensity,
            applied.tuning.style, g_nr_successful_evaluations.load(), g_nr_transfer_submitted_frames.load(),
            BypassReasonName(static_cast<BypassReason>(g_last_bypass_reason.load())));
        // RequestStageCapture uses atomics and Log only, never g_chain_cs.
        // Keep session publication and request identity in this one lock so
        // an export completing on another worker cannot race past registration.
        AcquireSRWLockExclusive(&g_feedback_lock);
        g_feedback_session = s;
        if (!config.enabled || g_nr_successful_evaluations.load() == 0) {
            s->stage_finished = true; strcpy_s(s->stage_reason, "NR disabled or no successful NR frame yet; current-chain screenshots and logs collected without enabling NR");
        } else if (RequestStageCapture()) {
            s->stage_request = g_stage_capture_request_id.load();
        } else {
            s->stage_finished = true; strcpy_s(s->stage_reason, "another stage request was already in progress; it is not attached to this session");
        }
        g_feedback_status.store(1, std::memory_order_release);
        ReleaseSRWLockExclusive(&g_feedback_lock);
        if (!QueueUserWorkItem(&FeedbackWorker, s, WT_EXECUTEDEFAULT)) {
            AcquireSRWLockExclusive(&g_feedback_lock); g_feedback_session = nullptr; ReleaseSRWLockExclusive(&g_feedback_lock);
            delete s; g_feedback_status.store(5); return;
        }
        Log("[feedback] one-click session requested; current settings retained, 3 display images, exact-fence stages, bounded 30-second wait.");
    }
    char tag[80] = {};
    AcquireSRWLockExclusive(&g_feedback_lock);
    auto *s = g_feedback_session;
    if (s != nullptr && s->runtime_identity == runtime && s->shots_requested < 3 &&
        GetTickCount64() - s->started >= s->shots_requested * 2000ull) {
        std::snprintf(tag, sizeof(tag), "%s_%u", s->postfix, s->shots_requested++);
    }
    ReleaseSRWLockExclusive(&g_feedback_lock);
    if (tag[0]) runtime->save_screenshot(tag);
}

void DrawFeedbackControls() {
    const auto status = g_feedback_status.load(std::memory_order_acquire);
    if (status != 1 && status != 2) {
        if (ImGui::Button(kChineseUi ? "一键采集反馈包（F8）" : "Collect feedback ZIP (F8)"))
            RequestFeedbackBundle();
    } else ImGui::TextUnformatted(kChineseUi ? "正在采集反馈包，请保持机位，最多等待约 30 秒……" :
        "Collecting feedback. Keep the camera still; allow about 30 seconds...");
    ImGui::TextWrapped("%s", kChineseUi ?
        "采集当前画面、NR 各阶段、设置、版本和日志；不自动切换模式。只保存在本机，请将 ZIP 发给开发者。" :
        "Collects current images, NR stages, settings, versions and logs without switching modes. Saved locally; send the ZIP to the developer.");
    if (status >= 3) {
        ImGui::TextWrapped("%s", status == 3 ? (kChineseUi ? "反馈包已生成。" : "Feedback ZIP saved.") :
            status == 4 ? (kChineseUi ? "反馈包已生成；部分画面未采到，包内已记录原因，仍可提交。" :
                "Feedback ZIP saved with missing items explained. It can still be submitted.") :
                (kChineseUi ? "反馈包保存失败，请检查日志和磁盘空间。" : "Could not save feedback. Check the log and disk space."));
        char path[4096] = {};
        AcquireSRWLockShared(&g_feedback_lock); WideToUtf8(g_feedback_last_zip, path, sizeof(path)); ReleaseSRWLockShared(&g_feedback_lock);
        if (path[0]) {
            ImGui::TextWrapped("%s", path);
            if (ImGui::Button(kChineseUi ? "复制反馈包路径" : "Copy ZIP path")) ImGui::SetClipboardText(path);
        }
    }
}
