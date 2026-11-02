// Cached observations only: this may run on the F8 start path. The existing
// worker computes the loaded-path file identity before the end snapshot.
void FeedbackAppendNrNetwork(FeedbackSession& s, const char* prefix) {
    const auto runtime = NrNetworkRuntimeSnapshot();
    const auto* file = g_nr_network_file_identity.load(std::memory_order_acquire);
    const bool file_matches = file && file->generation == runtime.generation &&
        file->module_address == runtime.module_address;
    FeedbackAppend(s, "%s.network.runtimeGeneration=%llu\n%s.network.runtimeModule=%llu\n"
        "%s.network.loadedVersionKnown=%d\n%s.network.loadedVersion=%u.%u.%u.%u\n"
        "%s.network.family=%s\n%s.network.evidence=%s\n%s.network.identityStatus=%s\n"
        "%s.network.loadedPathFileSha256=%s\n%s.network.loadedPathFileBytes=%llu\n%s.network.loadedPathFile=%s\n"
        "%s.network.contract=UInt DLSSNR.Hint.Render.Preset at Feature18 Create; 0=runtime-default,1=original,2=optimized; "
        "file hash is the disk file at the loaded module path, not relocated memory or Authenticode; "
        "version/family/expected-network are inference; successful Create/Evaluate do not verify the internally executed network; "
        "DLL replacement requires restart\n",
        prefix, runtime.generation, prefix, runtime.module_address,
        prefix, runtime.version.known ? 1 : 0, prefix, runtime.version.value[0], runtime.version.value[1],
        runtime.version.value[2], runtime.version.value[3],
        prefix, nr_network_policy::FamilyName(runtime.family), prefix, nr_network_policy::EvidenceName(runtime.evidence),
        prefix, runtime.identity_status, prefix, runtime.sha256,
        prefix, file_matches ? file->file_bytes : 0ull, prefix, file_matches ? file->path_utf8 : "", prefix);

    struct Receipt {
        unsigned layer = 0, evaluations = 0, since_reset = 0;
        unsigned long long instance = 0;
        NrTuningSignature tuning{};
        bool has_handle = false, retired = false;
    } receipts[std::size(g_contexts)]{};
    unsigned count = 0;
    const bool copied = TryEnterCriticalSection(&g_chain_cs) != FALSE;
    if (copied) {
        for (const auto& context : g_contexts) {
            if (!context.tuning_signature_initialized) continue;
            auto& receipt = receipts[count++];
            receipt.layer = context.layer_index + 1;
            receipt.instance = context.instance_id;
            receipt.evaluations = context.evaluations;
            receipt.since_reset = context.successful_evaluations_since_reset;
            receipt.tuning = context.tuning_signature;
            receipt.has_handle = context.nr_handle != nullptr;
            receipt.retired = context.retired;
        }
        LeaveCriticalSection(&g_chain_cs);
    }
    FeedbackAppend(s, "%s.network.nativeContextSnapshotAvailable=%d\n%s.network.nativeContextCount=%u\n"
        "%s.network.nativeContextScope=bounded native before/after contexts at sample time; retired entries are historical; "
        "Present context creation requests are recorded in nr-network-create logs; no executed-network readback exists\n",
        prefix, copied ? 1 : 0, prefix, count, prefix);
    for (unsigned i = 0; i < count; ++i) {
        const auto& receipt = receipts[i];
        FeedbackAppend(s, "%s.network.nativeContext.%u=layer:%u,instance:%llu,request:%d,runtimeGeneration:%llu,"
            "runtimeModule:%llu,hasCreatedHandle:%d,evaluations:%u,sinceReset:%u,retired:%d\n",
            prefix, i, receipt.layer, receipt.instance, receipt.tuning.network_preset,
            receipt.tuning.runtime_generation, receipt.tuning.runtime_module_address,
            receipt.has_handle ? 1 : 0, receipt.evaluations, receipt.since_reset, receipt.retired ? 1 : 0);
    }
}
