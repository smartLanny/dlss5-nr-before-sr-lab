#pragma once
#include <cstdint>
#include <cstddef>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12CommandQueue;
struct ID3D12Fence;

namespace nr::nvfp4 {
enum class Mode : uint32_t { Disabled, Auto, Passthrough, UpstreamReference, Optimized };
enum class Phase : uint32_t { Create, Evaluate };
enum class State : uint32_t { Disabled, Observing, Preparing, Ready, Fallback, Quarantined };
enum class Stage : uint32_t { OriginalPair, ReplacementPair, Pack, Expansion, Contraction, Epilogue };
// A diagnostic record describes one unchanged NVAPI Launch call. A call with
// launch_count != 1 is one inseparable batch, not individual kernel timings.
// String/hash pointers are borrowed only for the observer callback.
struct OriginalLaunchInfo {
    uint64_t sequence = 0;
    uintptr_t function = 0, module = 0;
    const char* function_name = nullptr;
    const uint8_t* module_sha256 = nullptr;
    uint32_t launch_count = 0, params_bytes = 0, shared_bytes = 0;
    uint32_t grid[3] = {}, block[3] = {};
    bool identity_live = false, ffn_target = false;
};
using OriginalLaunchObserver = void (*)(const OriginalLaunchInfo&, bool begin,
    int32_t status, ID3D12GraphicsCommandList*, void*) noexcept;
struct ExpansionAcceptance {
    const wchar_t* directory = nullptr;
    const char* cubin_sha256 = nullptr;
    const char* builder_sha256 = nullptr;
    const char* build_id = nullptr;
    const char* kernel_id = nullptr;
    uint32_t variant = 0;
};
struct OptimizedAcceptance {
    const wchar_t* directory = nullptr;
    const char* cubin_sha256 = nullptr;
    const char* builder_sha256 = nullptr;
    const char* build_id = nullptr;
    const char* kernel_id = nullptr;
    uint32_t variant = 0;
    const uint32_t* tested_m = nullptr;
    uint32_t tested_m_count = 0;
    // A request cannot bypass the compiled qualified policy registry. Explicit
    // Optimized host experiments do not automatically admit production Auto.
    bool allow_auto = false;
    bool fused_publish = false;
    // An explicit host experiment may replace pack+expansion with one kernel.
    // It inherits the tested M set and requires the fused publication path.
    const ExpansionAcceptance* expansion = nullptr;
};
struct Config {
    Mode mode = Mode::Disabled;
    bool diagnostic_passthrough = false;
    // Absolute directory containing the locked upstream hybrid assets.
    const wchar_t* asset_directory = nullptr;
    // Full path of the actual loaded, signed NR runtime used by this Core.
    // The fixed quantized weights are admitted only for its approved digest.
    const wchar_t* nr_runtime_path = nullptr;
    uint64_t device_budget_bytes = 256ull << 20;
    // Optional private host diagnostics. Data is borrowed for the duration of
    // the callback. No callback is used by the production default.
    void (*observe_module)(const void*, uint32_t, const uint8_t sha256[32], void*) = nullptr;
    void (*observe_launch)(const char* function_name, const void* params, uint32_t bytes,
                           const uint32_t grid[3], const uint32_t block[3],
                           uint32_t shared_bytes, void*) = nullptr;
    void* observer_user = nullptr;
    void (*observe_stage)(Stage stage, bool begin, uint32_t block_id,
                          ID3D12GraphicsCommandList*, void*) = nullptr;
    const OptimizedAcceptance* optimized_acceptance = nullptr;
    // Explicit private profiling only. Arm before loading the NR runtime to
    // retain all function names and module hashes. Called immediately before
    // and after a forwarded Launch; status is meaningful only on the end.
    // A failed call may have recorded work and must not count as a valid sample.
    // Must not allocate, wait, perform file I/O, or throw. Disabled remains
    // passive: no CUDA/device preparation or replacement is enabled.
    OriginalLaunchObserver observe_original_launch = nullptr;
    // Range diagnostics may observe the incoming call stream, including calls
    // replaced by this backend. Their sequence then stays aligned with FP8.
    // Default false preserves the forwarded-original-only diagnostic contract.
    bool observe_original_input_calls = false;
};
struct OwnedCall {
    Phase phase = Phase::Evaluate;
    uint64_t context_id = 0;
    uint64_t generation = 0;
    uint64_t recording_token = 0;
    uint64_t evaluation_serial = 0;
    ID3D12Device* device = nullptr;
    ID3D12GraphicsCommandList* command_list = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t feature_id = 18;
    bool pre_sr = true;
    bool allow_replacement = true;
};
struct Snapshot {
    State state = State::Disabled;
    Mode mode = Mode::Disabled;
    uint32_t cuda_sm_count = 0;
    uint32_t compute_major = 0;
    uint32_t compute_minor = 0;
    uint64_t observed_m = 0;
    uint64_t replaced_pairs = 0;
    uint64_t original_launches = 0;
    uint64_t replacement_launches = 0;
    uint64_t uav_barriers = 0;
    uint64_t live_device_bytes = 0;
    uint64_t completed_recordings = 0;
    bool may_have_recorded_failure = false;
    char reason[192] = {};
};

// Cold configuration. Disabled never prepares CUDA resources or replaces GPU
// work. Init itself does not load DLLs or install hooks.
bool Init(const Config& config) noexcept;
// Explicit cold opt-in, before loading the owned NR runtime. Installs only the
// passive NVAPI observer, including while Disabled, so later Off-to-Auto changes
// retain module/function identities. Does not change mode, register a device,
// start the preparation worker or prepare CUDA resources. Failure is diagnostic
// only: the caller must keep the original NR runtime available.
bool ArmObserver() noexcept;
bool EnsureStarted(ID3D12Device* device) noexcept;
// Stops accepting replacements. Recorded resources remain pinned until their
// queue fence completes and their command list has successfully reset.
void Shutdown() noexcept;
class OwnedScope {
public:
    explicit OwnedScope(const OwnedCall& call) noexcept;
    ~OwnedScope();
    OwnedScope(const OwnedScope&) = delete;
    OwnedScope& operator=(const OwnedScope&) = delete;
    bool MayHaveRecordedFailure() const noexcept;
    // Frozen when this scope is constructed; Ready work discovered later in
    // this Evaluate cannot upgrade it. Original ABI checks can still reject.
    bool WillReplace() const noexcept;
    Snapshot Status() const noexcept;
private:
    alignas(std::max_align_t) unsigned char storage_[768];
    void* implementation_ = nullptr;
    void* previous_ = nullptr;
};
Snapshot GetStatus(uint64_t context_id = 0, uint64_t generation = 0) noexcept;
void OnSubmit(uint64_t recording_token, ID3D12CommandQueue* queue,
              ID3D12Fence* fence, uint64_t fence_value) noexcept;
// definitely_unsubmitted is true only when the owner proves that the recording
// was never submitted. An uncertain discard must use OnQuarantine instead.
void OnReset(uint64_t recording_token, bool definitely_unsubmitted = false) noexcept;
void OnQuarantine(uint64_t recording_token, const char* reason) noexcept;
void RetireContext(uint64_t context_id, uint64_t generation) noexcept;

// Host/testing integration can wrap an already resolved NVAPI entry. Unknown
// IDs are returned byte-for-byte, and every out-of-scope launch is forwarded.
void* WrapNvapi(uint32_t id, void* original) noexcept;
} // namespace nr::nvfp4
