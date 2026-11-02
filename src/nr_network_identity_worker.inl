// Runs only from the existing pinned status/F8 workers. It hashes the disk
// file at the loaded module path, not the relocated image and not a candidate
// DLL. No new worker, DLL load, synchronous render-path hash, or GPU work.
std::atomic<bool> g_nr_network_identity_measuring{false};
const NrNetworkFileIdentity& EnsureNrNetworkIdentityOnWorker() {
    static const NrNetworkFileIdentity pending{};
    const auto* loaded = g_nr_network_loaded.load(std::memory_order_acquire);
    if (!loaded) return pending; // Do not consume a once-flag before NR loads.
    const auto* cached = g_nr_network_file_identity.load(std::memory_order_acquire);
    if (cached && cached->generation == loaded->generation && cached->module_address == loaded->module_address)
        return *cached;
    bool expected = false;
    if (!g_nr_network_identity_measuring.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return pending;
    struct MeasuringGuard {
        ~MeasuringGuard() { g_nr_network_identity_measuring.store(false, std::memory_order_release); }
    } measuring_guard;
    cached = g_nr_network_file_identity.load(std::memory_order_acquire);
    if (cached && cached->generation == loaded->generation && cached->module_address == loaded->module_address)
        return *cached;
    auto* identity = new (std::nothrow) NrNetworkFileIdentity{};
    if (!identity) return pending;
    identity->generation = loaded->generation;
    identity->module_address = loaded->module_address;
    identity->status = "path-unavailable";
    const auto module = reinterpret_cast<HMODULE>(static_cast<std::uintptr_t>(loaded->module_address));
    const DWORD count = GetModuleFileNameW(module, identity->path, static_cast<DWORD>(std::size(identity->path)));
    if (count && count < std::size(identity->path)) {
        WideToUtf8(identity->path, identity->path_utf8, sizeof(identity->path_utf8));
        identity->status = "hash-unavailable";
        // The owner retains the loaded module. Restrict sharing during this
        // one measurement; replacing a runtime remains a restart-only action.
        HANDLE file = CreateFileW(identity->path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER size{};
            auto* buffer = new (std::nothrow) unsigned char[64 * 1024];
            bool ok = buffer && GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= (1ll << 30);
            if (ok) identity->file_bytes = static_cast<unsigned long long>(size.QuadPart);
            nr_bundle::Sha256 sha{};
            unsigned long long total = 0;
            while (ok && total < identity->file_bytes) {
                DWORD bytes = 0;
                ok = ReadFile(file, buffer, static_cast<DWORD>(std::min<unsigned long long>(64 * 1024,
                    identity->file_bytes - total)), &bytes, nullptr) && bytes != 0;
                if (ok) { sha.Update(buffer, bytes); total += bytes; }
            }
            if (ok) { sha.Finish(identity->sha256); identity->status = "loaded-path-file-hashed"; }
            delete[] buffer;
            CloseHandle(file);
        }
    } else {
        identity->path[0] = 0;
    }
    g_nr_network_file_identity.store(identity, std::memory_order_release);
    Log("[nr-network-identity] runtimeGeneration=%llu module=%llu status=%s fileBytes=%llu sha256=%s "
        "measurement=loaded-path-file-once-worker; not a relocated-memory hash, Authenticode check, or executed-network receipt.",
        identity->generation, identity->module_address, identity->status, identity->file_bytes, identity->sha256);
    return *identity;
}
