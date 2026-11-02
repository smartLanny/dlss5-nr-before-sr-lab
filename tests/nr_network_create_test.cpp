// Compiles the actual production CreateSignedNrFeature body. The NGX/queue
// services are observation stubs, so this proves binding/admission only.
#include "nr_network_policy.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define NR_HAS_NETWORK_MODEL_SELECTION 1
using NgxResult = unsigned;
constexpr NgxResult kNgxSuccess = 1;
struct ID3D12GraphicsCommandList {};
// #330 reads adapter/process observations around admitted Create calls. This
// network fixture deliberately has no adapter; failed device discovery and
// missing process samples stay explicit while the actual Create body runs.
struct ID3D12Device {};
struct LUID {unsigned LowPart=0;int HighPart=0;};
constexpr const void* IID_ID3D12Device=nullptr;
constexpr bool SUCCEEDED(int value){return value>=0;}
unsigned memory_device_queries=0,memory_samples=0,memory_events=0;
int ID3D12GraphicsCommandList_GetDevice(ID3D12GraphicsCommandList*,const void*,void** out){
    ++memory_device_queries;*out=nullptr;return -1;
}
LUID ID3D12Device_GetAdapterLuid(ID3D12Device*){return {};}
void ID3D12Device_Release(ID3D12Device*){}
namespace nr_process_memory {struct Snapshot {bool attempted=false;};}
struct NgxHandle { unsigned id = 1; };
struct NgxParams {
    bool preset_written = false;
    unsigned preset = 99;
    unsigned style = 17;
};
enum class NrJitterMode { Legacy };
unsigned checks = 0;
void Check(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
struct NrNetworkCreationIdentity { unsigned long long generation = 5, module_address = 0x12340000; };
NrNetworkCreationIdentity runtime_identity{};
NrNetworkCreationIdentity CurrentNrNetworkCreationIdentity() { return runtime_identity; }
nr_network_policy::Family runtime_family = nr_network_policy::Family::OriginalAndOptimized;
nr_network_policy::Evidence runtime_evidence = nr_network_policy::Evidence::LoadedPathFileHash;
struct RuntimeSnapshot {
    unsigned long long generation = 5, module_address = 0x12340000;
    nr_network_policy::Family family = runtime_family;
    nr_network_policy::Evidence evidence = runtime_evidence;
    const char* sha256 = "";
};
RuntimeSnapshot NrNetworkRuntimeSnapshot() { return {runtime_identity.generation, runtime_identity.module_address, runtime_family, runtime_evidence}; }
bool publish_old_hash_on_create = false;
nr_network_policy::Assessment NrNetworkRequestedStatus(int requested) {
    if (publish_old_hash_on_create) {
        runtime_family = nr_network_policy::Family::OriginalOnly;
        runtime_evidence = nr_network_policy::Evidence::LoadedPathFileHash;
        publish_old_hash_on_create = false;
    }
    return nr_network_policy::Assess(requested, runtime_family, runtime_evidence);
}
#include "network-tuning.inl"
struct NrContext {
    LUID memory_adapter{};bool memory_adapter_known=false;
    NgxParams* params = nullptr;
    int bound_create_flags = 0, bound_perf_quality = 5, mode = 1;
    unsigned width = 640, height = 360, layer_index = 0;
    NrTuningSignature tuning_signature{};
    bool tuning_signature_initialized = true, codec_peak_shoulder = false;
    unsigned long long instance_id = 0, feature_generation = 12;
    unsigned successful_evaluations_since_reset = 0;
    bool diagnostic_first_sample_logged = false, create_variant_selected = false;
    NgxHandle* quarantined_create_handle = nullptr;
    bool create_dependencies_quarantined = false, failed = false;
    NgxHandle* nr_handle = nullptr;
    bool lifecycle_dependencies_quarantined = false;
    unsigned frames_seen = 9, next_create_retry_frame = 0, create_failures = 0;
    struct History { unsigned invalidations = 0; void Invalidate() { ++invalidations; } } dual_history;
    void* dual_create_guides[2]{};
};
nr_process_memory::Snapshot NrReadModelMemory(const NrContext&){++memory_samples;return {};}
void NrRememberModelMemory(const NrContext&,const void*,bool,unsigned,
                          const nr_process_memory::Snapshot& before,const nr_process_memory::Snapshot& after){
    Check(!before.attempted&&!after.attempted,"missing adapter memory observations remain missing");
    ++memory_events;
}
struct FeatureRecord {};
enum class BypassReason { None, NetworkInvalidRequest, NetworkUnavailable, RuntimeUnavailable, FailedCreateQuarantine, RuntimeRejected };
BypassReason bypass = BypassReason::None;
void SetBypassReason(BypassReason reason) { bypass = reason; }
unsigned mark_recorded = 0, bridge_calls = 0;
namespace nr_source_runtime { void MarkPrivateRecorded() { ++mark_recorded; } }
namespace nr_processing_runtime { void MarkRecorded() {} }
enum class PrivateNrOperation { Create };
enum class NgxLayer { PrivateBridge };
struct ScopedPrivateNrCall { explicit ScopedPrivateNrCall(PrivateNrOperation) {} };
struct ScopedSrwExclusive { explicit ScopedSrwExclusive(int*) {} };
struct ScopedNgxCreate {
    ScopedNgxCreate(NgxLayer, ID3D12GraphicsCommandList*, int, NgxParams*) {}
    void Finish(NgxResult, const NgxHandle*, NgxParams*) {}
};
namespace nr_timing { enum class Stage { ModelCreate }; enum { ApiReturned = 1, ApiResultKnown = 2 }; }
struct NrTimingScope {
    struct Detail { unsigned flags = 0; int api_result = 0; };
    struct Record { Detail detail; } record;
    explicit NrTimingScope(nr_timing::Stage) {}
    void Stop() {}
};
unsigned generic_context_logs = 0, network_rejection_logs = 0;
void Log(const char* format, ...) {
    if (std::strstr(format, "No free NR context")) ++generic_context_logs;
    if (std::strstr(format, "[nr-network-rejected]")) ++network_rejection_logs;
}
void LogNrCreateMemory(const char*, const NrContext&, NgxResult) {}
void SetInt(NgxParams*, const char*, int) {}
void SetUInt(NgxParams* params, const char* key, unsigned value) {
    Check(params && std::strcmp(key, "DLSSNR.Hint.Render.Preset") == 0, "actual Create uses the exact owned UInt network parameter");
    params->preset_written = true; params->preset = value;
}
NgxResult next_result = kNgxSuccess;
bool return_failure_handle = false;
NgxHandle returned_handle{};
NgxResult ObserveCreate(void*, ID3D12GraphicsCommandList*, int feature, void* opaque, void** handle) {
    ++bridge_calls;
    auto* params = static_cast<NgxParams*>(opaque);
    Check(feature == 18, "network selection only goes to Feature 18");
    Check(params->preset_written && params->preset <= 2, "parameter bound before actual bridge Create");
    Check(params->style == 17, "network selector does not overwrite Style");
    *handle = next_result == kNgxSuccess || return_failure_handle ? &returned_handle : nullptr;
    return next_result;
}
struct Api {
    void* signed_create = nullptr;
    NgxResult (*bridge_create)(void*, ID3D12GraphicsCommandList*, int, void*, void**) = ObserveCreate;
} g_nr_api;
int g_nr_api_lock = 0;
unsigned long long g_next_nr_instance_id = 0;
std::atomic<unsigned> g_config_generation{1};
std::atomic<bool> g_nr_failed_create_process_latched{false};
#include "network-create.inl"

void Reset() {
    bypass = BypassReason::None; mark_recorded = bridge_calls = 0;
    next_result = kNgxSuccess; return_failure_handle = false;
    publish_old_hash_on_create = false;
    generic_context_logs = network_rejection_logs = 0;
    memory_device_queries=memory_samples=memory_events=0;
    runtime_identity = {}; runtime_family = nr_network_policy::Family::OriginalAndOptimized;
    runtime_evidence = nr_network_policy::Evidence::LoadedPathFileHash;
    g_nr_failed_create_process_latched = false;
}
NrContext Context(NgxParams& params, int request) {
    NrContext context; context.params = &params;
    context.tuning_signature.network_preset = request;
    context.tuning_signature.runtime_generation = runtime_identity.generation;
    context.tuning_signature.runtime_module_address = runtime_identity.module_address;
    return context;
}
#ifndef NR_NETWORK_CREATE_FIXTURE_ONLY
int main() {
    ID3D12GraphicsCommandList list; FeatureRecord feature;
    for (int request = 0; request <= 2; ++request) {
        Reset(); NgxParams params; auto context = Context(params, request); void* handle = nullptr;
        bool called = false;
        Check(CreateSignedNrFeature(&list, context, feature, &handle, &called) == kNgxSuccess && called, "known request reaches production Create");
        Check(bridge_calls == 1 && mark_recorded == 1 && params.preset == static_cast<unsigned>(request), "one model Create with the requested network");
        Check(handle && context.instance_id && context.create_variant_selected, "success remains a distinct instance receipt");
        Check(memory_device_queries==1&&memory_samples==2&&memory_events==1,
            "admitted Create retains bounded before/after memory observations");
    }
    for (int request : {-1, 3, 71}) {
        Reset(); NgxParams params; auto context = Context(params, request); void* handle = nullptr;
        bool called = true;
        Check(CreateSignedNrFeature(&list, context, feature, &handle, &called) != kNgxSuccess && !called, "invalid request refused without an API call");
        Check(!bridge_calls && !mark_recorded && !params.preset_written && !handle, "refusal records no private work and no Create");
        Check(!memory_device_queries&&!memory_samples&&!memory_events,"refused Create does not query or publish model memory");
    }
    Reset(); NgxParams params; auto context = Context(params, 2); void* handle = nullptr;
    runtime_family = nr_network_policy::Family::OriginalOnly;
    bool called = true;
    Check(CreateSignedNrFeature(&list, context, feature, &handle, &called) != kNgxSuccess && !bridge_calls && !called, "known legacy runtime cannot silently adopt optimized");
    Check(bypass == BypassReason::NetworkUnavailable && context.tuning_signature.network_preset == 2, "missing-network refusal preserves saved request");
    Reset(); params = {}; context = Context(params, 2); runtime_family = nr_network_policy::Family::Unknown; runtime_evidence = nr_network_policy::Evidence::Unknown;
    Check(CreateSignedNrFeature(&list, context, feature, &handle) == kNgxSuccess, "unknown compatible runtime is not rejected using version assumptions");
    Reset(); params = {}; context = Context(params, 1); ++runtime_identity.generation;
    Check(CreateSignedNrFeature(&list, context, feature, &handle) != kNgxSuccess && !bridge_calls && !mark_recorded, "stale module generation never reaches Create");
    Reset(); params = {}; context = Context(params, 1); ++runtime_identity.module_address;
    Check(CreateSignedNrFeature(&list, context, feature, &handle) != kNgxSuccess && !bridge_calls && !mark_recorded, "different runtime module never reuses old context");
    Reset(); params = {}; context = Context(params, 1); next_result = 0xbad00005u; return_failure_handle = true;
    Check(CreateSignedNrFeature(&list, context, feature, &handle) == next_result, "opaque model error remains an error");
    Check(!handle && context.quarantined_create_handle && context.create_dependencies_quarantined && context.failed && g_nr_failed_create_process_latched,
        "failed Create with handle still quarantines all dependencies");
    std::printf("PASS %u production Create admission/binding/ownership checks; mocked NGX boundary only\n", checks);
}
#endif
