// Installation health is a small asynchronous local snapshot. No game directory
// writes, disk reads or JSON encoding occur inside the rendering/chain lock.
#include "nr_runtime_status.h"
std::atomic<bool> g_runtime_status_worker{false};
struct RuntimeStatusJob {
    nr_runtime_status::Sample sample;
    bool active = false;
    BypassReason reason = BypassReason::None;
    FILETIME sampled_at = {};
};

void RuntimeStatusTime(FILETIME time, char (&text)[32]) {
    SYSTEMTIME utc = {};
    FileTimeToSystemTime(&time, &utc);
    std::snprintf(text, sizeof(text), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond, utc.wMilliseconds);
}

struct RuntimeStatusWorkspace {
    wchar_t root[32768] = {}, path[32768] = {}, temporary[32768] = {}, exe[32768] = {};
    char escaped[196608] = {}, addon_escaped[196608] = {}, json[405504] = {};
};

bool RuntimeStatusEscape(const wchar_t *path, char *out, std::size_t capacity) {
    std::size_t used = 0;
    for (std::size_t i = 0; path[i] != 0; ++i) {
        const unsigned ch = static_cast<unsigned short>(path[i]);
        const bool escape = ch < 0x20 || ch >= 0x7f || ch == '\\' || ch == '"';
        const std::size_t count = escape ? 6 : 1;
        if (used + count >= capacity) { out[0] = 0; return false; }
        if (escape) std::snprintf(out + used, capacity - used, "\\u%04x", ch);
        else out[used] = static_cast<char>(ch);
        used += count;
    }
    out[used] = 0;
    return true;
}

DWORD WINAPI RuntimeStatusWorker(void *opaque) {
    auto *job = static_cast<RuntimeStatusJob *>(opaque);
    auto *workspace = new (std::nothrow) RuntimeStatusWorkspace{};
    if (workspace == nullptr) {
        delete job;
        g_runtime_status_worker.store(false, std::memory_order_release);
        return 0;
    }
    auto &root = workspace->root, &path = workspace->path;
    auto &temporary = workspace->temporary, &exe = workspace->exe;
    auto &escaped = workspace->escaped, &addon_escaped = workspace->addon_escaped;
    auto &json = workspace->json;
    const auto &identity = EnsureRuntimeIdentityOnWorker();
#if defined(NR_HAS_NETWORK_MODEL_SELECTION)
    EnsureNrNetworkIdentityOnWorker();
    const auto network_runtime = NrNetworkRuntimeSnapshot();
#endif
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", root, static_cast<DWORD>(std::size(root)));
    bool success = false;
    if (length != 0 && length + 100 < std::size(root)) {
        wcscat_s(root, L"\\DLSS5InstallAssistant");
        CreateDirectoryW(root, nullptr);
        wcscat_s(root, L"\\runtime");
        CreateDirectoryW(root, nullptr);
        swprintf_s(path, L"%ls\\%lu.json", root, GetCurrentProcessId());
        swprintf_s(temporary, L"%ls.tmp", path);
        const DWORD exe_length = GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
        FILETIME started = {}, exited = {}, kernel = {}, user = {};
        if (exe_length != 0 && exe_length < std::size(exe) &&
            GetProcessTimes(GetCurrentProcess(), &started, &exited, &kernel, &user)) {
            // Escape every Windows path code point that could affect JSON.
            // UTF-16 escapes preserve non-ASCII and surrogate pairs losslessly.
            const bool paths_encoded = RuntimeStatusEscape(exe, escaped, sizeof(escaped)) &&
                RuntimeStatusEscape(identity.path, addon_escaped, sizeof(addon_escaped));
            char start_text[32] = {}, now_text[32] = {};
            RuntimeStatusTime(started, start_text);
            RuntimeStatusTime(job->sampled_at, now_text);
            const char *state = !job->sample.enabled ? "disabled" :
                (job->active ? "active" : (job->sample.bypassed ? "bypassed" : "waiting"));
            const int bytes = std::snprintf(json, sizeof(json),
                "{\"schema\":\"dlss5-runtime-status/v2\",\"processId\":%lu,"
                "\"processStartedAt\":\"%s\",\"updatedAt\":\"%s\",\"executable\":\"%s\","
                "\"addonPath\":\"%s\",\"addonSha256\":\"%s\",\"addonIdentityStatus\":\"%s\","
                "\"sourceCommit\":\"%s\",\"sourceDirty\":%s,\"artifactId\":\"%s%s\","
                "\"buildId\":\"%s\",\"state\":\"%s\",\"active\":%s,\"bypassReason\":\"%s\","
                "\"nativeSrObserved\":%s,\"nrEvaluations\":%llu,\"transferSubmitted\":%llu,"
                "\"configGeneration\":%u,\"carrierEpoch\":%llu"
#if defined(NR_HAS_NETWORK_MODEL_SELECTION)
                ",\"nrRuntimeGeneration\":%llu,\"nrRuntimeModule\":%llu,\"nrRuntimeFileSha256\":\"%s\","
                "\"nrRuntimeIdentityStatus\":\"%s\",\"nrNetworkFamily\":\"%s\",\"nrNetworkEvidence\":\"%s\","
                "\"nrExecutedNetworkVerified\":false"
#endif
                "}",
                GetCurrentProcessId(), start_text, now_text, escaped,
                addon_escaped, identity.sha256, identity.status,
                kSourceCommit, kSourceDirty ? "true" : "false",
                identity.sha256[0] != 0 ? "sha256:" : "", identity.sha256,
                kBuildIdentity, state,
                job->active ? "true" : "false", BypassReasonName(job->reason),
                job->sample.carriers != 0 ? "true" : "false",
                static_cast<unsigned long long>(job->sample.nr_evaluations),
                static_cast<unsigned long long>(job->sample.transfers), job->sample.config,
                static_cast<unsigned long long>(job->sample.epoch)
#if defined(NR_HAS_NETWORK_MODEL_SELECTION)
                , network_runtime.generation, network_runtime.module_address, network_runtime.sha256,
                network_runtime.identity_status, nr_network_policy::FamilyName(network_runtime.family),
                nr_network_policy::EvidenceName(network_runtime.evidence)
#endif
                );
            HANDLE file = CreateFileW(temporary, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                DWORD written = 0;
                success = paths_encoded && bytes > 0 && static_cast<std::size_t>(bytes) < sizeof(json) &&
                    WriteFile(file, json, static_cast<DWORD>(bytes), &written, nullptr) &&
                    written == static_cast<DWORD>(bytes) && FlushFileBuffers(file);
                CloseHandle(file);
                success = success && MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
                if (!success) DeleteFileW(temporary);
            }
        }
    }
    if (!success) {
        static std::atomic<bool> reported{false};
        if (!reported.exchange(true)) Log("[installer-status] local status snapshot unavailable; rendering is unaffected.");
    }
    delete workspace;
    delete job;
    g_runtime_status_worker.store(false, std::memory_order_release);
    return 0;
}

void QueueRuntimeStatus() {
    static SRWLOCK lock = SRWLOCK_INIT;
    static ULONGLONG last = 0;
    static nr_runtime_status::Sample previous = {};
    if (!TryAcquireSRWLockExclusive(&lock)) return;
    const ULONGLONG now = GetTickCount64();
    if (now - last < 2000 || g_runtime_status_worker.load(std::memory_order_acquire)) {
        ReleaseSRWLockExclusive(&lock); return;
    }
    last = now;
    RuntimeStatusJob *job = new (std::nothrow) RuntimeStatusJob{};
    if (job != nullptr) {
        auto &s = job->sample;
        const auto config_before = g_config_generation.load(std::memory_order_acquire);
        s.epoch = g_pipeline_stats_epoch.load(std::memory_order_acquire);
        s.enabled = SnapshotConfig().enabled;
        s.config = g_config_generation.load(std::memory_order_acquire);
        s.carriers = g_pipeline_eligible_carriers.load(std::memory_order_acquire);
        s.nr_evaluations = g_pipeline_feature18_success.load(std::memory_order_acquire);
        s.transfers = g_pipeline_transfer_submitted.load(std::memory_order_acquire);
        job->reason = static_cast<BypassReason>(g_last_bypass_reason.load(std::memory_order_acquire));
        s.bypassed = job->reason != BypassReason::None;
        s.coherent = config_before == s.config &&
            s.config == g_config_generation.load(std::memory_order_acquire) &&
            s.epoch == g_pipeline_stats_epoch.load(std::memory_order_acquire);
        job->active = nr_runtime_status::Active(previous, s);
        previous = s;
        GetSystemTimeAsFileTime(&job->sampled_at);
        HMODULE pinned = nullptr;
        g_runtime_status_worker.store(true, std::memory_order_release);
        HANDLE thread = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&RuntimeStatusWorker), &pinned))
            thread = CreateThread(nullptr, 0, RuntimeStatusWorker, job, 0, nullptr);
        if (thread != nullptr) CloseHandle(thread);
        else { delete job; g_runtime_status_worker.store(false, std::memory_order_release); }
    }
    ReleaseSRWLockExclusive(&lock);
}
