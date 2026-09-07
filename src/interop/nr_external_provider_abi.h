#pragma once
#include <cstdint>

// Versioned Feeder/external producer <-> NR consumer ABI (MC1).
// CPU handshake and opaque GPU handles only. Never adopts COM ownership across
// the DLL boundary; the producer retains resource/command lifetime and must
// keep resources, descriptors and allocator valid until the real Execute batch
// fence completes. Submit only records commands; its return is not GPU completion.
// Exact version/size; x64 Windows WINAPI at the DLL boundary.
constexpr std::uint32_t kNrExternalProviderAbi = 0x00010000u;
constexpr std::uint64_t kNrExternalProviderIdDl5f = 0x444C3546ull; // 'DL5F'

enum class NrGuideProvenance : std::uint32_t {
    Unknown = 0,
    Native = 1,     // game/NGX Feature1 guides
    Extracted = 2,  // engine-structure extraction
    Synthetic = 3,  // ReShade depth + OFA/estimated MV (Feeder path)
};

enum class NrExternalMotionUnit : std::uint32_t {
    Pixels = 0,
    Normalized = 1,
};

enum class NrExternalProviderReason : std::uint32_t {
    Ready = 0,
    Starting,
    Disabled,              // F6/master off or zero strength: no NR work
    CompetingNr,           // another NR owner already claimed
    Busy,
    MissingRequired,       // color/output (or required guides) absent
    TransportFailed,       // copy/writeback could not be recorded
    EpochMismatch,
    ViewMismatch,
    FrameMismatch,
    LeaseInvalid,          // queue/fence/resource state lease rejected
    UnsupportedContract,
    IdentityPassthrough,   // admitted but left the original image untouched
    HistoryResetRequired,
    ObserverUncertain,
    RuntimeUnavailable,    // NR API/codec/hooks not ready; decline (never fake copy-success)
};

enum class NrExternalLeaseFlags : std::uint32_t {
    None = 0,
    CommandListOwnedByProducer = 1u << 0,
    QueueFenceRequired = 1u << 1,
    OutputSameAsColor = 1u << 2, // in-place writeback (Feeder backbuffer)
    ResetTemporal = 1u << 3,
    DepthInverted = 1u << 4, // Feeder/ReShade depth_reversed → Feature18 bit 8
    HdrColor = 1u << 5,      // HDR color create flag bit 0
};

struct NrExternalResourceV1 {
    std::uint64_t resource = 0; // ID3D12Resource* as integer; never dereferenced here
    std::uint32_t subresource = 0;
    std::uint32_t format = 0;   // DXGI_FORMAT numeric
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t rect_x = 0;
    std::uint32_t rect_y = 0;
    std::uint32_t rect_w = 0;
    std::uint32_t rect_h = 0;
    std::uint32_t state_before = 0; // D3D12_RESOURCE_STATES
    std::uint32_t state_after = 0;
    NrGuideProvenance provenance = NrGuideProvenance::Unknown;
    NrExternalMotionUnit motion_unit = NrExternalMotionUnit::Pixels;
    float motion_scale_x = 1.0f;
    float motion_scale_y = 1.0f;
    float jitter_x = 0.0f;
    float jitter_y = 0.0f;
};
static_assert(sizeof(NrExternalResourceV1) == 72);

struct NrExternalFrameV1 {
    std::uint32_t size = sizeof(NrExternalFrameV1);
    std::uint32_t abi = kNrExternalProviderAbi;
    std::uint64_t provider_id = 0;
    std::uint64_t view_id = 0;
    std::uint64_t epoch = 0;
    std::uint64_t frame_id = 0;
    std::uint32_t lease_flags = 0;
    std::uint32_t reserved = 0;
    float pre_exposure = 1.0f;
    float exposure_scale = 1.0f;
    NrExternalResourceV1 color{};
    NrExternalResourceV1 depth{};
    NrExternalResourceV1 motion{};
    NrExternalResourceV1 exposure{};
    NrExternalResourceV1 output{};
    std::uint64_t command_list = 0; // ID3D12GraphicsCommandList*
    std::uint64_t queue = 0;        // ID3D12CommandQueue* (optional unless lease requires)
    std::uint64_t fence = 0;        // ID3D12Fence* (optional unless lease requires)
    std::uint64_t fence_value = 0;
};
static_assert(sizeof(NrExternalFrameV1) == 448);

struct NrExternalStatusV1 {
    std::uint32_t size = sizeof(NrExternalStatusV1);
    std::uint32_t abi = kNrExternalProviderAbi;
    NrExternalProviderReason reason = NrExternalProviderReason::Starting;
    std::uint32_t reserved = 0;
    std::uint64_t observer_epoch = 0;
    std::uint64_t owner_provider_id = 0;
    std::uint64_t admitted_frames = 0;
    std::uint64_t writeback_frames = 0;
    std::uint32_t last_provenance = 0; // NrGuideProvenance of color
    std::uint32_t last_bypass = 0;     // NrExternalProviderReason numeric for F8
};
static_assert(sizeof(NrExternalStatusV1) == 56);

struct NrExternalSubmitResultV1 {
    std::uint32_t size = sizeof(NrExternalSubmitResultV1);
    std::uint32_t abi = kNrExternalProviderAbi;
    NrExternalProviderReason reason = NrExternalProviderReason::Starting;
    std::uint32_t writeback_performed = 0; // NR writeback recorded; producer still must submit
    std::uint64_t admitted_frame_id = 0;
    std::uint32_t history_reset = 0;
    std::uint32_t color_provenance = 0;
    char bypass[64]{};
};
static_assert(sizeof(NrExternalSubmitResultV1) == 96);

inline const char *NrGuideProvenanceName(NrGuideProvenance p) noexcept {
    switch (p) {
    case NrGuideProvenance::Native: return "Native";
    case NrGuideProvenance::Extracted: return "Extracted";
    case NrGuideProvenance::Synthetic: return "Synthetic";
    default: return "Unknown";
    }
}

inline const char *NrExternalProviderReasonName(NrExternalProviderReason r) noexcept {
    switch (r) {
    case NrExternalProviderReason::Ready: return "ready";
    case NrExternalProviderReason::Starting: return "starting";
    case NrExternalProviderReason::Disabled: return "master-disabled";
    case NrExternalProviderReason::CompetingNr: return "competing-nr-owner";
    case NrExternalProviderReason::Busy: return "busy";
    case NrExternalProviderReason::MissingRequired: return "missing-required-input";
    case NrExternalProviderReason::TransportFailed: return "transport-failed";
    case NrExternalProviderReason::EpochMismatch: return "epoch-mismatch";
    case NrExternalProviderReason::ViewMismatch: return "view-mismatch";
    case NrExternalProviderReason::FrameMismatch: return "frame-mismatch";
    case NrExternalProviderReason::LeaseInvalid: return "lease-invalid";
    case NrExternalProviderReason::UnsupportedContract: return "unsupported-contract";
    case NrExternalProviderReason::IdentityPassthrough: return "identity-passthrough";
    case NrExternalProviderReason::HistoryResetRequired: return "history-reset-required";
    case NrExternalProviderReason::ObserverUncertain: return "observer-uncertain";
    case NrExternalProviderReason::RuntimeUnavailable: return "runtime-unavailable";
    default: return "unknown";
    }
}

inline bool NrExternalStatusAllowsSubmit(const NrExternalStatusV1 &s) noexcept {
    return s.size == sizeof(s) && s.abi == kNrExternalProviderAbi && !s.reserved &&
        s.reason == NrExternalProviderReason::Ready && !(s.observer_epoch & 1u);
}
