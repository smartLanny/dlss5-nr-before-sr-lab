// Included after nr_feedback_capture.inl. Reuses its bounded parameter observer,
// not its legacy screenshot/stage worker. No runtime pointer leaves Present.
#include "nr_feedback_text.h"
#include "nr_feedback_ticket.h"
#include "nr_feedback_single_text.h"
#include "interop/nr_deployment_names.h"
namespace nr_core_feedback {
#define NR_HAS_CORE_TEXT_REPORT 1
#include "nr_feedback_report.inl"
struct PairSnapshot {
    char session[96] = {};
    unsigned config = 0;
    bool ready = false, complete = false, archive_requested = false;
};
struct CoreSession {
    FeedbackSession activity{};
    wchar_t directory[1024] = {};
    wchar_t exe[1024] = {}, reshade_module[1024] = {};
    std::wstring carrier_log; // Worker-only path from the uniquely matched loaded companion.
    char session[96] = {};
    bool snapshot_ok = false, archive_requested = false;
    SYSTEMTIME local_time{}, utc_time{};
    unsigned long long serial = 0;
};
std::shared_ptr<const std::string> g_full_report; // g_feedback_lock
wchar_t g_report_path[1024] = {};
DWORD g_report_error = ERROR_SUCCESS;
bool g_report_complete = false;
PairSnapshot g_pair{}; // g_feedback_lock
char g_reason[160] = {}; // g_feedback_lock
char g_submission[nr_feedback_ticket::kCapacity] = {}; // same session, g_feedback_lock
std::atomic<unsigned long long> g_serial{0};
std::atomic<bool> g_cancel{false};
nr_bundle::Job *g_job = nullptr; // cancellation pointer, g_feedback_lock

bool Busy() {
    const auto status = g_feedback_status.load(std::memory_order_acquire);
    return status == 1 || status == 2 || g_feedback_requested.load(std::memory_order_acquire);
}
PairSnapshot SnapshotPair() {
    AcquireSRWLockShared(&g_feedback_lock);
    const auto pair = g_pair;
    ReleaseSRWLockShared(&g_feedback_lock);
    return pair;
}
// The caller serializes image/core requests with nr_f8::g_control.
bool Request(bool archive_requested = false) {
    AcquireSRWLockExclusive(&g_feedback_lock);
    if (Busy()) { ReleaseSRWLockExclusive(&g_feedback_lock); return false; }
    g_pair = {};
    g_pair.archive_requested = archive_requested;
    g_full_report.reset(); g_report_path[0] = 0; g_report_error = ERROR_SUCCESS; g_report_complete = false;
    // No allocation or exception is possible while holding the publication lock.
    std::snprintf(g_pair.session, sizeof(g_pair.session), "DLSS5_CORE_%lu_%llu_%llu",
        GetCurrentProcessId(), GetTickCount64(), ++g_serial);
    g_reason[0] = 0; g_feedback_last_zip[0] = 0; g_submission[0] = 0;
    g_cancel.store(false);
    g_feedback_status.store(1, std::memory_order_release);
    g_feedback_requested.store(true, std::memory_order_release);
    ReleaseSRWLockExclusive(&g_feedback_lock);
    return true;
}
void Cancel() {
    g_cancel.store(true, std::memory_order_release);
    AcquireSRWLockExclusive(&g_feedback_lock);
    if (g_job) nr_bundle::RequestCancel(*g_job);
    ReleaseSRWLockExclusive(&g_feedback_lock);
}
void Publish(const nr_bundle::Result &result, bool complete, const char *reason,
             const char *report = nullptr, const nr_feedback_report::Output *text = nullptr, bool archive_requested = true) {
    char submission[nr_feedback_ticket::kCapacity] = {};
    if (result.zip_path[0] || (text && text->text))
        nr_feedback_ticket::Build(submission, sizeof(submission), report,
            archive_requested ? complete && result.required_complete : text->complete);

    AcquireSRWLockExclusive(&g_feedback_lock);
    g_job = nullptr;
    g_full_report = text ? text->text : nullptr;
    g_report_path[0] = 0;
    g_report_error = text ? text->error : ERROR_SUCCESS;
    g_report_complete = text && text->text && text->complete;
    if (text && text->text) nr_bundle::CopyPath(g_report_path, text->path.c_str());
    nr_bundle::CopyPath(g_feedback_last_zip, result.zip_path);
    nr_bundle::CopyText(g_reason, sizeof(g_reason), reason);
    nr_bundle::CopyText(g_submission, sizeof(g_submission), submission);
    g_pair.ready = true;
    g_pair.complete = archive_requested && complete && result.required_complete && result.zip_path[0];
    const unsigned status = archive_requested ?
        (result.status == nr_bundle::Status::Failed ? 5u : g_pair.complete ? 3u : 4u) :
        (!text || !text->text ? 5u : text->complete ? 3u : 4u);
    g_feedback_status.store(status, std::memory_order_release);
    ReleaseSRWLockExclusive(&g_feedback_lock);
}
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE && value != nullptr) CloseHandle(value); }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
};

bool ReadSummary(const wchar_t *source, std::string &text, bool &coverage, ULONGLONG deadline) {
    coverage = false; text.clear();
    if (!nr_bundle::LocalAbsolutePath(source)) return false;
    wchar_t native[1024] = {};
    if (!nr_bundle::NativePath(native, source)) return false;
    Handle input(CreateFileW(native, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (input.value == INVALID_HANDLE_VALUE || GetFileType(input.value) != FILE_TYPE_DISK ||
        !GetFileInformationByHandle(input.value, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) return false;
    const UINT64 size = (static_cast<UINT64>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    nr_feedback_text::Summary summary;
    char bytes[16384];
    std::string line;
    bool long_line = false, read_ok = true;
    UINT64 scanned = 0;
    const UINT64 target = std::min<UINT64>(size, nr_feedback_text::kScanLimit);
    auto feed = [&](const char *data, DWORD count) {
        for (DWORD i = 0; i < count; ++i) {
            if (data[i] == '\n') { summary.Push(line, long_line); line.clear(); long_line = false; }
            else if (line.size() < nr_feedback_text::kLineLimit) line += data[i];
            else long_line = true;
        }
    };
    while (scanned < target && !g_cancel.load() && GetTickCount64() < deadline) {
        DWORD got = 0;
        const DWORD count = static_cast<DWORD>(std::min<UINT64>(sizeof(bytes), target - scanned));
        if (!ReadFile(input.value, bytes, count, &got, nullptr) || !got) { read_ok = false; break; }
        feed(bytes, got); scanned += got;
    }
    if (!line.empty() || long_line) summary.Push(line, long_line);
    coverage = read_ok && scanned == size;
    // A capped prefix is NOT a complete search for the first bypass. Record
    // that limitation, and still include the latest bounded tail.
    if (!coverage && size > scanned && !g_cancel.load() && GetTickCount64() < deadline) {
        LARGE_INTEGER offset{}; offset.QuadPart = static_cast<LONGLONG>(size > 65536 ? size - 65536 : 0);
        if (SetFilePointerEx(input.value, offset, nullptr, FILE_BEGIN)) {
            summary.tail = {}; line.clear(); long_line = false;
            UINT64 left = size - static_cast<UINT64>(offset.QuadPart);
            bool skip_fragment = offset.QuadPart != 0;
            while (left && !g_cancel.load() && GetTickCount64() < deadline) {
                DWORD got = 0;
                if (!ReadFile(input.value, bytes, static_cast<DWORD>(std::min<UINT64>(left, sizeof(bytes))), &got, nullptr) || !got) break;
                left -= got;
                DWORD start = 0;
                if (skip_fragment) {
                    while (start < got && bytes[start] != '\n') ++start;
                    if (start == got) continue;
                    ++start; skip_fragment = false;
                }
                feed(bytes + start, got - start);
            }
            if (!line.empty() || long_line) summary.Push(line, long_line);
        }
    }
    // Detect replacement/resizing during the read; never claim a coherent full scan.
    BY_HANDLE_FILE_INFORMATION after{};
    if (!GetFileInformationByHandle(input.value, &after) ||
        after.nFileSizeHigh != info.nFileSizeHigh || after.nFileSizeLow != info.nFileSizeLow ||
        after.ftLastWriteTime.dwHighDateTime != info.ftLastWriteTime.dwHighDateTime ||
        after.ftLastWriteTime.dwLowDateTime != info.ftLastWriteTime.dwLowDateTime) coverage = false;
    text = summary.Text(size, scanned, coverage);
    return text.size() <= nr_feedback_text::kSummaryLimit;
}
// F7's paired archive retains its existing temporary-file/ZIP contract.
bool SummaryFile(const wchar_t *source, const wchar_t *output, bool &coverage, ULONGLONG deadline) {
    std::string text;
    if (!nr_bundle::LocalAbsolutePath(output) || !ReadSummary(source, text, coverage, deadline)) return false;
    wchar_t native[1024] = {};
    if (!nr_bundle::NativePath(native, output)) return false;
    Handle out(CreateFileW(native, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    DWORD written = 0;
    return out.value != INVALID_HANDLE_VALUE &&
        WriteFile(out.value, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size();
}
std::wstring Beside(const wchar_t *path, const wchar_t *leaf) {
    if (!path || !*path) return {};
    std::wstring result(path);
    const auto slash = result.find_last_of(L'\\');
    if (slash == std::wstring::npos) return {};
    result.resize(slash + 1); result += leaf;
    return result.size() < 1024 ? result : std::wstring{};
}
std::wstring FindCarrierLog(FeedbackSession &s) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId()));
    if (snapshot.value == INVALID_HANDLE_VALUE) {
        FeedbackAppend(s, "carrierLog.status=module-inventory-unavailable\n"); return {};
    }
    MODULEENTRY32W entry{}; entry.dwSize = sizeof(entry);
    unsigned matches = 0, scanned = 0;
    std::wstring result;
    BOOL more = Module32FirstW(snapshot.value, &entry);
    for (; more && scanned < 2048; more = Module32NextW(snapshot.value, &entry), ++scanned) {
        if (!nr_deployment_names::MatchingCarrier(entry.szModule)) continue;
        ++matches;
        if (nr_bundle::LocalAbsolutePath(entry.szExePath))
            result = Beside(entry.szExePath, L"dlss5-bridge.log");
    }
    const bool complete = !more && GetLastError() == ERROR_NO_MORE_FILES;
    const bool found = complete && matches == 1 && !result.empty();
    FeedbackAppend(s, "carrierLog.status=%s\ncarrierLog.matches=%u\n"
        "carrierLog.scope=optional bounded sanitized log beside the matched loaded companion; file contents are not proof of delivery\n",
        found ? "matched-loaded-path" : !complete ? "module-inventory-incomplete" :
        matches > 1 ? "ambiguous-companion" : matches == 0 ? "matching-companion-not-loaded" : "path-unavailable", matches);
    return found ? result : std::wstring{};
}
void CollectLogs(nr_bundle::Job &job, const CoreSession &session) {
    const std::wstring paths[] = {g_log_path, Beside(g_log_path, L"nr-before-sr.previous.log"),
        Beside(g_log_path, L"ReShade.log"), Beside(session.exe, L"ReShade.log"),
        Beside(session.reshade_module, L"ReShade.log"), session.carrier_log};
    const char *names[] = {"logs/nr-current-summary.txt", "logs/nr-previous-summary.txt",
        "logs/reshade-addon-summary.txt", "logs/reshade-game-summary.txt", "logs/reshade-module-summary.txt",
        "logs/carrier-summary.txt"};
    const ULONGLONG deadline = GetTickCount64() + 3000;
    for (unsigned i = 0; i < 6; ++i) {
        bool duplicate = false;
        for (unsigned j = 0; j < i; ++j)
            if (!paths[i].empty() && _wcsicmp(paths[i].c_str(), paths[j].c_str()) == 0) duplicate = true;
        if (duplicate) continue;
        wchar_t output[1024] = {};
        bool coverage = false;
        if (g_cancel.load() || swprintf_s(output, L"%ls\\summary-%u.txt", session.directory, i) < 0 ||
            paths[i].empty() || !SummaryFile(paths[i].c_str(), output, coverage, deadline)) {
            nr_bundle::AddMissing(job, names[i], g_cancel.load() ? "cancelled" : "summary-source-or-write-unavailable", i == 0);
            continue;
        }
        nr_bundle::AddFile(job, output, names[i], nr_bundle::FileKind::Copy, i == 0, nr_feedback_text::kSummaryLimit);
        if (!coverage) {
            const std::string name = "coverage/" + std::to_string(i);
            nr_bundle::AddMissing(job, name.c_str(), "bounded scan incomplete; first-event coverage not guaranteed; see prefix and tail", i == 0);
        }
    }
}

// F8 does not create a Job, a ZIP or summary scratch files. Build from the same
// bounded sanitizer/summary reader used by F7 and persist exactly one text file.
nr_feedback_report::Output CollectSingleText(const CoreSession &session, bool capture_complete) {
    nr_feedback_report::Output output;
    nr_feedback_single::Document doc;
    doc.complete = capture_complete;
    doc.Append("\n===== CURRENT CAPTURE METADATA / 本次采集 =====\n");
    std::string metadata = nr_feedback_report::SafeLines(session.activity.report);
    if (metadata.compare(0, 7, "format=") == 0) metadata.replace(0, 7, "metadata.format=");
    doc.Append(metadata);
    const std::wstring paths[] = {g_log_path, Beside(g_log_path, L"nr-before-sr.previous.log"),
        Beside(g_log_path, L"ReShade.log"), Beside(session.exe, L"ReShade.log"),
        Beside(session.reshade_module, L"ReShade.log"), session.carrier_log};
    const char *names[] = {"nr-current", "nr-previous", "reshade-addon", "reshade-game", "reshade-module", "carrier"};
    const ULONGLONG deadline = GetTickCount64() + 3000;
    for (unsigned i = 0; i < 6; ++i) {
        bool duplicate = false;
        for (unsigned j = 0; j < i; ++j)
            if (!paths[i].empty() && _wcsicmp(paths[i].c_str(), paths[j].c_str()) == 0) duplicate = true;
        if (duplicate) continue;
        bool coverage = false;
        std::string summary;
        if (g_cancel.load() || paths[i].empty() || !ReadSummary(paths[i].c_str(), summary, coverage, deadline)) {
            doc.Missing(names[i], g_cancel.load() ? "cancelled" : "source-or-read-unavailable", i == 0);
            continue;
        }
        doc.Append(std::string("\n===== ") + names[i] +
            (i == 1 ? " / PREVIOUS PROCESS, NOT CURRENT =====\n" : " / CURRENT PROCESS (head/first/tail) =====\n"));
        if (!coverage) doc.Missing(std::string(names[i]) + ".coverage", "bounded or changing source; first-event completeness not guaranteed", i == 0);
        nr_bundle::Sha256 digest;
        digest.Update(reinterpret_cast<const unsigned char *>(summary.data()), summary.size());
        char hash[65]{}; digest.Finish(hash);
        doc.Append(std::string("summary.bytes=") + std::to_string(summary.size()) + "\nsummary.sha256=" + hash + "\n");
        doc.Append(summary);
    }
    if (g_cancel.load()) { doc.complete = false; doc.Append("collection.cancelled=1\n"); }
    doc.Finish();
    output.complete = doc.complete;
    char exe[1024]{};
    const wchar_t *leaf = wcsrchr(session.exe, L'\\');
    WideToUtf8(leaf ? leaf + 1 : session.exe, exe, sizeof(exe));
    const auto &t = session.local_time;
    const std::string name = nr_feedback_single::Filename(exe, kBuildIdentity,
        t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
        GetCurrentProcessId(), session.serial);
    const std::wstring filename(name.begin(), name.end());
    auto cached = std::make_shared<const std::string>(std::move(doc.text));
    if (nr_feedback_report::WriteAtomic(session.directory, *cached, output, filename.c_str())) output.text = std::move(cached);
    return output;
}

#include "nr_feedback_modules.inl"

void Modules(FeedbackSession &s) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId()));
    MODULEENTRY32W entry{}; entry.dwSize = sizeof(entry);
    unsigned count = 0;
    if (snapshot.value == INVALID_HANDLE_VALUE) { FeedbackAppend(s, "modules.status=unavailable\n"); return; }
    for (BOOL more = Module32FirstW(snapshot.value, &entry); more; more = Module32NextW(snapshot.value, &entry)) {
        if (!FeedbackModuleRelevant(entry.szModule)) continue;
        if (count == 32) { FeedbackAppend(s, "modules.truncated=1\n"); break; }
        char name[1024] = {}; WideToUtf8(entry.szModule, name, sizeof(name));
        const auto safe = nr_feedback_text::Sanitize(name);
        FeedbackAppend(s, "module.%u=%s\n", count++, safe.c_str());
    }
    FeedbackAppend(s, "modules.contract=name inventory plus bounded loaded-file fingerprints; no module bytes or process addresses exported\n");
}
DWORD WINAPI Worker(void *opaque) {
    auto *session = static_cast<CoreSession *>(opaque);
    auto &s = session->activity;
    nr_bundle::Result result{};
    nr_bundle::Job *job = nullptr;
    bool complete = false;
    nr_feedback_report::Output text;
    try {
        // Bounded CPU observation interval, independent of Present/NR/GPU.
        for (unsigned i = 0; i < 15 && !g_cancel.load(); ++i) Sleep(50);
        AcquireSRWLockExclusive(&g_feedback_lock);
        if (g_feedback_session == &s) g_feedback_session = nullptr;
        g_feedback_status.store(2, std::memory_order_release);
        ReleaseSRWLockExclusive(&g_feedback_lock);
        FeedbackAppendActivity(s, "end");
#if defined(NR_HAS_COMMAND_CREATION_EVIDENCE)
        FeedbackAppendCommandHistory(s);
#endif
        FeedbackAppend(s, "collection.elapsedMs=%llu\ncollection.parameterSamples=%u\ncollection.cancelled=%d\n",
            GetTickCount64() - s.started, s.observations, g_cancel.load() ? 1 : 0);
        const auto &identity = EnsureRuntimeIdentityOnWorker();
        FeedbackAppend(s, "artifact.sourceCommit=%s\nartifact.sourceDirty=%d\nartifact.addonSha256=%s\nartifact.identityStatus=%s\n",
            kSourceCommit, kSourceDirty ? 1 : 0, identity.sha256, identity.status);
        Modules(s);
        session->carrier_log = FindCarrierLog(s);
        FingerprintRuntimeFiles(s, *session);
        const auto color = SnapshotColorPolicy();
        FeedbackAppend(s, "color.status=%s\ncolor.requested=%.9g\ncolor.effective=%.9g\ncolor.reason=%s\n"
            "color.contract=last recorded blend; not a measured picture strength\ncollection.metadataTruncated=%d\n",
            NrColorPolicyStatusName(color.status), color.policy.requested_color_strength,
            color.policy.effective_color_strength, NrColorReasonName(color.policy.color_reason), s.report_truncated ? 1 : 0);
        complete = !g_cancel.load() && session->snapshot_ok && !s.report_truncated;
        if (FeedbackCreateDirectory(session->directory)) {
            if (session->archive_requested) job = nr_bundle::CreateJob(session->directory, s.report);
            else text = CollectSingleText(*session, complete);
        } else {
            text.error = GetLastError();
            if (!text.error) text.error = ERROR_PATH_NOT_FOUND;
        }
        if (job) {
            job->archive_limit = nr_feedback_text::kCoreZipLimit;
            AcquireSRWLockExclusive(&g_feedback_lock); g_job = job; ReleaseSRWLockExclusive(&g_feedback_lock);
            CollectLogs(*job, *session);
            if (!session->snapshot_ok) nr_bundle::AddMissing(*job, "runtime/snapshot", "chain snapshot busy; no lock wait or invented parameters", true);
            if (s.report_truncated) nr_bundle::AddMissing(*job, "runtime/metadata", "bounded metadata truncated", true);
            if (g_cancel.load()) {
                nr_bundle::AddMissing(*job, "collection/cancelled", "user cancelled; partial core evidence retained", true);
                nr_bundle::RequestCancel(*job);
            }
            result = nr_bundle::Run(*job);
            complete = !g_cancel.load() && session->snapshot_ok && !s.report_truncated;
            text = nr_feedback_report::Build(*job, result, complete);
        }
    } catch (...) {
        text.error = ERROR_NOT_ENOUGH_MEMORY;
        // Never let a failed diagnostic allocation unwind into the game.
        AcquireSRWLockExclusive(&g_feedback_lock);
        if (g_feedback_session == &s) g_feedback_session = nullptr;
        ReleaseSRWLockExclusive(&g_feedback_lock);
    }
    Publish(result, complete, g_cancel.load() ? "cancelled; inspect partial evidence" :
        !session->archive_requested ? (text.text ? "single TXT saved; missing evidence is explicit; no ZIP or images requested" : "TXT save failed; no old report reused") :
        result.status == nr_bundle::Status::Failed ? "core package failed; no previous ZIP reused" : "paired F7 core collected; no image readback requested", s.report, &text, session->archive_requested);
    nr_bundle::DestroyJob(job);
    delete session;
    return 0;
}
void Present(reshade::api::effect_runtime *runtime) {
    if (!runtime || !g_feedback_requested.exchange(false, std::memory_order_acq_rel)) return;
    HMODULE pinned = nullptr;
    auto *session = new (std::nothrow) CoreSession{};
    if (!session || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&Worker), &pinned)) {
        delete session; Publish({}, false, "core worker allocation or module pin failed"); return;
    }
    try {
    auto &s = session->activity;
    s.started = GetTickCount64();
    const auto pair = SnapshotPair();
    session->archive_requested = pair.archive_requested;
    session->serial = g_serial.load();
    GetLocalTime(&session->local_time); GetSystemTime(&session->utc_time);
    nr_bundle::CopyText(session->session, sizeof(session->session), pair.session);
    ChainConfig config{}; NrDiagnosticIdentity applied{}; unsigned generation = 0;
    if (TryEnterCriticalSection(&g_chain_cs)) {
        config = g_config; applied = g_applied_nr_identity; generation = g_config_generation.load();
        session->snapshot_ok = true; LeaveCriticalSection(&g_chain_cs);
    }
    char gpu[256] = {}; std::uint32_t driver = 0, vendor = 0, width = 0, height = 0;
    auto *device = runtime->get_device();
    const bool gpu_known = device && device->get_property(reshade::api::device_properties::description, gpu);
    const bool driver_known = device && device->get_property(reshade::api::device_properties::driver_version, &driver);
    if (device) device->get_property(reshade::api::device_properties::vendor_id, &vendor);
    runtime->get_screenshot_width_and_height(&width, &height); // dimensions only; never save_screenshot
    if (GetModuleFileNameW(nullptr, session->exe, 1024) >= 1024) session->exe[0] = 0;
    if (GetModuleFileNameW(reshade::internal::get_reshade_module_handle(), session->reshade_module, 1024) >= 1024) session->reshade_module[0] = 0;
    const wchar_t *exe_leaf = wcsrchr(session->exe, L'\\');
    char exe[1024] = {}; WideToUtf8(exe_leaf ? exe_leaf + 1 : session->exe, exe, sizeof(exe));
    const auto safe_exe = nr_feedback_text::Sanitize(exe);
    const auto safe_gpu = nr_feedback_text::Sanitize(gpu_known ? gpu : "unknown");
    FeedbackAppend(s, "format=DLSS5-Core-v1\ncapture.kind=core\nsession=%s\nbuild=%s\nlanguage=%s\n"
        "exe.leaf=%s\ngpu=%s\ndriver.raw=%u\ndriver.known=%d\nvendor=%u\napi=%u\n"
        "display.extent=%ux%u\ndisplay.storageFormat=%u\ndisplay.colorSpace=%u\n"
        "snapshot.valid=%d\nrequested.config=%u\nrequested.enabled=%d\nrequested.mode=%d\n"
        "requested.blend=%.9g\nrequested.color=%.9g\nrequested.workMode=%d\nrequested.workScale=%.9g\n"
        "requested.lightingLock=%.9g\nrequested.detailStability=%.9g\nrequested.edgeGuard=%.9g\n"
        "requested.intensity=%.9g\nrequested.localTone=%.9g\nrequested.localStructure=%.9g\nrequested.skin=%.9g\n"
        "requested.faceMask=%d\nrequested.uiCorrection=%d\nrequested.jitterMode=%d\nrequested.style=%u\n"
        "applied.instance=%llu\napplied.frame=%llu\napplied.config=%u\napplied.intensity=%.9g\napplied.skin=%.9g\n"
        "capture.automaticSettingChanges=none\nconfig.writesToIni=0\nimages.requested=0\ngpu.readbackRequested=0\n"
        "fg.status=unavailable; this collector does not intercept FG\n"
        "applied.contract=historical successful NR; compare config generation and start/end counters\n"
        "core.zipLimitBytes=%llu\ntimeline.columns=tag,elapsedMs,SRcarrier,featureGeneration,reset,jitterX,jitterY,mvScaleX,mvScaleY,preExposure,exposureScale,previousBypass\n",
        session->session, kBuildIdentity, kChineseUi ? "zh-CN" : "en", safe_exe.c_str(), safe_gpu.c_str(),
        driver, driver_known, vendor, device ? static_cast<unsigned>(device->get_api()) : 0, width, height,
        g_feedback_swap_format.load(), g_feedback_swap_color_space.load(), session->snapshot_ok, generation,
        config.enabled, config.mode, ActiveTransferStrength(config), config.color_strength, config.work_mode,
        config.custom_work_scale, config.lighting_lock, config.detail_stability, config.edge_guard,
        config.intensity, config.local_tone, config.local_structure, config.skin_structure, config.auto_mask,
        config.ui_correction, static_cast<int>(config.jitter_mode), config.style,
        applied.instance_id, applied.frame_id, applied.config_generation, applied.tuning.intensity,
        applied.tuning.skin_structure, static_cast<unsigned long long>(nr_feedback_text::kCoreZipLimit));
    const auto &local = session->local_time;
    const auto &utc = session->utc_time;
    FeedbackAppend(s, "capture.delivery=%s\ncapture.localTime=%04u-%02u-%02uT%02u:%02u:%02u.%03u\n"
        "capture.utcTime=%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\n",
        session->archive_requested ? "f7-paired-archive" : "single-txt",
        local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond, local.wMilliseconds,
        utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond, utc.wMilliseconds);
    FeedbackAppendActivity(s, "start");
    AcquireSRWLockExclusive(&g_feedback_lock);
    g_pair.config = generation; g_feedback_session = &s;
    ReleaseSRWLockExclusive(&g_feedback_lock);
    if (!QueueUserWorkItem(Worker, session, WT_EXECUTEDEFAULT)) {
        AcquireSRWLockExclusive(&g_feedback_lock); g_feedback_session = nullptr; ReleaseSRWLockExclusive(&g_feedback_lock);
        delete session; Publish({}, false, "core worker queue failed; no previous ZIP reused");
    }
    } catch (...) {
        AcquireSRWLockExclusive(&g_feedback_lock);
        if (g_feedback_session == &session->activity) g_feedback_session = nullptr;
        ReleaseSRWLockExclusive(&g_feedback_lock);
        delete session; Publish({}, false, "core snapshot allocation failed; no game state changed");
    }
}
} // namespace nr_core_feedback
