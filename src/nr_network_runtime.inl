// Included beside EnsureNrApi. The loaded module is retained by the existing
// runtime owner for the process lifetime; this does not add DLL hot swapping.
#define NR_HAS_NETWORK_MODEL_SELECTION 1
struct NrNetworkRuntimeMetadata {
    unsigned long long generation = 0;
    unsigned long long module_address = 0;
    nr_network_policy::Version version{};
    nr_network_policy::Family family = nr_network_policy::Family::Unknown;
    nr_network_policy::Evidence evidence = nr_network_policy::Evidence::Unknown;
    char sha256[65]{};
    const char* identity_status = "not-loaded";
};
struct NrNetworkFileIdentity {
    unsigned long long generation = 0;
    unsigned long long module_address = 0;
    unsigned long long file_bytes = 0;
    wchar_t path[32768]{};
    char path_utf8[131072]{};
    char sha256[65]{};
    const char* status = "pending";
};
// Published objects are immutable. Retaining the few per-module receipts also
// avoids a reader holding a dangling pointer during diagnostics/teardown.
std::atomic<const NrNetworkRuntimeMetadata*> g_nr_network_loaded{nullptr};
std::atomic<const NrNetworkFileIdentity*> g_nr_network_file_identity{nullptr};

NrNetworkRuntimeMetadata NrNetworkRuntimeSnapshot() noexcept {
    const auto* loaded = g_nr_network_loaded.load(std::memory_order_acquire);
    if (!loaded) return {};
    auto result = *loaded;
    const auto* file = g_nr_network_file_identity.load(std::memory_order_acquire);
    if (file && file->generation == result.generation && file->module_address == result.module_address) {
        result.identity_status = file->status;
        std::memcpy(result.sha256, file->sha256, sizeof(result.sha256));
        const auto known = nr_network_policy::KnownFileFamily(file->sha256);
        if (!nr_network_policy::FileVersionConsistent(known, result.version)) {
            result.identity_status = "loaded-version-file-hash-conflict";
            result.family = nr_network_policy::Family::Unknown;
            result.evidence = nr_network_policy::Evidence::Unknown;
        } else if (known != nr_network_policy::Family::Unknown) {
            result.family = known;
            result.evidence = nr_network_policy::Evidence::LoadedPathFileHash;
        } else if (file->sha256[0]) {
            // A modified/unrecognized file supersedes a version-string hint.
            // Its resource set and fallback semantics remain unknown.
            result.family = nr_network_policy::Family::Unknown;
            result.evidence = nr_network_policy::Evidence::Unknown;
        }
    }
    return result;
}
struct NrNetworkCreationIdentity {
    unsigned long long generation = 0;
    unsigned long long module_address = 0;
};
NrNetworkCreationIdentity CurrentNrNetworkCreationIdentity() noexcept {
    const auto* loaded = g_nr_network_loaded.load(std::memory_order_acquire);
    return loaded ? NrNetworkCreationIdentity{loaded->generation, loaded->module_address} : NrNetworkCreationIdentity{};
}
nr_network_policy::Assessment NrNetworkRequestedStatus(int requested) noexcept {
    const auto runtime = NrNetworkRuntimeSnapshot();
    return nr_network_policy::Assess(requested, runtime.family, runtime.evidence);
}

// Caller holds g_nr_api_lock. Only the first successful load (or a genuinely
// different owner in a future implementation) publishes a new generation.
// Version metadata is bounded and already mapped; hashing stays on a worker.
bool RecordNrNetworkRuntimeLoaded(HMODULE module) {
    if (!module) return false;
    const auto address = static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(module));
    const auto* prior = g_nr_network_loaded.load(std::memory_order_acquire);
    if (prior && prior->module_address == address) return true;
    if (prior && prior->generation == UINT64_MAX) return false;
    auto* metadata = new (std::nothrow) NrNetworkRuntimeMetadata{};
    if (!metadata) return false;
    metadata->generation = prior ? prior->generation + 1 : 1;
    metadata->module_address = address;
    metadata->identity_status = "hash-pending-worker";
    const auto resource = FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(16)); // RT_VERSION
    if (resource) {
        const DWORD size = SizeofResource(module, resource);
        const auto loaded = LoadResource(module, resource);
        const auto* bytes = loaded ? static_cast<const unsigned char*>(LockResource(loaded)) : nullptr;
        metadata->version = nr_network_policy::ReadVersionResource(bytes, size);
    }
    metadata->family = nr_network_policy::VersionHint(metadata->version);
    if (metadata->family != nr_network_policy::Family::Unknown)
        metadata->evidence = nr_network_policy::Evidence::LoadedVersionHint;
    g_nr_network_loaded.store(metadata, std::memory_order_release);
    Log("[nr-network-runtime] module=%llu generation=%llu versionKnown=%d version=%u.%u.%u.%u "
        "familyHint=%s evidence=%s sha256=pending-worker; version is not model adoption proof; DLL changes require game restart.",
        metadata->module_address, metadata->generation, metadata->version.known ? 1 : 0,
        metadata->version.value[0], metadata->version.value[1], metadata->version.value[2], metadata->version.value[3],
        nr_network_policy::FamilyName(metadata->family), nr_network_policy::EvidenceName(metadata->evidence));
    return true;
}

bool ValidateNrActiveNetworkRequests() {
    const auto runtime = NrNetworkRuntimeSnapshot();
    const unsigned active = nr_layers::Active(g_config.nr_layers);
    for (unsigned layer = 0; layer < active; ++layer) {
        const int requested = nr_layers::RequestedNetwork(g_config, layer);
        const auto status = nr_network_policy::Assess(requested, runtime.family, runtime.evidence);
        if (status.valid && !status.unavailable) continue;
        SetBypassReason(status.valid ? BypassReason::NetworkUnavailable : BypassReason::NetworkInvalidRequest);
        // Bounded transition diagnostics. Config and loaded-runtime identity
        // are independent, so learning a known file hash does not hide a new refusal.
        static unsigned last_config = UINT_MAX;
        static unsigned long long last_runtime = UINT64_MAX;
        static int last_request = INT_MIN;
        static unsigned last_layer = UINT_MAX;
        const unsigned config = g_config_generation.load(std::memory_order_relaxed);
        if (last_config != config || last_runtime != runtime.generation ||
            last_request != requested || last_layer != layer) {
            last_config = config; last_runtime = runtime.generation;
            last_request = requested; last_layer = layer;
            Log("[nr-network-rejected] layer=%u request=%d reason=%s runtimeGeneration=%llu evidence=%s "
                "sha256=%s; saved selection retained, native output retained. Select an available network; save/report F8 if unexpected.",
                layer + 1, requested, status.valid ? "known-runtime-missing-network" : "invalid-saved-network",
                runtime.generation, nr_network_policy::EvidenceName(runtime.evidence), runtime.sha256);
        }
        return false;
    }
    return true;
}
