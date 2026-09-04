#include "nr_jitter_status.h"
// g_chain_cs owns both the recorded sample and its live-context validation.
nr_jitter_status::Snapshot g_recorded_jitter = {};

void RecordSuccessfulJitterBinding(const NrContext &context,
                                   unsigned long long frame) {
    if (!context.jitter_binding_written || context.instance_id == 0) return;
    g_recorded_jitter = {context.config_generation, context.feature_generation,
        context.instance_id, frame, GetTickCount64(), context.mode,
        context.jitter_last_known, nr_jitter_status::Status::NoFrame};
}

nr_jitter_status::Snapshot SnapshotJitterStatus() {
    EnterCriticalSection(&g_chain_cs);
    auto snapshot = g_recorded_jitter;
    bool live_instance = false;
    for (const auto &context : g_contexts) {
        if (context.instance_id == snapshot.instance && snapshot.instance != 0 &&
            context.feature_generation == snapshot.feature && !context.retired &&
            !context.failed && context.nr_handle != nullptr) {
            live_instance = true;
            break;
        }
    }
    snapshot.status = nr_jitter_status::StatusFor(snapshot,
        g_config_generation.load(std::memory_order_relaxed),
        g_pipeline_stats_generation.load(std::memory_order_relaxed), g_config.mode,
        live_instance, g_config.enabled && ActiveTransferStrength(g_config) > 0.001f,
        g_last_bypass_reason.load(std::memory_order_relaxed) !=
            static_cast<int>(BypassReason::None), GetTickCount64());
    LeaveCriticalSection(&g_chain_cs);
    return snapshot;
}
