// Immutable once-per-process identity, measured only on a pinned worker.
// This is the file hash at the loaded module path, not a relocated-memory hash.
struct RuntimeModuleIdentity {
    wchar_t path[32768] = {};
    char path_utf8[131072] = {};
    char sha256[65] = {};
    const char *status = "pending";
};
std::atomic<RuntimeModuleIdentity *> g_runtime_module_identity{nullptr};

const RuntimeModuleIdentity &RuntimeIdentitySnapshot() {
    static const RuntimeModuleIdentity pending = {};
    const auto *identity = g_runtime_module_identity.load(std::memory_order_acquire);
    return identity != nullptr ? *identity : pending;
}

BOOL CALLBACK InitializeRuntimeIdentity(PINIT_ONCE, PVOID, PVOID *) {
    auto *identity = new (std::nothrow) RuntimeModuleIdentity{};
    if (identity == nullptr) return TRUE;
    identity->status = "path-unavailable";
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&InitializeRuntimeIdentity), &module)) {
        const DWORD count = GetModuleFileNameW(module, identity->path,
            static_cast<DWORD>(std::size(identity->path)));
        if (count != 0 && count < std::size(identity->path)) {
            WideToUtf8(identity->path, identity->path_utf8, sizeof(identity->path_utf8));
            identity->status = "hash-unavailable";
            // Deny writes/deletes during measurement. Installation while the
            // game is running is independently rejected by the installer.
            HANDLE file = CreateFileW(identity->path, GENERIC_READ, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                LARGE_INTEGER size = {};
                auto *buffer = new (std::nothrow) unsigned char[64 * 1024];
                bool ok = buffer != nullptr && GetFileSizeEx(file, &size) &&
                    size.QuadPart > 0 && size.QuadPart <= 64ll * 1024 * 1024;
                nr_bundle::Sha256 sha = {};
                unsigned long long total = 0;
                while (ok && total < static_cast<unsigned long long>(size.QuadPart)) {
                    DWORD bytes = 0;
                    ok = ReadFile(file, buffer, static_cast<DWORD>(std::min<unsigned long long>(
                        64 * 1024, size.QuadPart - total)), &bytes, nullptr) && bytes != 0;
                    if (ok) { sha.Update(buffer, bytes); total += bytes; }
                }
                if (ok) { sha.Finish(identity->sha256); identity->status = "verified"; }
                delete[] buffer;
                CloseHandle(file);
            }
        } else { identity->path[0] = 0; }
    }
    g_runtime_module_identity.store(identity, std::memory_order_release);
    Log("[artifact-identity] sourceCommit=%s sourceDirty=%d status=%s "
        "addonPath=%s addonSha256=%s measurement=loaded-path-file-once-worker.",
        kSourceCommit, kSourceDirty ? 1 : 0, identity->status,
        identity->path_utf8, identity->sha256);
    return TRUE;
}

const RuntimeModuleIdentity &EnsureRuntimeIdentityOnWorker() {
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    InitOnceExecuteOnce(&once, InitializeRuntimeIdentity, nullptr, nullptr);
    return RuntimeIdentitySnapshot();
}
