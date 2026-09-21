#pragma once
#include <atomic>
#include <cstdint>
#include "nr_external_provider_abi.h"

// Published for F8 single-TXT. Updated by the external provider consumer and the
// native Feature1 note seam; never invents success.
namespace nr_external_provider_feedback {
inline std::atomic<std::uint32_t> last_provenance{
    static_cast<std::uint32_t>(NrGuideProvenance::Unknown)};
inline std::atomic<std::uint32_t> last_reason{
    static_cast<std::uint32_t>(NrExternalProviderReason::Starting)};
inline std::atomic<std::uint64_t> admitted_frames{0};
inline std::atomic<std::uint64_t> writeback_frames{0};
inline std::atomic<std::uint64_t> native_feature1_notes{0};
inline std::atomic<std::uint64_t> external_submits{0};
inline std::atomic<std::uint64_t> completion_frames{0};
inline std::atomic<std::uint64_t> completion_failures{0};
inline std::atomic<std::uint32_t> pending_completions{0};
inline std::atomic<std::uint64_t> owner_provider_id{0};
// 0=none, 1=test-deterministic-copy (NR_TEST_MULTIAPI only),
// 2=native-feature1-note, 3=core Feature18 external-frame path
inline std::atomic<std::uint32_t> nr_backend{0};

inline void Publish(const NrExternalStatusV1 &status) noexcept {
    last_provenance.store(status.last_provenance, std::memory_order_relaxed);
    last_reason.store(status.last_bypass, std::memory_order_relaxed);
    admitted_frames.store(status.admitted_frames, std::memory_order_relaxed);
    writeback_frames.store(status.writeback_frames, std::memory_order_relaxed);
    owner_provider_id.store(status.owner_provider_id, std::memory_order_release);
}
} // namespace nr_external_provider_feedback
