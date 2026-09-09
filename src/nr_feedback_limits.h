#pragma once

// Both buffers are members of heap-owned capture/package objects. Keep one
// fixed bound from the live writer through CreateJob, including the final NUL.
namespace nr_feedback_limits {
inline constexpr unsigned int kMetadataBytes = 128u * 1024u;
inline constexpr unsigned int kFinalStatusReserveBytes = 256u;
// Actual full-width end snapshots (including timing) use <72 KiB. Leave room
// for bounded worker fingerprints/inventory and the final collection status.
inline constexpr unsigned int kMandatoryTailBytes = 80u * 1024u;
inline constexpr unsigned int kModuleInventoryBytes = 4u * 1024u;
// The timeline gate runs before an observation burst, which may also append
// the bounded NGX/resource summary on observations 1 and 60.
inline constexpr unsigned int kTimelineBurstBytes = 4u * 1024u;
inline constexpr unsigned int kTimelineReserveBytes = kMandatoryTailBytes + kTimelineBurstBytes;
static_assert(kFinalStatusReserveBytes < kMandatoryTailBytes);
static_assert(kTimelineReserveBytes < kMetadataBytes);
}
